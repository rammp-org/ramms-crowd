// Copyright (c) RAMMP. All rights reserved.

#include "RammsStandAtPathEndTask.h"

#include "Engine/World.h"
#include "RammsTrafficSubsystem.h"
#include "StateTreeExecutionContext.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"

EStateTreeRunStatus FRammsStandAtPathEndTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bStandAtEnd = InstanceData.StandChance >= 1.0f || FMath::FRand() < InstanceData.StandChance;
	FMassZoneGraphTargetLocation* Target = InstanceData.TargetLocation.GetMutablePtr<FMassZoneGraphTargetLocation>(Context);
	// Nobody stops in the street: a walk that ends on a street crossing (a pedestrian lane over a traffic lane) walks
	// on. Without traffic there are none.
	if (InstanceData.bStandAtEnd && Target != nullptr && Target->LaneHandle.IsValid())
	{
		const URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(Context.GetWorld());
		if (Traffic != nullptr && Traffic->IsStreetCrossing(Target->LaneHandle))
		{
			InstanceData.bStandAtEnd = false;
		}
	}
	if (Target != nullptr && InstanceData.bStandAtEnd)
	{
		Target->EndOfPathIntent = EMassMovementAction::Stand;
	}
	// Nothing to do after entering; let the state's other tasks decide when it ends.
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FRammsRandomDurationTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.Duration = FMath::FRandRange(InstanceData.MinSeconds, FMath::Max(InstanceData.MaxSeconds, InstanceData.MinSeconds));
	return EStateTreeRunStatus::Running;
}
