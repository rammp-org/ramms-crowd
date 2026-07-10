// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdStimulusProcessor.h"

#include "DrawDebugHelpers.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeTypes.h"
#include "RammsCrowdStimulusTypes.h"
#include "RammsStimulusSubsystem.h"

namespace UE::RammsCrowd
{
static TAutoConsoleVariable<bool> CVarDebugStimulus(
	TEXT("ramms.crowd.DebugStimulus"), false,
	TEXT("Draw crowd stimulus debug: proxemics rings around stimulus sources and zone-colored markers on reacting NPCs."));

static ERammsStimulusZone ClassifyZone(const float Distance, const float ClosingSpeed, const FRammsStimulusFragment& Fragment,
	const FRammsReactionParamsFragment& Params, const double Now)
{
	// Hysteresis: leaving a zone requires exceeding its radius by ExitHysteresis.
	auto ZoneRadius = [&Params](const ERammsStimulusZone Zone) -> float
	{
		switch (Zone)
		{
			case ERammsStimulusZone::Startle: return Params.StartleRadiusCm;
			case ERammsStimulusZone::React: return Params.ReactRadiusCm;
			case ERammsStimulusZone::Aware: return Params.AwareRadiusCm;
			default: return 0.0f;
		}
	};

	ERammsStimulusZone Zone = ERammsStimulusZone::None;
	if (Distance < Params.StartleRadiusCm)
	{
		Zone = ERammsStimulusZone::Startle;
	}
	else if (Distance < Params.ReactRadiusCm)
	{
		Zone = ERammsStimulusZone::React;
	}
	else if (Distance < Params.AwareRadiusCm)
	{
		Zone = ERammsStimulusZone::Aware;
	}

	// Fast approach startles inside the React zone (cooldown-gated below).
	if (Zone == ERammsStimulusZone::React && ClosingSpeed > Params.StartleClosingSpeedCmS)
	{
		Zone = ERammsStimulusZone::Startle;
	}

	// Startle cooldown: do not re-enter Startle too soon after the last activation.
	if (Zone == ERammsStimulusZone::Startle && Fragment.Zone < ERammsStimulusZone::Startle
		&& Now - Fragment.LastStartleTime < Params.StartleCooldownSeconds)
	{
		Zone = ERammsStimulusZone::React;
	}

	// Hysteresis: hold the current zone while within its exit radius.
	if (Zone < Fragment.Zone && Fragment.Zone != ERammsStimulusZone::None
		&& Distance < ZoneRadius(Fragment.Zone) * Params.ExitHysteresis)
	{
		Zone = Fragment.Zone;
	}

	return Zone;
}
} // namespace UE::RammsCrowd

URammsCrowdStimulusProcessor::URammsCrowdStimulusProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
	// Perception reads the game-thread stimulus registry; trivial math for <=500 entities.
	bRequiresGameThreadExecution = true;
}

void URammsCrowdStimulusProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FRammsStimulusFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddConstSharedRequirement<FRammsReactionParamsFragment>();
	EntityQuery.AddSubsystemRequirement<URammsStimulusSubsystem>(EMassFragmentAccess::ReadOnly);
	// The signal subsystem is used AFTER chunk iteration (batched signals), so it must
	// be registered on ProcessorRequirements: EntityQuery-level subsystem requirements
	// are only accessible inside ForEachEntityChunk.
	ProcessorRequirements.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
}

void URammsCrowdStimulusProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = EntityManager.GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const bool bDebugDraw = UE::RammsCrowd::CVarDebugStimulus.GetValueOnGameThread();

	TArray<FMassEntityHandle> ZoneChangedEntities;
	TArray<FMassEntityHandle> StartledEntities;

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
	{
		const URammsStimulusSubsystem& StimulusSubsystem = Context.GetSubsystemChecked<URammsStimulusSubsystem>();
		const TConstArrayView<FRammsStimulusSnapshot> Snapshots = StimulusSubsystem.GetSnapshots();

		const FRammsReactionParamsFragment& Params = Context.GetConstSharedFragment<FRammsReactionParamsFragment>();
		const TArrayView<FRammsStimulusFragment> StimulusList = Context.GetMutableFragmentView<FRammsStimulusFragment>();
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetFragmentView<FMassVelocityFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			FRammsStimulusFragment& Stimulus = StimulusList[EntityIt];
			const FVector EntityLocation = TransformList[EntityIt].GetTransform().GetLocation();

			// Nearest active stimulus source.
			const FRammsStimulusSnapshot* Nearest = nullptr;
			float NearestDistSq = FLT_MAX;
			for (const FRammsStimulusSnapshot& Snapshot : Snapshots)
			{
				const float DistSq = FVector::DistSquared2D(EntityLocation, Snapshot.Location);
				if (DistSq < NearestDistSq)
				{
					NearestDistSq = DistSq;
					Nearest = &Snapshot;
				}
			}

			ERammsStimulusZone NewZone = ERammsStimulusZone::None;
			if (Nearest != nullptr)
			{
				const FVector ToEntity = EntityLocation - Nearest->Location;
				const float CenterDistance = ToEntity.Size2D();
				const float SurfaceDistance = FMath::Max(0.0f, CenterDistance - Nearest->Radius);

				// Closing speed along the separation axis (positive = approaching).
				const FVector RelativeVelocity = Nearest->Velocity - VelocityList[EntityIt].Value;
				const FVector SeparationDir = CenterDistance > UE_KINDA_SMALL_NUMBER ? ToEntity / CenterDistance : FVector::ForwardVector;
				const float ClosingSpeed = static_cast<float>(FVector::DotProduct(RelativeVelocity, SeparationDir));

				Stimulus.StimulusEntity = Nearest->Entity;
				Stimulus.StimulusLocation = Nearest->Location;
				Stimulus.AwayDirection = FVector(SeparationDir.X, SeparationDir.Y, 0.0).GetSafeNormal2D(UE_KINDA_SMALL_NUMBER, FVector::ForwardVector);
				Stimulus.Distance = SurfaceDistance;
				Stimulus.ClosingSpeed = ClosingSpeed;
				Stimulus.Strength = FMath::Clamp(1.0f - SurfaceDistance / FMath::Max(Params.AwareRadiusCm, 1.0f), 0.0f, 1.0f) * Nearest->StrengthScale;

				NewZone = UE::RammsCrowd::ClassifyZone(SurfaceDistance, ClosingSpeed, Stimulus, Params, Now);
			}
			else
			{
				Stimulus.StimulusEntity = FMassEntityHandle();
				Stimulus.Distance = FLT_MAX;
				Stimulus.ClosingSpeed = 0.0f;
				Stimulus.Strength = 0.0f;
			}

			if (NewZone != Stimulus.Zone)
			{
				Stimulus.PreviousZone = Stimulus.Zone;
				Stimulus.Zone = NewZone;
				Stimulus.ZoneEnterTime = static_cast<float>(Now);
				if (NewZone == ERammsStimulusZone::Startle)
				{
					Stimulus.LastStartleTime = static_cast<float>(Now);
					StartledEntities.Add(Context.GetEntity(EntityIt));
				}
				ZoneChangedEntities.Add(Context.GetEntity(EntityIt));
			}

			if (bDebugDraw && Stimulus.Zone != ERammsStimulusZone::None)
			{
				const FColor ZoneColor = Stimulus.Zone == ERammsStimulusZone::Startle ? FColor::Red
					: Stimulus.Zone == ERammsStimulusZone::React ? FColor::Orange
					: FColor::Yellow;
				DrawDebugPoint(World, EntityLocation + FVector(0, 0, 200), 12.0f, ZoneColor, false, -1.0f);
			}
		}
	});

	if (ZoneChangedEntities.Num() > 0)
	{
		UMassSignalSubsystem& SignalSubsystem = Context.GetMutableSubsystemChecked<UMassSignalSubsystem>();
		SignalSubsystem.SignalEntitiesDeferred(Context, UE::Mass::Signals::NewStateTreeTaskRequired, ZoneChangedEntities);
		SignalSubsystem.SignalEntitiesDeferred(Context, UE::RammsCrowd::Signals::StimulusZoneChanged, ZoneChangedEntities);
		if (StartledEntities.Num() > 0)
		{
			SignalSubsystem.SignalEntitiesDeferred(Context, UE::RammsCrowd::Signals::Startled, StartledEntities);
		}
	}

	if (bDebugDraw)
	{
		if (const URammsStimulusSubsystem* StimulusSubsystem = World->GetSubsystem<URammsStimulusSubsystem>())
		{
			for (const FRammsStimulusSnapshot& Snapshot : StimulusSubsystem->GetSnapshots())
			{
				// Default profile radii; per-entity radii may differ, these rings are orientation only.
				DrawDebugCircle(World, Snapshot.Location, 600.0f, 48, FColor::Yellow, false, -1.0f, 0, 2.0f, FVector::ForwardVector, FVector::RightVector, false);
				DrawDebugCircle(World, Snapshot.Location, 300.0f, 32, FColor::Orange, false, -1.0f, 0, 2.0f, FVector::ForwardVector, FVector::RightVector, false);
				DrawDebugCircle(World, Snapshot.Location, 120.0f, 24, FColor::Red, false, -1.0f, 0, 2.0f, FVector::ForwardVector, FVector::RightVector, false);
			}
		}
	}
}
