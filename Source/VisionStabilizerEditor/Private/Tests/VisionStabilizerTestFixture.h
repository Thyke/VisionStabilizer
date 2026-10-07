#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

/**
 * @brief Geometry-free root/spine/head rig with a root-to-head virtual POV bone.
 * @note Strong references keep both transient assets alive throughout a test.
 */
struct FVisionStabilizerTestRig
{
    TStrongObjectPtr<USkeleton> Skeleton{NewObject<USkeleton>()}; ///< Transient skeleton with the virtual bone.
    TStrongObjectPtr<USkeletalMesh> Mesh{NewObject<USkeletalMesh>()}; ///< Reference bones only; no render geometry.
    bool bValid = false; ///< True when bone-tree merge and virtual-bone creation both succeed.

    /** @brief Builds the reference hierarchy and creates VB pov_head for node validation. */
    FVisionStabilizerTestRig()
    {
        Mesh->SetSkeleton(Skeleton.Get());
        {
            FReferenceSkeletonModifier Modifier(Mesh->GetRefSkeleton(), Skeleton.Get());
            Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
            Modifier.Add(FMeshBoneInfo(TEXT("spine"), TEXT("spine"), 0), FTransform(FVector(0, 0, 100)));
            Modifier.Add(FMeshBoneInfo(TEXT("head"), TEXT("head"), 1), FTransform(FVector(0, 0, 60)));
        }
        bValid = Skeleton->MergeAllBonesToBoneTree(Mesh.Get(), false) &&
            Skeleton->AddNewNamedVirtualBone(TEXT("root"), TEXT("head"), TEXT("VB pov_head"));
    }
};
#endif
