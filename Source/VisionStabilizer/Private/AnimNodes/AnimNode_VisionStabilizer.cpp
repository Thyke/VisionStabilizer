#include "AnimNodes/AnimNode_VisionStabilizer.h"

#include "Animation/AnimInstanceProxy.h"
#include "BoneContainer.h"
#include "Curves/CurveFloat.h"
#include "SceneTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
VisionStabilizerCore::FPoint ToPoint(const FVector& Vector)
{
    return {Vector.X, Vector.Y, Vector.Z};
}

FVector ToVector(const VisionStabilizerCore::FPoint& Point)
{
    return FVector(Point.X, Point.Y, Point.Z);
}

bool IsUsableTransform(const FTransform& Transform)
{
    return !Transform.ContainsNaN() && Transform.GetRotation().IsNormalized();
}
}

FAnimNode_VisionStabilizer::FAnimNode_VisionStabilizer()
{
    HeadBone.BoneName = TEXT("head");
    PovHeadBone.BoneName = TEXT("VB pov_head");
}

void FAnimNode_VisionStabilizer::ResetState()
{
    PositionState.Reset();
    CapturedPovTransformCS = FTransform::Identity;
    LastCorrectionDistance = 0.0f;
}

void FAnimNode_VisionStabilizer::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
    ResetState();
    LastUpdateCounter.Reset();
    // Do not clear the curve snapshot here: engine PreUpdate may precede a
    // deferred worker-thread Initialize. Bone transforms are captured lazily.
}

void FAnimNode_VisionStabilizer::PreUpdate(const UAnimInstance* InAnimInstance)
{
    check(IsInGameThread());
    (void)InAnimInstance;

    // Keep UObject reads in this hook; evaluation samples only the owned FRichCurve.
    const UCurveFloat* Curve = PovClampCurve.Get();
    if (ClampMode != EVisionStabilizerClampMode::GroundSpeedCurve || !IsValid(Curve))
    {
        CachedCurveSource.Reset();
        bHasCurveSnapshot = false;
        return;
    }

    // Cooked builds refresh on asset replacement. In-place key edits are tracked
    // below only in the editor, where curve editing is part of the normal workflow.
    bool bRefresh = CachedCurveSource.Get() != Curve;
#if WITH_EDITOR
    // Live editor curve edits, including changes with unchanged key counts.
    // Equality is O(keys); copies/allocations occur only when data has changed.
    bRefresh = bRefresh || !(CurveSnapshot == Curve->FloatCurve);
#endif
    if (bRefresh)
    {
        CurveSnapshot = Curve->FloatCurve;
        CachedCurveSource = PovClampCurve.Get();
    }
    bHasCurveSnapshot = CurveSnapshot.GetNumKeys() > 0;
}

void FAnimNode_VisionStabilizer::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
    HeadBone.Initialize(RequiredBones);
    PovHeadBone.Initialize(RequiredBones);
    ResetState();
    bBonePairValid = false;

    if (HeadBone.BoneName == PovHeadBone.BoneName ||
        !HeadBone.IsValidToEvaluate(RequiredBones) || !PovHeadBone.IsValidToEvaluate(RequiredBones))
    {
        return;
    }

    const FCompactPoseBoneIndex HeadIndex = HeadBone.GetCompactPoseIndex(RequiredBones);
    const FCompactPoseBoneIndex PovIndex = PovHeadBone.GetCompactPoseIndex(RequiredBones);
    bool bPovIsVirtual = false;
    for (const FVirtualBoneCompactPoseData& VirtualBone : RequiredBones.GetVirtualBoneCompactPoseData())
    {
        if (VirtualBone.VBIndex == HeadIndex)
        {
            return; // Head must be a real bone, even in an invalid cooked graph.
        }
        bPovIsVirtual |= VirtualBone.VBIndex == PovIndex;
    }
    if (!bPovIsVirtual)
    {
        return; // Never overwrite an ordinary skinned bone as the POV target.
    }

    // A dedicated leaf prevents propagation to any real or virtual descendants.
    for (int32 Index = 0; Index < RequiredBones.GetCompactPoseNumBones(); ++Index)
    {
        if (RequiredBones.GetParentBoneIndex(FCompactPoseBoneIndex(Index)) == PovIndex)
        {
            return;
        }
    }
    bBonePairValid = true;
}

bool FAnimNode_VisionStabilizer::IsValidToEvaluate(
    const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
    (void)Skeleton;
    return bBonePairValid && HeadBone.BoneName != PovHeadBone.BoneName &&
        HeadBone.IsValidToEvaluate(RequiredBones) && PovHeadBone.IsValidToEvaluate(RequiredBones);
}

void FAnimNode_VisionStabilizer::UpdateInternal(const FAnimationUpdateContext& Context)
{
    // The final base Update_AnyThread has already updated the input pose,
    // executed exposed inputs and computed alpha before calling this hook.
    FAnimNode_SkeletalControlBase::UpdateInternal(Context);

    const FGraphTraversalCounter& CurrentCounter = Context.AnimInstanceProxy->GetUpdateCounter();
    if (LastUpdateCounter.HasEverBeenUpdated() &&
        !LastUpdateCounter.WasSynchronizedCounter(CurrentCounter))
    {
        // This hook is skipped when alpha, LOD or bone validation disables the
        // control, so those gaps also rebase the next active incoming pose.
        ResetState();
    }
    LastUpdateCounter.SynchronizeWith(CurrentCounter);

    ActualAlpha = FMath::IsFinite(ActualAlpha) ? FMath::Clamp(ActualAlpha, 0.0f, 1.0f) : 0.0f;
    if (bResetStabilization || !bBonePairValid || !IsLODEnabled(Context.AnimInstanceProxy) ||
        !FAnimWeight::IsRelevant(ActualAlpha))
    {
        ResetState();
    }
}

void FAnimNode_VisionStabilizer::ResetDynamics(ETeleportType InTeleportType)
{
    (void)InTeleportType;
    ResetState();
    LastUpdateCounter.Reset();
}

float FAnimNode_VisionStabilizer::ResolveClampRadius(
    float& OutNormalizedSpeed, bool& bOutUsedFallback) const
{
    const double ManualRadius = VisionStabilizerCore::SanitizeRadius(PovClamp);
    OutNormalizedSpeed = 0.0f;
    bOutUsedFallback = false;

    if (ClampMode != EVisionStabilizerClampMode::GroundSpeedCurve)
    {
        return static_cast<float>(ManualRadius);
    }

    const auto Speed = VisionStabilizerCore::NormalizeGroundSpeed(
        GroundSpeed, MinGroundSpeed, MaxGroundSpeed);
    OutNormalizedSpeed = static_cast<float>(Speed.Normalized);
    if (!bHasCurveSnapshot)
    {
        bOutUsedFallback = true;
        return static_cast<float>(ManualRadius);
    }

    const float Sample = CurveSnapshot.Eval(OutNormalizedSpeed, static_cast<float>(ManualRadius));
    if (!FMath::IsFinite(Sample))
    {
        bOutUsedFallback = true;
        return static_cast<float>(ManualRadius);
    }
    return FMath::Max(0.0f, Sample);
}

void FAnimNode_VisionStabilizer::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VisionStabilizer_Evaluate);
    check(OutBoneTransforms.Num() == 0);
    const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
    if (!IsValidToEvaluate(nullptr, RequiredBones))
    {
        ResetState();
        return;
    }

    const FCompactPoseBoneIndex HeadIndex = HeadBone.GetCompactPoseIndex(RequiredBones);
    const FCompactPoseBoneIndex PovIndex = PovHeadBone.GetCompactPoseIndex(RequiredBones);
    const FTransform& HeadTransformCS = Output.Pose.GetComponentSpaceTransform(HeadIndex);
    const FTransform& IncomingPovCS = Output.Pose.GetComponentSpaceTransform(PovIndex);
    if (!IsUsableTransform(HeadTransformCS) || !IsUsableTransform(IncomingPovCS))
    {
        ResetState();
        return; // Pass through invalid upstream input rather than creating more NaNs.
    }

    LastClampRadius = ResolveClampRadius(LastNormalizedSpeed, bLastUsedFallback);
    if (!PositionState.IsInitialized())
    {
        // Initialization has no evaluated pose. Capture the first valid INCOMING
        // component-space transform, not the reference pose and not a world pose.
        CapturedPovTransformCS = IncomingPovCS;
    }
    if (!PositionState.Step(ToPoint(IncomingPovCS.GetTranslation()),
        ToPoint(HeadTransformCS.GetTranslation()), LastClampRadius))
    {
        ResetState();
        return;
    }

    // Offset the output, not the persisted point. Otherwise the next projection
    // would absorb a height adjustment into the dead zone.
    const FVector HeightOffset(0.0, 0.0, VisionStabilizerCore::SanitizeHeight(PovHeightOffset));
    FTransform TargetCS = CapturedPovTransformCS;
    TargetCS.SetTranslation(ToVector(PositionState.GetPoint()) + HeightOffset);
    if (!IsUsableTransform(TargetCS))
    {
        ResetState();
        return;
    }

    LastCorrectionDistance = static_cast<float>(FVector::Dist(
        IncomingPovCS.GetTranslation() + HeightOffset, TargetCS.GetTranslation()));

    // Emit the full target without pre-blending. SkeletalControlBase applies alpha;
    // feeding that blend back into PositionState would change the filter's history.
    // The sole output is the virtual POV bone, so compact-pose sorting is implicit.
    OutBoneTransforms.Emplace(PovIndex, TargetCS);

#if ENABLE_ANIM_DEBUG && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    if (bEnableDebugDraw)
    {
        QueueDebugDraw(*Output.AnimInstanceProxy, HeadTransformCS.GetTranslation() + HeightOffset,
            TargetCS.GetTranslation(), IncomingPovCS.GetTranslation() + HeightOffset, LastClampRadius);
    }
#endif
}

void FAnimNode_VisionStabilizer::QueueDebugDraw(FAnimInstanceProxy& Proxy,
    const FVector& CenterCS, const FVector& TargetCS, const FVector& IncomingCS, float Radius) const
{
#if ENABLE_ANIM_DEBUG && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    const FTransform& ComponentToWorld = Proxy.GetComponentTransform();
    const FVector CenterWS = ComponentToWorld.TransformPosition(CenterCS);
    const FVector TargetWS = ComponentToWorld.TransformPosition(TargetCS);
    const FVector IncomingWS = ComponentToWorld.TransformPosition(IncomingCS);

    // Transform three component-space circles point-by-point. Unlike a world
    // sphere with an arbitrary scale factor, this depicts the actual CS clamp
    // envelope correctly even with non-uniform component scale.
    constexpr int32 Segments = 24;
    for (int32 Plane = 0; Plane < 3; ++Plane)
    {
        for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const auto MakeOffset = [Plane, Radius](double Angle)
            {
                const double A = Radius * FMath::Cos(Angle);
                const double B = Radius * FMath::Sin(Angle);
                return Plane == 0 ? FVector(A, B, 0.0) :
                    (Plane == 1 ? FVector(A, 0.0, B) : FVector(0.0, A, B));
            };
            const FVector A = ComponentToWorld.TransformPosition(
                CenterCS + MakeOffset(2.0 * PI * Segment / Segments));
            const FVector B = ComponentToWorld.TransformPosition(
                CenterCS + MakeOffset(2.0 * PI * (Segment + 1) / Segments));
            Proxy.AnimDrawDebugLine(A, B, FColor::Green, false, 0.0f, 0.6f, SDPG_Foreground);
        }
    }
    Proxy.AnimDrawDebugPoint(CenterWS, 5.0f, FColor::Green, false, 0.0f, SDPG_Foreground);
    Proxy.AnimDrawDebugPoint(IncomingWS, 5.0f, FColor::Yellow, false, 0.0f, SDPG_Foreground);
    Proxy.AnimDrawDebugPoint(TargetWS, 9.0f, FColor::Cyan, false, 0.0f, SDPG_Foreground);
    Proxy.AnimDrawDebugLine(IncomingWS, TargetWS, FColor::Cyan, false, 0.0f, 1.2f, SDPG_Foreground);
#else
    (void)Proxy; (void)CenterCS; (void)TargetCS; (void)IncomingCS; (void)Radius;
#endif
}

void FAnimNode_VisionStabilizer::GatherDebugData(FNodeDebugData& DebugData)
{
    FString Text = DebugData.GetNodeName(this);
    AddDebugNodeData(Text);
    Text += FString::Printf(TEXT(" Head=%s POV=%s Mode=%s Radius=%.2fcm Speed01=%.3f Correction=%.2fcm %s%s"),
        *HeadBone.BoneName.ToString(), *PovHeadBone.BoneName.ToString(),
        ClampMode == EVisionStabilizerClampMode::Manual ? TEXT("Manual") : TEXT("GroundSpeedCurve"),
        LastClampRadius, LastNormalizedSpeed, LastCorrectionDistance,
        PositionState.IsInitialized() ? TEXT("Initialized") : TEXT("Inactive/Uninitialized"),
        bLastUsedFallback ? TEXT(" [MANUAL FALLBACK]") : TEXT(""));
    DebugData.AddDebugItem(Text);
    ComponentPose.GatherDebugData(DebugData);
}
