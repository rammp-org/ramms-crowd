// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrossingGateProcessor.h"

#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassCrowdSettings.h"
#include "MassCrowdSubsystem.h"
#include "MassExecutionContext.h"
#include "MassNavigationFragments.h"
#include "MassZoneGraphNavigationFragments.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsTrafficSubsystem.h"
#include "ZoneGraphSubsystem.h"

URammsCrossingGateProcessor::URammsCrossingGateProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	// After the StateTree has requested this frame's path and speed, before path following advances along it.
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Tasks;
	ExecutionOrder.ExecuteBefore.Add(FName(TEXT("MassZoneGraphPathFollowProcessor")));
	bRequiresGameThreadExecution = true;
}

void URammsCrossingGateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRammsCrossingWaitFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

float URammsCrossingGateProcessor::HoldLimit(const FRammsStreetCrossing* Crossing, const float Radius)
{
	if (Crossing == nullptr)
	{
		return HoldDistanceCm;
	}
	return FMath::Clamp(Crossing->EdgeStart - Radius - 10.0f, MinHoldDistanceCm, HoldDistanceCm);
}

bool URammsCrossingGateProcessor::UpdateWait(FRammsCrossingWaitFragment& Wait, const FZoneGraphLaneHandle& Lane, const float DistanceAlongLane, const float Limit,
	const bool bSignalClosed, const float* CarHoldSince, const float Now, float& InOutDesiredSpeed)
{
	if (Wait.Lane != Lane)
	{
		Wait.Lane = Lane;
		Wait.LaneSince = Now;
		Wait.bHeldForCar = false;
	}
	bool bHold = false;
	bool bForCar = false;
	if (Lane.IsValid() && DistanceAlongLane <= Limit)
	{
		// Nobody steps in front of a car that is clearing the intersection or driving over the crossing, nor in front
		// of one that began to yield to other walkers before they came; and whoever waits for a car keeps waiting while
		// any car holds the crossing (the car was there first). Whoever waited for the signal goes on Walk unless a car
		// was there first.
		bForCar = CarHoldSince != nullptr && (Wait.LaneSince >= *CarHoldSince || Wait.bHeldForCar);
		bHold = bSignalClosed || bForCar;
	}
	Wait.bHeldForCar = bHold && bForCar && !bSignalClosed;

	if (bHold)
	{
		// The StateTree may have just asked for a new path at walking speed: remember it, then stand.
		if (InOutDesiredSpeed > 1.0f)
		{
			Wait.HeldSpeed = InOutDesiredSpeed;
		}
		Wait.bHolding = true;
		InOutDesiredSpeed = 0.0f;
	}
	else if (Wait.bHolding)
	{
		if (InOutDesiredSpeed <= 1.0f && Wait.HeldSpeed > 1.0f)
		{
			InOutDesiredSpeed = Wait.HeldSpeed;
		}
		Wait.bHolding = false;
	}
	return bHold;
}

void URammsCrossingGateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
	const FZoneGraphTag CrossingTag = CrowdSettings != nullptr ? CrowdSettings->CrossingTag : FZoneGraphTag();
	const UMassCrowdSubsystem* Crowd = UWorld::GetSubsystem<UMassCrowdSubsystem>(World);
	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	// Crossings a traffic vehicle is over or about to cross, street crossings and vehicle lanes (none without traffic).
	URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(World);
	const double Now = World->GetTimeSeconds();
	const bool bCarHolds = Traffic != nullptr && Traffic->GetHeldCrossingCount(Now) > 0;
	if (!CrossingTag.IsValid() && !bCarHolds && Traffic == nullptr)
	{
		return;
	}
	int32 OnVehicleLanes = 0;

	EntityQuery.ForEachEntityChunk(Context, [Crowd, ZoneGraph, CrossingTag, Traffic, bCarHolds, Now, &OnVehicleLanes](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TArrayView<FRammsCrossingWaitFragment> WaitList = Context.GetMutableFragmentView<FRammsCrossingWaitFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassZoneGraphLaneLocationFragment& Location = LaneList[EntityIt];
			const FZoneGraphLaneHandle& Lane = Location.LaneHandle;
			const float Radius = RadiusList.IsEmpty() ? 25.0f : RadiusList[EntityIt].Radius; // (URammsCrowdAgentTrait's default)
			const FRammsStreetCrossing* Crossing = Traffic != nullptr && Lane.IsValid() ? Traffic->FindStreetCrossing(Lane) : nullptr;
			const float Limit = HoldLimit(Crossing, Radius);
			OnVehicleLanes += Traffic != nullptr && Lane.IsValid() && Traffic->IsVehicleLane(Lane) ? 1 : 0;

			bool bSignalClosed = false;
			const float* CarHoldSince = nullptr;
			if (Lane.IsValid() && Location.DistanceAlongLane <= Limit)
			{
				FZoneGraphTagMask Tags;
				if (CrossingTag.IsValid() && Crowd != nullptr && ZoneGraph != nullptr && ZoneGraph->GetLaneTags(Lane, Tags) && Tags.Contains(CrossingTag)
					&& Crowd->HasCrowdDataForZoneGraph(Lane.DataHandle))
				{
					bSignalClosed = Crowd->GetLaneState(Lane) == ECrowdLaneState::Closed;
				}
				CarHoldSince = bCarHolds ? Traffic->FindCrossingHold(Lane, Now) : nullptr;
			}

			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIt];
			float DesiredSpeed = MoveTarget.DesiredSpeed.Get();
			const float Before = DesiredSpeed;
			UpdateWait(WaitList[EntityIt], Lane, Location.DistanceAlongLane, Limit, bSignalClosed, CarHoldSince, static_cast<float>(Now), DesiredSpeed);
			if (DesiredSpeed != Before)
			{
				MoveTarget.DesiredSpeed.Set(DesiredSpeed);
			}
		}
	});

	if (Traffic != nullptr)
	{
		Traffic->WarnWalkersOnVehicleLanes(OnVehicleLanes);
	}
}
