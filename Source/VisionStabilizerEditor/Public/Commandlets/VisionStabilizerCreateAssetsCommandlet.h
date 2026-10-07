#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "VisionStabilizerCreateAssetsCommandlet.generated.h"

/**
 * @brief Creates the default clamp CurveFloat from an unattended editor command.
 * @note Invoke with -run=VisionStabilizerCreateAssets and optionally
 * -Package=/Game/Curves/CF_Clamp. Existing assets are left unchanged.
 */
UCLASS()
class VISIONSTABILIZEREDITOR_API UVisionStabilizerCreateAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    /** @brief Enables editor asset operations and console logging without a client or server. */
    UVisionStabilizerCreateAssetsCommandlet();
    /**
     * @brief Parses the destination and creates or reuses a saved curve.
     * @param Params Command-line arguments; Package overrides the default plugin asset path.
     * @return Zero for a created or existing saved curve; one when creation fails.
     */
    virtual int32 Main(const FString& Params) override;
};
