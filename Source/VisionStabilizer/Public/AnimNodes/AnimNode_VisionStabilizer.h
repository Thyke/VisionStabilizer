#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTypes.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Curves/RichCurve.h"
#include "VisionStabilizerCore.h"
#include "AnimNode_VisionStabilizer.generated.h"

class UCurveFloat;

/** @brief Selects whether clamp radius comes from a scalar input or a speed curve. */
UENUM(BlueprintType)
enum class EVisionStabilizerClampMode : uint8
{
    Manual UMETA(DisplayName = "Manual"), ///< Use the POV Clamp value directly.
    GroundSpeedCurve UMETA(DisplayName = "Ground-Speed Curve") ///< Sample the curve at normalized ground speed.
};

/**
 * @brief Stabilizes a dedicated virtual POV bone with a component-space positional dead zone.
 *
 * Only PovHeadBone is written. The target retains rotation and scale from the
 * first valid incoming POV pose after a reset; the real head remains untouched.
 * @note FAnimNode_SkeletalControlBase blends the full target transform with alpha
 * once. The persistent filter state stores the unblended position.
 */
USTRUCT(BlueprintInternalUseOnly)
struct VISIONSTABILIZER_API FAnimNode_VisionStabilizer : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

    /** @brief Defaults the bone names to head and VB pov_head. */
    FAnimNode_VisionStabilizer();

    /** @brief Real animated head used as the clamp center; never written by this control. */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Bones", meta = (NeverAsPin))
    FBoneReference HeadBone;

    /** @brief Dedicated leaf virtual bone receiving the target; attach the camera here. */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Bones", meta = (NeverAsPin, DisplayName = "POV Head Bone"))
    FBoneReference PovHeadBone;

    /** @brief Radius source; switching modes disconnects the inactive pin and binding. */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Dynamic Clamp", meta = (NeverAsPin))
    EVisionStabilizerClampMode ClampMode = EVisionStabilizerClampMode::Manual;

    /**
     * @brief Clamp radius in component-space centimeters; zero follows the head.
     * @note Curve mode uses the stored literal as its fallback. Switching away
     * from Manual disconnects the pin and binding without baking a runtime value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Stabilizer|Manual Clamp",
        meta = (PinShownByDefault, ClampMin = "0.0", UIMin = "0.0", Units = "cm", DisplayName = "POV Clamp"))
    float PovClamp = 5.0f;

    /** @brief Horizontal movement speed in cm/s, normally supplied from Velocity.Size2D(). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Stabilizer|Dynamic Clamp",
        meta = (PinShownByDefault, ClampMin = "0.0", UIMin = "0.0", Units = "cm/s",
        EditCondition = "ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve", EditConditionHides))
    float GroundSpeed = 0.0f;

    /**
     * @brief Curve mapping normalized speed [0, 1] to clamp radius in centimeters.
     * @note Missing keys or non-finite samples use PovClamp. Curve data is copied
     * on the game thread before worker evaluation; this property is not a runtime pin.
     */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Dynamic Clamp",
        meta = (NeverAsPin, DisplayName = "POV Clamp Curve",
        EditCondition = "ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve", EditConditionHides))
    TObjectPtr<UCurveFloat> PovClampCurve = nullptr;

    /** @brief Ground speed in cm/s mapped to curve X = 0; runtime sorts reversed bounds. */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Dynamic Clamp",
        meta = (NeverAsPin, ClampMin = "0.0", Units = "cm/s",
        EditCondition = "ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve", EditConditionHides))
    float MinGroundSpeed = 0.0f;

    /** @brief Ground speed in cm/s mapped to curve X = 1; a collapsed range uses X = 0. */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Dynamic Clamp",
        meta = (NeverAsPin, ClampMin = "0.0", Units = "cm/s",
        EditCondition = "ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve", EditConditionHides))
    float MaxGroundSpeed = 600.0f;

    /**
     * @brief Signed height offset in centimeters along mesh component +Z.
     * @note Applied after filtering so adjustments are immediate. The axis is
     * component up, which may differ from world or camera up.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Stabilizer|Settings",
        meta = (PinHiddenByDefault, Units = "cm", DisplayName = "POV Height Offset"))
    float PovHeightOffset = 0.0f;

    /**
     * @brief Resets history while true; pulse for one update to capture a new POV seed.
     * @note Holding true recaptures the incoming transform on every active update.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Stabilizer|Settings",
        meta = (PinHiddenByDefault))
    bool bResetStabilization = false;

    /**
     * @brief Queues foreground debug primitives through the animation instance proxy.
     * @note Worker evaluation does not access UWorld or call DrawDebug directly.
     */
    UPROPERTY(EditAnywhere, Category = "Vision Stabilizer|Debug", meta = (NeverAsPin))
    bool bEnableDebugDraw = false;

    /** @brief Resets pose history while preserving any curve snapshot prepared by PreUpdate. */
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    /** @brief Appends current control diagnostics and visits the input pose. */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
    /** @return True to request the game-thread curve snapshot hook. */
    virtual bool HasPreUpdate() const override { return true; }
    /** @brief Copies curve data on the game thread; editor key edits refresh the snapshot. */
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
    /** @return True to receive animation dynamics resets. */
    virtual bool NeedsDynamicReset() const override { return true; }
    /** @brief Discards pose history for any teleport type; the next valid pose supplies the seed. */
    virtual void ResetDynamics(ETeleportType InTeleportType) override;

protected:
    /** @brief Detects update gaps and handles the reset pin after base input and alpha processing. */
    virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
    /** @brief Resolves the current compact-pose indices and validates a real head plus leaf virtual POV. */
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    /** @return True when the validated, distinct bone pair exists in the current required bones. */
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    /**
     * @brief Emits one unblended component-space POV transform, or passes through invalid input.
     * @param Output Incoming component-space pose; the real head is only read.
     * @param OutBoneTransforms Empty output array receiving the virtual POV target.
     */
    virtual void EvaluateSkeletalControl_AnyThread(
        FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;

    /**
     * @brief Resolves a finite radius using node inputs and the owned curve snapshot.
     * @param OutNormalizedSpeed Curve X in [0, 1], or zero in Manual mode.
     * @param bOutUsedFallback True when curve mode falls back to the manual literal.
     * @return Non-negative clamp radius in centimeters.
     * @note Safe for worker evaluation; does not access the curve UObject.
     */
    float ResolveClampRadius(float& OutNormalizedSpeed, bool& bOutUsedFallback) const;

private:
    /** @brief Clears the persistent position, captured transform and correction distance. */
    void ResetState();
    /** @brief Queues the component-space clamp envelope and targets through the proxy in debug builds. */
    void QueueDebugDraw(FAnimInstanceProxy& Proxy, const FVector& CenterCS,
        const FVector& TargetCS, const FVector& IncomingCS, float Radius) const;

    VisionStabilizerCore::FPositionState PositionState; ///< Unblended position before height offset.
    FTransform CapturedPovTransformCS = FTransform::Identity; ///< Seed transform supplying target rotation and scale.
    FGraphTraversalCounter LastUpdateCounter; ///< Detects interruptions in this control's update history.
    bool bBonePairValid = false; ///< Result of validating the current required-bone set.

    /** @brief Owned curve data written by PreUpdate and read during worker evaluation. */
    FRichCurve CurveSnapshot;
    /** @brief Game-thread-only identity cache used to detect a different curve asset. */
    TWeakObjectPtr<UCurveFloat> CachedCurveSource;
    bool bHasCurveSnapshot = false; ///< Whether the current snapshot has sampleable keys.

    float LastClampRadius = 5.0f; ///< Last resolved radius in centimeters.
    float LastNormalizedSpeed = 0.0f; ///< Last resolved curve input.
    float LastCorrectionDistance = 0.0f; ///< Incoming-to-target distance in centimeters; cleared on reset.
    bool bLastUsedFallback = false; ///< Whether the last radius resolution used the manual fallback.
};
