#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AnimNodes/AnimNode_VisionStabilizer.h"
#include "AnimGraphNode_VisionStabilizer.generated.h"

/**
 * @brief AnimGraph editor node with bone validation and mode-specific input pins.
 * @note Lives in the uncooked module; the runtime module does not depend on it.
 */
UCLASS()
class VISIONSTABILIZEREDITOR_API UAnimGraphNode_VisionStabilizer : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    /** @brief Runtime settings serialized into the compiled Animation Blueprint. */
    UPROPERTY(EditAnywhere, Category = "Settings")
    FAnimNode_VisionStabilizer Node;

    /** @return Display title shared by all graph title contexts. */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    /** @return Filter behavior and the mode-switch connection rules shown in the graph. */
    virtual FText GetTooltipText() const override;
    /** @return Skeletal Controls, the node's action menu category. */
    virtual FString GetNodeCategory() const override;
    /**
     * @brief Checks bone selection, curve settings and inactive connections at compile time.
     * @param ForSkeleton Concrete skeleton, or null when one is not available.
     * @param MessageLog Receives errors for unsafe setups and warnings for recoverable settings.
     */
    virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
    /** @brief Shows settings for the selected mode and exposes the manual fallback for missing curves. */
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
    /** @brief Makes inactive mode pins hidden, non-connectable and ineligible for orphan retention. */
    virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const override;
    /** @return True only when the pin is active and the base node allows a property binding. */
    virtual bool IsPinBindable(const UEdGraphPin* InPin) const override;
    /** @brief Copies unlinked mode-pin literals into Node so hidden fallback values stay in sync. */
    virtual void CopyPinDefaultsToNodeData(UEdGraphPin* InPin) override;
    /** @brief Applies mode visibility before and after the base optional-pin allocation. */
    virtual void AllocateDefaultPins() override;
    /** @brief Removes inactive wiring before the base class migrates old pins. */
    virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
    /** @brief Reconstructs mode pins and marks the Blueprint structurally modified after a mode edit. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    /** @brief Rebuilds pin visibility and connections from the restored transaction state. */
    virtual void PostEditUndo() override;

protected:
    /** @return Controller label used by skeletal-control editor tooling. */
    virtual FText GetControllerDescription() const override;
    /** @return Runtime control settings owned by this graph node. */
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }

private:
    /** @return True for the manual radius and ground-speed properties. */
    bool IsModePin(FName Name) const;
    /** @return True when the selected mode does not consume this property. */
    bool IsInactiveModePin(FName Name) const;
    /**
     * @brief Updates optional-pin records while preserving an active pin's visibility choice.
     * @param bForceActivePinVisible Expose the newly selected mode's input after a mode edit.
     */
    void ConfigureModePins(bool bForceActivePinVisible);
    /**
     * @brief Removes inactive links and property bindings without baking connected runtime values.
     * @param CandidatePins Current or reconstruction-time pins; copied before link notifications fire.
     */
    void DisconnectInactiveModePins(const TArray<UEdGraphPin*>& CandidatePins);
};
