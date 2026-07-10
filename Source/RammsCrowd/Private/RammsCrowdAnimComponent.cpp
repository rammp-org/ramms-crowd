// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/LODSyncComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MassLODFragments.h"
#include "MassActorSubsystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "RammsCrowdAgentTrait.h"

URammsCrowdAnimComponent::URammsCrowdAnimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Run before animation evaluation so the AnimBP reads this frame's values.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

FMassEntityHandle URammsCrowdAnimComponent::ResolveEntityHandle()
{
	UMassActorSubsystem* ActorSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassActorSubsystem>() : nullptr;
	if (ActorSubsystem == nullptr || GetOwner() == nullptr)
	{
		return FMassEntityHandle();
	}
	// Representation actors are pooled: re-resolve every tick rather than caching across frames.
	return ActorSubsystem->GetEntityHandleFromActor(GetOwner());
}

void URammsCrowdAnimComponent::SetLookAt(const FVector& InDirection, const bool bInHasTarget)
{
	LookAtDirection = InDirection;
	bHasLookAtTarget = bInHasTarget;
}

void URammsCrowdAnimComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	CachedHandle = ResolveEntityHandle();

	const UMassEntitySubsystem* EntitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (EntitySubsystem == nullptr || !CachedHandle.IsValid())
	{
		MassVelocity = FVector::ZeroVector;
		MassSpeed = 0.0f;
		bMassIsMoving = false;
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	if (!EntityManager.IsEntityValid(CachedHandle))
	{
		MassVelocity = FVector::ZeroVector;
		MassSpeed = 0.0f;
		bMassIsMoving = false;
		return;
	}

	if (const FMassVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassVelocityFragment>(CachedHandle))
	{
		MassVelocity = Velocity->Value;
		MassSpeed = static_cast<float>(Velocity->Value.Size2D());
	}
	if (const FMassMoveTargetFragment* MoveTarget = EntityManager.GetFragmentDataPtr<FMassMoveTargetFragment>(CachedHandle))
	{
		bMassIsMoving = MoveTarget->GetCurrentAction() == EMassMovementAction::Move;
	}
	if (const FRammsCrowdProfileParameters* Profile = EntityManager.GetConstSharedFragmentDataPtr<FRammsCrowdProfileParameters>(CachedHandle))
	{
		GaitVariation = Profile->GaitVariation;
	}

	if (bDriveSingleNodeAnimation)
	{
		UpdateDrivenAnimation();
	}

	if (bManageLODSyncByDistance)
	{
		if (const FMassViewerInfoFragment* ViewerInfo = EntityManager.GetFragmentDataPtr<FMassViewerInfoFragment>(CachedHandle))
		{
			ManageLODSync(static_cast<float>(FMath::Sqrt(ViewerInfo->ClosestViewerDistanceSq)));
		}
	}
}

void URammsCrowdAnimComponent::ManageLODSync(const float ViewerDistanceCm)
{
	ULODSyncComponent* Sync = LODSync.Get();
	if (Sync == nullptr)
	{
		Sync = GetOwner() ? GetOwner()->FindComponentByClass<ULODSyncComponent>() : nullptr;
		LODSync = Sync;
	}
	if (Sync == nullptr)
	{
		return;
	}

	int32 Forced = INDEX_NONE;
	if (ViewerDistanceCm < ForceLOD0DistanceCm)
	{
		Forced = 0;
	}
	else if (ViewerDistanceCm < ForceLOD1DistanceCm)
	{
		Forced = 1;
	}
	else if (ViewerDistanceCm < ForceLOD2DistanceCm)
	{
		Forced = 2;
	}

	if (Sync->ForcedLOD != Forced)
	{
		Sync->ForcedLOD = Forced;
	}
}

void URammsCrowdAnimComponent::UpdateDrivenAnimation()
{
	USkeletalMeshComponent* Mesh = DrivenMesh.Get();
	if (Mesh == nullptr)
	{
		if (const AActor* Owner = GetOwner())
		{
			// Prefer the root mesh: on assembled characters (City Sample) it is the
			// leader-pose component every clothing/body part follows.
			Mesh = Cast<USkeletalMeshComponent>(Owner->GetRootComponent());
			if (Mesh == nullptr)
			{
				Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
			}
		}
		DrivenMesh = Mesh;
	}
	// An actual Animation Blueprint takes precedence over this fallback driver — but
	// AnimationBlueprint mode with a NULL class (e.g. the City Sample crowd pack,
	// whose anim instance lived in game code not shipped with it) animates nothing,
	// so drive that too.
	if (Mesh == nullptr
		|| (Mesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && Mesh->GetAnimClass() != nullptr))
	{
		return;
	}

	// Deterministic per-entity gait jitter; re-seeded when this pooled actor is
	// recycled onto a different entity. Also desyncs the animation phase below.
	bool bReseeded = false;
	if (CachedHandle.Index != SeededEntityIndex)
	{
		SeededEntityIndex = CachedHandle.Index;
		const uint32 Hash = static_cast<uint32>(CachedHandle.Index) * 2654435761u;
		const float Random01 = static_cast<float>(Hash & 0x3FF) / 1023.0f;
		GaitRateScale = 1.0f + (Random01 - 0.5f) * GaitVariation;
		bReseeded = true;
	}

	// Trust measured speed, not movement intent: entities keep sliding while
	// decelerating into Stand (intent already switched) and feet must follow.
	// Direction of travel relative to the character's visual facing. The mesh is
	// authored facing +Y (MetaHuman/mannequin convention), so its world facing is the
	// component's Y axis; falls back to actor forward for X-facing meshes.
	if (MassSpeed > UE_KINDA_SMALL_NUMBER)
	{
		const FVector Facing = Mesh == GetOwner()->GetRootComponent()
			? Mesh->GetRightVector()
			: GetOwner()->GetActorForwardVector();
		const FVector VelocityDir = MassVelocity.GetSafeNormal2D();
		const float Dot = static_cast<float>(FVector::DotProduct(Facing.GetSafeNormal2D(), VelocityDir));
		const float Sign = FMath::Sign(static_cast<float>(FVector::CrossProduct(Facing, VelocityDir).Z));
		DirectionAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f))) * Sign;
	}
	else
	{
		DirectionAngleDegrees = 0.0f;
	}

	// Blend-space path: play once, feed (direction, speed) every tick.
	if (LocomotionBlendSpace != nullptr)
	{
#if WITH_EDITOR
		// Script-authored blend spaces load with an empty runtime triangulation in
		// the editor (they evaluate to reference pose with frozen time). Rebuild it
		// once per session before first playback; cooked builds bake this at cook.
		{
			static TSet<FObjectKey> RebuiltBlendSpaces;
			const FObjectKey Key(LocomotionBlendSpace);
			if (!RebuiltBlendSpaces.Contains(Key))
			{
				RebuiltBlendSpaces.Add(Key);
				LocomotionBlendSpace->ValidateSampleData();
				LocomotionBlendSpace->ResampleData();
			}
		}
#endif
		if (CurrentAnim != LocomotionBlendSpace || bReseeded)
		{
			CurrentAnim = LocomotionBlendSpace;
			Mesh->PlayAnimation(LocomotionBlendSpace, /*bLooping*/ true);
		}
		if (UAnimSingleNodeInstance* SingleNode = Mesh->GetSingleNodeInstance())
		{
			SingleNode->SetBlendSpacePosition(FVector(DirectionAngleDegrees, MassSpeed, 0.0));
		}
		Mesh->SetPlayRate(GaitRateScale);
		return;
	}

	const bool bWalking = MassSpeed > IdleSpeedThresholdCmS;
	UAnimSequenceBase* DesiredAnim = bWalking ? WalkAnim.Get() : IdleAnim.Get();
	if (DesiredAnim == nullptr)
	{
		return;
	}

	if (CurrentAnim != DesiredAnim || bReseeded)
	{
		CurrentAnim = DesiredAnim;
		Mesh->PlayAnimation(DesiredAnim, /*bLooping*/ true);
		const uint32 PhaseHash = (static_cast<uint32>(CachedHandle.Index) + 7919u) * 2246822519u;
		const float Phase01 = static_cast<float>(PhaseHash & 0x3FF) / 1023.0f;
		Mesh->SetPosition(Phase01 * DesiredAnim->GetPlayLength(), /*bFireNotifies*/ false);
	}

	const float PlayRate = bWalking
		? FMath::Clamp(MassSpeed / FMath::Max(WalkAnimReferenceSpeedCmS, 1.0f), 0.4f, 2.0f) * GaitRateScale
		: GaitRateScale;
	Mesh->SetPlayRate(PlayRate);
}
