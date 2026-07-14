// Copyright (c) RAMMP. All rights reserved.

#include "RammsSeatedPoseAnimInstance.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "RammsCrowdLog.h"
#include "RammsSeatComponent.h"

FAnimInstanceProxy* URammsSeatedPoseAnimInstance::CreateAnimInstanceProxy()
{
	return new FRammsSeatedPoseProxy(this);
}

void URammsSeatedPoseAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	USkeletalMeshComponent* Comp = GetSkelMeshComponent();

	// Self-defense: the mesh must refresh bones even while unrendered. Assembled
	// characters hide the leader body mesh under clothing, so it may NEVER count
	// as rendered — with the default tick option its bones freeze at ref pose and
	// the visible follower meshes copy that freeze, regardless of what Evaluate
	// produces. Late assembly passes can reset this, so reassert it here (this
	// runs on every anim-instance recreation).
	if (Comp != nullptr)
	{
		Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		// City Sample base meshes carry a post-process AnimBP (m_/f_*_animbp)
		// designed for CitySample's own crowd animation system. With that system
		// absent it ignores the input pose and outputs reference pose — evaluated
		// AFTER this instance, it silently discards the seated pose. It must be
		// disabled through the setter (a raw property write skips the anim
		// re-initialization needed for it to take effect).
		Comp->SetDisablePostProcessBlueprint(true);
	}

	if (BoneOffsets.Num() > 0)
	{
		return;
	}
	const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	for (const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr; Parent != nullptr; Parent = Parent->GetAttachParent())
	{
		if (const URammsSeatComponent* Seat = Cast<URammsSeatComponent>(Parent))
		{
			BoneOffsets = Seat->PoseBoneOffsets;
			UE_LOG(LogRammsCrowd, Verbose, TEXT("[SeatedPose] re-acquired %d offsets from seat '%s' (mesh '%s')"), BoneOffsets.Num(), *Seat->GetPathName(), *GetNameSafe(Comp));
			return;
		}
	}
	UE_LOG(LogRammsCrowd, Warning, TEXT("[SeatedPose] NO seat found in attach chain of '%s' (owner '%s') — offsets stay empty"),
		*GetNameSafe(Comp), Owner ? *Owner->GetPathName() : TEXT("<null>"));
}

void FRammsSeatedPoseProxy::PreUpdate(UAnimInstance* InAnimInstance, const float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	Offsets.Reset();
	if (const URammsSeatedPoseAnimInstance* Instance = Cast<URammsSeatedPoseAnimInstance>(InAnimInstance))
	{
		Offsets.Reserve(Instance->BoneOffsets.Num());
		for (const TPair<FName, FRotator>& Pair : Instance->BoneOffsets)
		{
			Offsets.Emplace(Pair.Key, Pair.Value.Quaternion());
		}
	}
}

bool FRammsSeatedPoseProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const TPair<FName, FQuat>& Offset : Offsets)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Offset.Key);
		if (MeshIndex == INDEX_NONE)
		{
			continue;
		}
		const FCompactPoseBoneIndex CompactIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (CompactIndex == INDEX_NONE)
		{
			continue;
		}
		FTransform& BoneTransform = Output.Pose[CompactIndex];
		BoneTransform.SetRotation(BoneTransform.GetRotation() * Offset.Value);
	}
	return true;
}
