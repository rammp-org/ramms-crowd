// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrossingGateProcessor.h"

#include "Engine/World.h"
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
}

void URammsCrossingGateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
	UWorld* World = Context.GetWorld();
	if (CrowdSettings == nullptr || World == nullptr || !CrowdSettings->CrossingTag.IsValid())
	{
		return;
	}
	const FZoneGraphTag CrossingTag = CrowdSettings->CrossingTag;
	const UMassCrowdSubsystem* Crowd = UWorld::GetSubsystem<UMassCrowdSubsystem>(World);
	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	if (Crowd == nullptr || ZoneGraph == nullptr)
	{
		return;
	}
	// Crossings a traffic vehicle is over or about to cross (none without traffic).
	const URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(World);
	const URammsTrafficSubsystem* Held = Traffic != nullptr && Traffic->GetHeldCrossingCount() > 0 ? Traffic : nullptr;

	EntityQuery.ForEachEntityChunk(Context, [Crowd, ZoneGraph, CrossingTag, Held](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TArrayView<FRammsCrossingWaitFragment> WaitList = Context.GetMutableFragmentView<FRammsCrossingWaitFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FMassZoneGraphLaneLocationFragment& Location = LaneList[EntityIt];
			bool bHold = false;
			if (Location.LaneHandle.IsValid() && Location.DistanceAlongLane <= HoldDistanceCm)
			{
				FZoneGraphTagMask Tags;
				if (ZoneGraph->GetLaneTags(Location.LaneHandle, Tags) && Tags.Contains(CrossingTag) && Crowd->HasCrowdDataForZoneGraph(Location.LaneHandle.DataHandle))
				{
					bHold = Crowd->GetLaneState(Location.LaneHandle) == ECrowdLaneState::Closed;
				}
				// Nobody steps in front of a car that is clearing the intersection or driving over the crossing.
				bHold = bHold || (Held != nullptr && Held->IsCrossingHeld(Location.LaneHandle));
			}

			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIt];
			FRammsCrossingWaitFragment& Wait = WaitList[EntityIt];
			if (bHold)
			{
				// The StateTree may have just asked for a new path at walking speed: remember it, then stand.
				const float Speed = MoveTarget.DesiredSpeed.Get();
				if (Speed > 1.0f)
				{
					Wait.HeldSpeed = Speed;
				}
				Wait.bHolding = true;
				MoveTarget.DesiredSpeed.Set(0.0f);
			}
			else if (Wait.bHolding)
			{
				if (MoveTarget.DesiredSpeed.Get() <= 1.0f && Wait.HeldSpeed > 1.0f)
				{
					MoveTarget.DesiredSpeed.Set(Wait.HeldSpeed);
				}
				Wait.bHolding = false;
			}
		}
	});
}
