#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VisionStabilizerTestFixture.h"
#include "AnimNodes/AnimNode_VisionStabilizer.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BoneContainer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/MemStack.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FPoseTestProxy : FAnimInstanceProxy
{
    explicit FPoseTestProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        InitializeObjects(Instance);
    }

    void AdvanceUpdate() { UpdateCounter.Increment(); }
};

struct FPoseTestNode : FAnimNode_VisionStabilizer
{
    using FAnimNode_VisionStabilizer::EvaluateSkeletalControl_AnyThread;
    using FAnimNode_VisionStabilizer::InitializeBoneReferences;
    using FAnimNode_VisionStabilizer::IsValidToEvaluate;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionStabilizerPoseTest,
    "VisionStabilizer.Runtime.PoseContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionStabilizerPoseTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FMemMark Mark(FMemStack::Get());
    FVisionStabilizerTestRig Rig;
    if (!TestTrue(TEXT("Synthetic skeleton and virtual bone created"), Rig.bValid)) { return false; }

    // Cache a real skeleton through the supported proxy initialization path.
    // The component stays unregistered; no geometry or test world is needed.
    TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>());
    Component->SetSkeletalMeshAsset(Rig.Mesh.Get());
    TStrongObjectPtr<UAnimInstance> Instance(NewObject<UAnimInstance>(Component.Get()));
    FPoseTestProxy Proxy(Instance.Get());
    TArray<FBoneIndexType> RequiredIndices;
    for (int32 Index = 0; Index < Rig.Skeleton->GetReferenceSkeleton().GetNum(); ++Index)
    {
        RequiredIndices.Add(static_cast<FBoneIndexType>(Index));
    }
    FBoneContainer& Bones = Proxy.GetRequiredBones();
    const UE::Anim::FCurveFilterSettings CurveSettings(UE::Anim::ECurveFilterMode::None, nullptr, 0);
    Bones.InitializeTo(MakeArrayView(RequiredIndices), CurveSettings, *Rig.Skeleton.Get());
    FPoseTestNode Node;
    Node.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
    Node.InitializeBoneReferences(Bones);
    const FAnimationUpdateContext UpdateContext(&Proxy, 1.0f / 60.0f);
    // Advance the traversal counter with every simulated update so the node's
    // gap detection follows the same sequence as a running animation graph.
    const auto UpdateNode = [&]()
    {
        Proxy.AdvanceUpdate();
        Node.Update_AnyThread(UpdateContext);
    };
    if (!TestTrue(TEXT("Valid head and leaf virtual POV accepted"), Node.IsValidToEvaluate(Rig.Skeleton.Get(), Bones)))
    {
        return false;
    }
    const FCompactPoseBoneIndex HeadIndex = Node.HeadBone.GetCompactPoseIndex(Bones);
    const FCompactPoseBoneIndex PovIndex = Node.PovHeadBone.GetCompactPoseIndex(Bones);
    FComponentSpacePoseContext Context(&Proxy);
    Context.ResetToRefPose();
    const FQuat InitialRotation = FRotator(10, 20, 30).Quaternion();
    const FVector InitialScale(1.1, 1.2, 1.3);
    const FVector Start(0, 0, 160);
    const FTransform InitialPov(InitialRotation, Start, InitialScale);
    Context.Pose.SetComponentSpaceTransform(HeadIndex, FTransform(Start));
    Context.Pose.SetComponentSpaceTransform(PovIndex, InitialPov);
    TArray<FBoneTransform> Output;
    UpdateNode();
    Node.EvaluateSkeletalControl_AnyThread(Context, Output);
    if (!TestEqual(TEXT("Exactly one output"), Output.Num(), 1)) { return false; }
    TestTrue(TEXT("Only POV is written"), Output[0].BoneIndex == PovIndex);

    // New incoming animation must not overwrite the persisted seed or captured R/S.
    Context.ResetToRefPose();
    const FTransform AnimatedHead(FRotator(15, 40, 5).Quaternion(), Start + FVector(0, 0, 2));
    const FTransform AnimatedPov(FRotator(-20, 50, 0).Quaternion(), Start + FVector(0, 0, 2), FVector(2));
    Context.Pose.SetComponentSpaceTransform(HeadIndex, AnimatedHead);
    Context.Pose.SetComponentSpaceTransform(PovIndex, AnimatedPov);
    Node.PovHeightOffset = 3.0f;
    Output.Reset();
    UpdateNode();
    Node.EvaluateSkeletalControl_AnyThread(Context, Output);
    if (!TestEqual(TEXT("Exactly one output on subsequent evaluation"), Output.Num(), 1)) { return false; }
    TestTrue(TEXT("Interior bob absorbed, immediate height offset"), Output[0].Transform.GetTranslation().Equals(Start + FVector(0, 0, 3)));
    TestTrue(TEXT("Captured POV rotation retained"), Output[0].Transform.GetRotation().Equals(InitialRotation));
    TestTrue(TEXT("Captured POV scale retained"), Output[0].Transform.GetScale3D().Equals(InitialScale));
    TestTrue(TEXT("Real head untouched during evaluation"), Context.Pose.GetComponentSpaceTransform(HeadIndex).Equals(AnimatedHead));
    Context.Pose.LocalBlendCSBoneTransforms(Output, 1.0f);
    TestTrue(TEXT("Real head untouched after applying output"), Context.Pose.GetComponentSpaceTransform(HeadIndex).Equals(AnimatedHead));

    // A native dynamic reset recaptures R/S from the next actual incoming pose.
    Node.ResetDynamics(ETeleportType::TeleportPhysics);
    Context.ResetToRefPose();
    Context.Pose.SetComponentSpaceTransform(HeadIndex, AnimatedHead);
    Context.Pose.SetComponentSpaceTransform(PovIndex, AnimatedPov);
    Output.Reset();
    UpdateNode();
    Node.EvaluateSkeletalControl_AnyThread(Context, Output);
    if (!TestEqual(TEXT("Output after reset"), Output.Num(), 1)) { return false; }
    TestTrue(TEXT("Reset recaptures rotation"), Output[0].Transform.GetRotation().Equals(AnimatedPov.GetRotation()));
    TestTrue(TEXT("Reset recaptures scale"), Output[0].Transform.GetScale3D().Equals(AnimatedPov.GetScale3D()));

    // Exercise the final base update path, including its conditional hook.
    // Every resumed pose is inside the clamp, so only a reset can follow it.
    Node.PovHeightOffset = 0.0f;
    const auto ExpectRebasedPose = [&](const TCHAR* Label, double Height)
    {
        const FTransform Incoming(FRotator(Height, 0, 0).Quaternion(), Start + FVector(0, 0, Height));
        Context.ResetToRefPose();
        Context.Pose.SetComponentSpaceTransform(HeadIndex, Incoming);
        Context.Pose.SetComponentSpaceTransform(PovIndex, Incoming);
        UpdateNode();
        Output.Reset();
        Node.EvaluateSkeletalControl_AnyThread(Context, Output);
        TestTrue(Label, Output.Num() == 1 && Output[0].Transform.Equals(Incoming));
    };

    Node.bResetStabilization = true;
    ExpectRebasedPose(TEXT("Reset pin rebases through the base update hook"), 3.0);
    Node.bResetStabilization = false;

    Node.Alpha = 0.0f;
    UpdateNode();
    TestEqual(TEXT("Base update disables zero alpha"), Node.ActualAlpha, 0.0f);
    Node.Alpha = 1.0f;
    ExpectRebasedPose(TEXT("Alpha reactivation recaptures the incoming pose"), 4.0);

    // The geometry-free proxy has LOD 0; a threshold below it disables the hook.
    Node.LODThreshold = -2;
    UpdateNode();
    TestEqual(TEXT("Base update disables control beyond its LOD threshold"), Node.ActualAlpha, 0.0f);
    Node.LODThreshold = INDEX_NONE;
    ExpectRebasedPose(TEXT("LOD reactivation recaptures the incoming pose"), 5.0);

    Proxy.AdvanceUpdate(); // A frame where this graph branch is not traversed.
    ExpectRebasedPose(TEXT("Graph re-entry recaptures the incoming pose"), 6.0);

    Node.PovHeadBone.BoneName = TEXT("head");
    Node.InitializeBoneReferences(Bones);
    TestFalse(TEXT("Duplicate/head output rejected at runtime"), Node.IsValidToEvaluate(Rig.Skeleton.Get(), Bones));
    Node.PovHeadBone.BoneName = TEXT("spine");
    Node.InitializeBoneReferences(Bones);
    TestFalse(TEXT("Real bone output rejected at runtime"), Node.IsValidToEvaluate(Rig.Skeleton.Get(), Bones));
    return true;
}

#endif
