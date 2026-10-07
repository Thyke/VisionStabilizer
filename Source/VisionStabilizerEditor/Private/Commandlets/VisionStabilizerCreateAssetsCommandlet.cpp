#include "Commandlets/VisionStabilizerCreateAssetsCommandlet.h"

#include "Assets/VisionStabilizerDefaultAssets.h"
#include "Misc/Parse.h"

UVisionStabilizerCreateAssetsCommandlet::UVisionStabilizerCreateAssetsCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UVisionStabilizerCreateAssetsCommandlet::Main(const FString& Params)
{
    FString PackageName = VisionStabilizerDefaultAssets::DefaultCurvePackage;
    FParse::Value(*Params, TEXT("Package="), PackageName);
    FString Message;
    const bool bSuccess = VisionStabilizerDefaultAssets::CreateDefaultCurve(PackageName, Message) != nullptr;
    if (bSuccess)
    {
        UE_LOG(LogTemp, Display, TEXT("VisionStabilizer: %s"), *Message);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("VisionStabilizer: %s"), *Message);
    }
    return bSuccess ? 0 : 1;
}
