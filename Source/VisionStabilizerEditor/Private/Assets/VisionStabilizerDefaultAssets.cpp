#include "Assets/VisionStabilizerDefaultAssets.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "VisionStabilizerCore.h"

UCurveFloat* VisionStabilizerDefaultAssets::CreateDefaultCurve(
    const FString& PackageName, FString& OutMessage)
{
    check(IsInGameThread());
    if ((!PackageName.StartsWith(TEXT("/Game/")) && !PackageName.StartsWith(TEXT("/VisionStabilizer/"))) ||
        !FPackageName::IsValidLongPackageName(PackageName))
    {
        OutMessage = TEXT("Use a valid long package name under /Game/ or /VisionStabilizer/.");
        return nullptr;
    }

    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(
        PackageName, Filename, FPackageName::GetAssetPackageExtension()))
    {
        OutMessage = TEXT("Package mount is unavailable. Enable the plugin and restart the editor.");
        return nullptr;
    }
    Filename = FPaths::ConvertRelativePathToFull(Filename);
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    // Check memory as well as disk: an unsaved editor object can already own
    // this path, and loading a failed package must never turn into an overwrite.
    UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
    if (!Existing && IFileManager::Get().FileExists(*Filename))
    {
        Existing = LoadObject<UObject>(nullptr, *ObjectPath);
        if (!Existing)
        {
            OutMessage = TEXT("An existing package could not be loaded. It was NOT overwritten.");
            return nullptr;
        }
    }
    if (Existing)
    {
        UCurveFloat* ExistingCurve = Cast<UCurveFloat>(Existing);
        if (ExistingCurve && !IFileManager::Get().FileExists(*Filename))
        {
            OutMessage = TEXT("An unsaved curve already occupies this object path. Save it explicitly or choose another path; nothing was overwritten.");
            return nullptr;
        }
        OutMessage = ExistingCurve
            ? FString::Printf(TEXT("Curve already exists and was left unchanged: %s"), *ObjectPath)
            : FString::Printf(TEXT("An object of another type exists at %s; nothing was overwritten."), *ObjectPath);
        return ExistingCurve;
    }

    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
    {
        OutMessage = TEXT("Could not create the Content directory. Check write permissions/source control.");
        return nullptr;
    }
    UPackage* Package = CreatePackage(*PackageName);
    UCurveFloat* Curve = NewObject<UCurveFloat>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    // Linear keys and constant extrapolation keep the default radius predictable
    // at the normalized-speed endpoints and outside the authored key range.
    Curve->FloatCurve.PreInfinityExtrap = RCCE_Constant;
    Curve->FloatCurve.PostInfinityExtrap = RCCE_Constant;
    for (const auto& Key : VisionStabilizerCore::DefaultCurveKeys)
    {
        const FKeyHandle Handle = Curve->FloatCurve.AddKey(Key.Time, Key.Value);
        Curve->FloatCurve.SetKeyInterpMode(Handle, RCIM_Linear);
    }
    Curve->MarkPackageDirty();

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Curve, *Filename, SaveArgs))
    {
        OutMessage = FString::Printf(TEXT("Failed to save %s. Check permissions and the Output Log; the new object is still unsaved."), *Filename);
        return nullptr;
    }
    // Announce only after the save succeeds; failure leaves an unsaved object
    // that the next call must treat as a conflict rather than a completed asset.
    FAssetRegistryModule::AssetCreated(Curve);
    OutMessage = FString::Printf(TEXT("Created CurveFloat: %s -> %s"), *ObjectPath, *Filename);
    return Curve;
}
