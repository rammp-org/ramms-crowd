// Copyright (c) RAMMP. All rights reserved.

#include "RammsStimulusSourceComponent.h"

#include "Annotations/ZoneGraphDisturbanceAnnotationBPLibrary.h"
#include "GameFramework/Actor.h"
#include "MassAgentComponent.h"
#include "RammsStimulusSubsystem.h"

URammsStimulusSourceComponent::URammsStimulusSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URammsStimulusSourceComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedAgentComponent = GetOwner() ? GetOwner()->FindComponentByClass<UMassAgentComponent>() : nullptr;

	if (URammsStimulusSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<URammsStimulusSubsystem>() : nullptr)
	{
		SourceHandle = Subsystem->RegisterSource();
	}
	bHasPreviousLocation = false;
}

void URammsStimulusSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SourceHandle != INDEX_NONE)
	{
		if (URammsStimulusSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<URammsStimulusSubsystem>() : nullptr)
		{
			Subsystem->UnregisterSource(SourceHandle);
		}
		SourceHandle = INDEX_NONE;
	}
	Super::EndPlay(EndPlayReason);
}

void URammsStimulusSourceComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	URammsStimulusSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<URammsStimulusSubsystem>() : nullptr;
	if (Owner == nullptr || Subsystem == nullptr || SourceHandle == INDEX_NONE)
	{
		return;
	}

	if (!bEnabled)
	{
		Subsystem->ClearSource(SourceHandle);
		bHasPreviousLocation = false;
		return;
	}

	const FVector Location = Owner->GetActorLocation();
	const FVector Velocity = ResolveVelocity(Location, DeltaTime);

	FRammsStimulusSnapshot Snapshot;
	Snapshot.Location = Location;
	Snapshot.Velocity = Velocity;
	Snapshot.Radius = RadiusCm;
	Snapshot.StrengthScale = StrengthScale;
	if (CachedAgentComponent != nullptr)
	{
		Snapshot.Entity = CachedAgentComponent->GetEntityHandle();
	}
	Subsystem->UpdateSource(SourceHandle, Snapshot);

	if (bTriggerLaneDisturbance && Velocity.Size2D() > DisturbanceSpeedThresholdCmS)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		// The disturbance annotation keeps one persistent danger per instigator; refreshing
		// twice a second follows the source without flooding the annotation subsystem.
		if (Now - LastDisturbanceTime > 0.5)
		{
			UZoneGraphDisturbanceAnnotationBPLibrary::TriggerDanger(GetWorld(), Owner, Location, DisturbanceRadiusCm, DisturbanceDurationSeconds);
			LastDisturbanceTime = Now;
		}
	}
}

FVector URammsStimulusSourceComponent::ResolveVelocity(const FVector& CurrentLocation, const float DeltaTime)
{
	const AActor* Owner = GetOwner();
	FVector Velocity = Owner->GetVelocity();

	// Physics-driven pawns report root velocity; kinematic or constraint-driven roots may
	// not, so fall back to finite differencing.
	if (Velocity.IsNearlyZero() && bHasPreviousLocation && DeltaTime > UE_SMALL_NUMBER)
	{
		Velocity = (CurrentLocation - PreviousLocation) / DeltaTime;
	}
	PreviousLocation = CurrentLocation;
	bHasPreviousLocation = true;
	return Velocity;
}
