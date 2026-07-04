// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimComponent.h"

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
}
