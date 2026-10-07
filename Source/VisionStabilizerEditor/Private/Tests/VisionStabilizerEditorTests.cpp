#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VisionStabilizerTestFixture.h"
#include "AnimGraphNode_VisionStabilizer.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Knot.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionStabilizerBoneValidationTest,
    "VisionStabilizer.Editor.BoneValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionStabilizerBoneValidationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FVisionStabilizerTestRig Rig;
    if (!TestTrue(TEXT("Test skeleton created"), Rig.bValid)) { return false; }
    TStrongObjectPtr<UAnimGraphNode_VisionStabilizer> Node(NewObject<UAnimGraphNode_VisionStabilizer>());
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestEqual(TEXT("Valid bone setup has no errors"), Log.NumErrors, 0);
    }
    Node->Node.HeadBone.BoneName = NAME_None;
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestTrue(TEXT("Missing head fails compilation"), Log.NumErrors > 0);
    }
    Node->Node.HeadBone.BoneName = TEXT("head");
    Node->Node.PovHeadBone.BoneName = TEXT("head");
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestTrue(TEXT("Duplicate selection fails compilation"), Log.NumErrors > 0);
    }
    Node->Node.PovHeadBone.BoneName = TEXT("spine");
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestTrue(TEXT("Non-virtual output fails compilation"), Log.NumErrors > 0);
    }
    Node->Node.PovHeadBone.BoneName = TEXT("does_not_exist");
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestTrue(TEXT("Unknown output fails compilation"), Log.NumErrors > 0);
    }
    Node->Node.PovHeadBone.BoneName = TEXT("VB pov_head");
    Node->Node.ClampMode = EVisionStabilizerClampMode::GroundSpeedCurve;
    {
        FCompilerResultsLog Log; Log.bSilentMode = true;
        Node->ValidateAnimNodeDuringCompilation(Rig.Skeleton.Get(), Log);
        TestEqual(TEXT("Null curve is nonfatal"), Log.NumErrors, 0);
        TestTrue(TEXT("Null curve reports a compiler warning"), Log.NumWarnings > 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionStabilizerPinReconstructionTest,
    "VisionStabilizer.Editor.ModePinReconstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionStabilizerPinReconstructionTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FVisionStabilizerTestRig Rig;
    if (!TestTrue(TEXT("Test skeleton created"), Rig.bValid)) { return false; }
    TStrongObjectPtr<UAnimBlueprint> Blueprint(NewObject<UAnimBlueprint>());
    Blueprint->TargetSkeleton = Rig.Skeleton.Get();
    UEdGraph* Graph = NewObject<UEdGraph>(Blueprint.Get());
    Graph->Schema = UAnimationGraphSchema::StaticClass();
    Blueprint->FunctionGraphs.Add(Graph);
    UAnimGraphNode_VisionStabilizer* Node = NewObject<UAnimGraphNode_VisionStabilizer>(Graph);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->AllocateDefaultPins();
    UEdGraphPin* ClampPin = Node->FindPin(TEXT("PovClamp"));
    if (!TestNotNull(TEXT("Manual clamp pin is present"), ClampPin)) { return false; }
    TestFalse(TEXT("Manual clamp pin is visible"), ClampPin->bHidden);
    // Store the literal before connecting a wire; a mode switch must retain
    // this fallback without sampling the connected value.
    ClampPin->DefaultValue = TEXT("7.0");
    Node->CopyPinDefaultsToNodeData(ClampPin);

    UK2Node_Knot* Source = NewObject<UK2Node_Knot>(Graph);
    Graph->AddNode(Source, false, false);
    Source->CreateNewGuid();
    Source->AllocateDefaultPins();
    UEdGraphPin* SourcePin = Source->GetOutputPin();
    SourcePin->PinType = ClampPin->PinType;
    SourcePin->MakeLinkTo(ClampPin);
    TestEqual(TEXT("Test wire connected"), SourcePin->LinkedTo.Num(), 1);

    Node->Node.ClampMode = EVisionStabilizerClampMode::GroundSpeedCurve;
    Node->ReconstructNode();
    TestEqual(TEXT("Switch disconnects old wire"), SourcePin->LinkedTo.Num(), 0);
    ClampPin = Node->FindPin(TEXT("PovClamp"));
    TestTrue(TEXT("Inactive clamp pin absent or hidden"), ClampPin == nullptr || ClampPin->bHidden);
    TestEqual(TEXT("Manual fallback literal retained"), Node->Node.PovClamp, 7.0f);
    UEdGraphPin* SpeedPin = Node->FindPin(TEXT("GroundSpeed"));
    TestTrue(TEXT("Curve mode exposes Ground Speed"), SpeedPin && !SpeedPin->bHidden);

    Node->Node.ClampMode = EVisionStabilizerClampMode::Manual;
    Node->ReconstructNode();
    TestEqual(TEXT("Returning modes does not resurrect wire"), SourcePin->LinkedTo.Num(), 0);
    ClampPin = Node->FindPin(TEXT("PovClamp"));
    TestTrue(TEXT("Returning to Manual exposes its pin"), ClampPin && !ClampPin->bHidden);
    return true;
}

#endif
