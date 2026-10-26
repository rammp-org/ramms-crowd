// Copyright (c) RAMMP. All rights reserved.

#include "RammsStandAtPathEndTask.h"

#include "StateTreeExecutionContext.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"

EStateTreeRunStatus FRammsStandAtPathEndTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.bStandAtEnd = InstanceData.StandChance >= 1.0f || FMath::FRand() < InstanceData.StandChance;
	FMassZoneGraphTargetLocation* Target = InstanceData.TargetLocation.GetMutablePtr<FMassZoneGraphTargetLocation>(Context);
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
