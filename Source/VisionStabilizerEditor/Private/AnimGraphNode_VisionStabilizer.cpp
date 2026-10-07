#include "AnimGraphNode_VisionStabilizer.h"

#include "Animation/Skeleton.h"
#include "Curves/CurveFloat.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "PropertyHandle.h"
#include "ReferenceSkeleton.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "VisionStabilizerGraphNode"

FText UAnimGraphNode_VisionStabilizer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    (void)TitleType;
    return LOCTEXT("Title", "Vision Stabilizer");
}

FText UAnimGraphNode_VisionStabilizer::GetControllerDescription() const
{
    return LOCTEXT("Description", "Vision Stabilizer");
}

FText UAnimGraphNode_VisionStabilizer::GetTooltipText() const
{
    return LOCTEXT("Tooltip",
        "Stabilizes a persistent positional POV target on a dedicated virtual bone without changing the real head. "
        "Translation filter only; the target keeps the POV rotation and scale captured at initialization. "
        "A larger clamp absorbs more bob. Zero clamp follows the head. "
        "Changing clamp mode intentionally disconnects the inactive input and its property binding.");
}

FString UAnimGraphNode_VisionStabilizer::GetNodeCategory() const
{
    return TEXT("Skeletal Controls");
}

bool UAnimGraphNode_VisionStabilizer::IsModePin(FName Name) const
{
    return Name == GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp) ||
        Name == GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed);
}

bool UAnimGraphNode_VisionStabilizer::IsInactiveModePin(FName Name) const
{
    return (Name == GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp) &&
            Node.ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve) ||
        (Name == GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed) &&
            Node.ClampMode != EVisionStabilizerClampMode::GroundSpeedCurve);
}

void UAnimGraphNode_VisionStabilizer::ConfigureModePins(bool bForceActivePinVisible)
{
    const FName ModeProperties[] = {
        GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp),
        GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed)
    };
    for (const FName PropertyName : ModeProperties)
    {
        FOptionalPinFromProperty* Record = ShowPinForProperties.FindByPredicate(
            [PropertyName](const FOptionalPinFromProperty& Candidate)
            {
                return Candidate.PropertyName == PropertyName;
            });
        const bool bNewRecord = Record == nullptr;
        if (bNewRecord)
        {
            Record = &ShowPinForProperties.AddDefaulted_GetRef();
            Record->PropertyName = PropertyName;
        }
        const bool bActive = !IsInactiveModePin(PropertyName);
        // Re-expose a pin when its mode becomes active, but preserve a user's
        // hide/show choice during reconstruction while the mode stays the same.
        const bool bWasInactive = !Record->bCanToggleVisibility;
        Record->bCanToggleVisibility = bActive;
        if (!bActive || bNewRecord || bForceActivePinVisible || (bActive && bWasInactive))
        {
            Record->bShowPin = bActive;
        }
    }
}

void UAnimGraphNode_VisionStabilizer::DisconnectInactiveModePins(
    const TArray<UEdGraphPin*>& CandidatePins)
{
    // Copy the list; link-change notifications must not invalidate iteration.
    const TArray<UEdGraphPin*> PinsToVisit = CandidatePins;
    for (UEdGraphPin* Pin : PinsToVisit)
    {
        if (!Pin || Pin->Direction != EGPD_Input || !IsInactiveModePin(Pin->PinName))
        {
            continue;
        }
        // An unlinked literal is meaningful and remains the manual fallback.
        // A connected runtime value is NOT sampled or baked during a mode switch.
        if (Pin->LinkedTo.IsEmpty())
        {
            CopyPinDefaultsToNodeData(Pin);
        }
        else
        {
            Modify();
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (LinkedPin && LinkedPin->GetOwningNode())
                {
                    LinkedPin->GetOwningNode()->Modify();
                }
            }
            Pin->BreakAllPinLinks(true);
        }
        // A hidden orphan must not resurrect the old wire on a later mode switch.
        Pin->SetSavePinIfOrphaned(false);
        Pin->bHidden = true;
        Pin->bNotConnectable = true;
    }

    // Hidden Property Access bindings are wiring too, even with no linked pin.
    const FName InactiveName = Node.ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve
        ? GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp)
        : GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed);
    if (HasBinding(InactiveName))
    {
        Modify();
        RemoveBindings(InactiveName);
    }
}

void UAnimGraphNode_VisionStabilizer::AllocateDefaultPins()
{
    // Seed records before allocation, then restore mode flags after the base
    // optional-pin manager has rebuilt its property list.
    ConfigureModePins(false);
    Super::AllocateDefaultPins();
    ConfigureModePins(false);
}

void UAnimGraphNode_VisionStabilizer::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
    // Break inactive links before old-to-new pin migration can restore them.
    DisconnectInactiveModePins(OldPins);
    ConfigureModePins(false);
    Super::ReallocatePinsDuringReconstruction(OldPins);
    ConfigureModePins(false);
}

void UAnimGraphNode_VisionStabilizer::CustomizePinData(
    UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const
{
    Super::CustomizePinData(Pin, SourcePropertyName, ArrayIndex);
    if (Pin && IsModePin(SourcePropertyName))
    {
        const bool bInactive = IsInactiveModePin(SourcePropertyName);
        Pin->bHidden = bInactive;
        Pin->bNotConnectable = bInactive;
        if (bInactive)
        {
            Pin->SetSavePinIfOrphaned(false);
        }
    }
}

bool UAnimGraphNode_VisionStabilizer::IsPinBindable(const UEdGraphPin* InPin) const
{
    return InPin && !IsInactiveModePin(InPin->PinName) && Super::IsPinBindable(InPin);
}

void UAnimGraphNode_VisionStabilizer::CopyPinDefaultsToNodeData(UEdGraphPin* InPin)
{
    Super::CopyPinDefaultsToNodeData(InPin);
    if (!InPin || InPin->Direction != EGPD_Input || !IsModePin(InPin->PinName) ||
        !InPin->LinkedTo.IsEmpty() || InPin->DefaultValue.IsEmpty())
    {
        return;
    }
    // Persist a scalar pin literal explicitly. The base hook alone is not a
    // contract that a custom node's hidden fallback property is kept in sync.
    if (FFloatProperty* Property = FindFProperty<FFloatProperty>(
        FAnimNode_VisionStabilizer::StaticStruct(), InPin->PinName))
    {
        Property->ImportText_Direct(*InPin->DefaultValue,
            Property->ContainerPtrToValuePtr<void>(&Node), this, PPF_None);
    }
}

void UAnimGraphNode_VisionStabilizer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    const bool bModeChanged = PropertyChangedEvent.GetPropertyName() ==
        GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, ClampMode);
    if (bModeChanged)
    {
        Modify();
        DisconnectInactiveModePins(Pins);
        ConfigureModePins(true);
    }
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (bModeChanged)
    {
        ReconstructNode();
        if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this))
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
    }
}

void UAnimGraphNode_VisionStabilizer::PostEditUndo()
{
    ConfigureModePins(false);
    Super::PostEditUndo();
    ReconstructNode();
}

void UAnimGraphNode_VisionStabilizer::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    Super::CustomizeDetails(DetailBuilder);
    const TSharedRef<IPropertyHandle> NodeProperty = DetailBuilder.GetProperty(
        GET_MEMBER_NAME_CHECKED(UAnimGraphNode_VisionStabilizer, Node), GetClass());
    const auto Hide = [&DetailBuilder, &NodeProperty](FName PropertyName)
    {
        const TSharedPtr<IPropertyHandle> Property = NodeProperty->GetChildHandle(PropertyName);
        if (Property.IsValid() && Property->IsValidHandle())
        {
            DetailBuilder.HideProperty(Property);
        }
    };

    if (Node.ClampMode == EVisionStabilizerClampMode::Manual)
    {
        Hide(GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed));
        Hide(GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClampCurve));
        Hide(GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, MinGroundSpeed));
        Hide(GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, MaxGroundSpeed));
    }
    // A populated curve hides the manual value in Details, but the serialized
    // literal still supplies the runtime fallback for a non-finite sample.
    else if (Node.PovClampCurve && Node.PovClampCurve->FloatCurve.GetNumKeys() > 0)
    {
        Hide(GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp));
    }
    else
    {
        const TSharedPtr<IPropertyHandle> Fallback = NodeProperty->GetChildHandle(
            GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp));
        if (Fallback.IsValid())
        {
            if (IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(Fallback))
            {
                Row->DisplayName(LOCTEXT("Fallback", "POV Clamp (Manual Fallback)"));
            }
        }
    }
}

void UAnimGraphNode_VisionStabilizer::ValidateAnimNodeDuringCompilation(
    USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
    Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

    if (Node.HeadBone.BoneName.IsNone())
    {
        MessageLog.Error(TEXT("@@: Head Bone is required."), this);
    }
    if (Node.PovHeadBone.BoneName.IsNone())
    {
        MessageLog.Error(TEXT("@@: POV Head Bone is required; select a dedicated virtual bone."), this);
    }
    if (!Node.HeadBone.BoneName.IsNone() && Node.HeadBone.BoneName == Node.PovHeadBone.BoneName)
    {
        MessageLog.Error(TEXT("@@: Head Bone and POV Head Bone must be different; the real head must never be the output."), this);
    }

    // Validate against the skeleton here; runtime repeats the safety checks
    // against required bones because LOD stripping can change availability.
    if (ForSkeleton)
    {
        const FReferenceSkeleton& Reference = ForSkeleton->GetReferenceSkeleton();
        const int32 HeadIndex = Reference.FindBoneIndex(Node.HeadBone.BoneName);
        const int32 PovIndex = Reference.FindBoneIndex(Node.PovHeadBone.BoneName);
        if (!Node.HeadBone.BoneName.IsNone() && HeadIndex == INDEX_NONE)
        {
            MessageLog.Error(TEXT("@@: Head Bone does not exist in the Animation Blueprint skeleton."), this);
        }
        if (!Node.PovHeadBone.BoneName.IsNone() && PovIndex == INDEX_NONE)
        {
            MessageLog.Error(TEXT("@@: POV Head Bone does not exist in the Animation Blueprint skeleton."), this);
        }

        const auto FindVirtual = [ForSkeleton](FName Name) -> const FVirtualBone*
        {
            return ForSkeleton->GetVirtualBones().FindByPredicate(
                [Name](const FVirtualBone& Bone) { return Bone.VirtualBoneName == Name; });
        };
        if (FindVirtual(Node.HeadBone.BoneName))
        {
            MessageLog.Error(TEXT("@@: Head Bone must be a real animated bone, not a virtual bone."), this);
        }
        const FVirtualBone* PovVirtual = FindVirtual(Node.PovHeadBone.BoneName);
        if (PovIndex != INDEX_NONE && !PovVirtual)
        {
            MessageLog.Error(TEXT("@@: POV Head Bone must be a virtual bone. Ordinary/skinned bone outputs are forbidden."), this);
        }
        if (PovVirtual && HeadIndex != INDEX_NONE && PovVirtual->TargetBoneName != Node.HeadBone.BoneName)
        {
            MessageLog.Warning(TEXT("@@: The POV virtual bone does not target Head Bone. The recommended setup is root -> head."), this);
        }
        if (PovIndex != INDEX_NONE)
        {
            for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
            {
                if (Reference.GetParentIndex(Index) == PovIndex)
                {
                    MessageLog.Error(TEXT("@@: POV Head Bone must be a dedicated leaf; it currently has child bones."), this);
                    break;
                }
            }
        }
    }
    else
    {
        MessageLog.Warning(TEXT("@@: No skeleton is available for bone-existence validation. Validate the concrete Animation Blueprint before shipping."), this);
    }

    if (Node.ClampMode != EVisionStabilizerClampMode::Manual &&
        Node.ClampMode != EVisionStabilizerClampMode::GroundSpeedCurve)
    {
        MessageLog.Error(TEXT("@@: Invalid clamp mode."), this);
    }

    if (Node.ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve)
    {
        if (!Node.PovClampCurve)
        {
            MessageLog.Warning(TEXT("@@: Ground-Speed Curve mode has no CurveFloat. The stored Manual POV Clamp literal will be used safely; its inactive pin/binding is disconnected."), this);
        }
        else if (Node.PovClampCurve->FloatCurve.GetNumKeys() == 0)
        {
            MessageLog.Warning(TEXT("@@: POV Clamp Curve has no keys. The stored Manual POV Clamp literal will be used."), this);
        }
        if (!FMath::IsFinite(Node.MinGroundSpeed) || !FMath::IsFinite(Node.MaxGroundSpeed) ||
            Node.MinGroundSpeed < 0.0f || Node.MaxGroundSpeed < 0.0f ||
            Node.MinGroundSpeed >= Node.MaxGroundSpeed)
        {
            MessageLog.Warning(TEXT("@@: Ground-speed bounds are non-finite, negative, reversed or equal. Runtime sanitizes/sorts them; a collapsed range evaluates curve X=0."), this);
        }
    }
    if (!FMath::IsFinite(Node.PovClamp) || Node.PovClamp < 0.0f)
    {
        MessageLog.Warning(TEXT("@@: POV Clamp is invalid. Runtime clamps negative values to zero and replaces non-finite values with 5 cm."), this);
    }
    if (!FMath::IsFinite(Node.PovHeightOffset))
    {
        MessageLog.Warning(TEXT("@@: POV Height Offset is non-finite. Runtime will use zero."), this);
    }

    for (const UEdGraphPin* Pin : Pins)
    {
        if (Pin && IsInactiveModePin(Pin->PinName) && !Pin->LinkedTo.IsEmpty())
        {
            MessageLog.Error(TEXT("@@: An inactive clamp-mode pin is still connected. Reconstruct this node before compiling."), this);
            break;
        }
    }
    const FName InactiveName = Node.ClampMode == EVisionStabilizerClampMode::GroundSpeedCurve
        ? GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, PovClamp)
        : GET_MEMBER_NAME_CHECKED(FAnimNode_VisionStabilizer, GroundSpeed);
    if (HasBinding(InactiveName))
    {
        MessageLog.Error(TEXT("@@: An inactive clamp-mode property is still bound. Reconstruct this node before compiling."), this);
    }
}

#undef LOCTEXT_NAMESPACE
