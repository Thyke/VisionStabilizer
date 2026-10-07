#include "Modules/ModuleManager.h"

#include "Assets/VisionStabilizerDefaultAssets.h"
#include "HAL/IConsoleManager.h"

class FVisionStabilizerEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        CreateCurveCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("VisionStabilizer.CreateDefaultCurve"),
            TEXT("Create and save the default CurveFloat without overwriting existing content. Optional argument: /Game/Folder/AssetName"),
            FConsoleCommandWithArgsDelegate::CreateStatic(&FVisionStabilizerEditorModule::CreateCurve),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (CreateCurveCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(CreateCurveCommand, false);
            CreateCurveCommand = nullptr;
        }
    }

private:
    static void CreateCurve(const TArray<FString>& Args)
    {
        if (Args.Num() > 1)
        {
            UE_LOG(LogTemp, Error, TEXT("Usage: VisionStabilizer.CreateDefaultCurve [/Game/Folder/CurveName]"));
            return;
        }
        const FString PackageName = Args.IsEmpty()
            ? FString(VisionStabilizerDefaultAssets::DefaultCurvePackage) : Args[0];
        FString Message;
        if (VisionStabilizerDefaultAssets::CreateDefaultCurve(PackageName, Message))
        {
            UE_LOG(LogTemp, Display, TEXT("VisionStabilizer: %s"), *Message);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("VisionStabilizer: %s"), *Message);
        }
    }

    IConsoleObject* CreateCurveCommand = nullptr;
};

IMPLEMENT_MODULE(FVisionStabilizerEditorModule, VisionStabilizerEditor)
