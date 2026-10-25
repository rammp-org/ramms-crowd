// Copyright (c) RAMMP. All rights reserved.

#include "RammsStandAtPathEndTask.h"

#include "StateTreeExecutionContext.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"

EStateTreeRunStatus FRammsStandAtPathEndTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (FMassZoneGraphTargetLocation* Target = InstanceData.TargetLocation.GetMutablePtr<FMassZoneGraphTargetLocation>(Context))
	{
		Target->EndOfPathIntent = EMassMovementAction::Stand;
	}
	// Nothing to do after entering; let the state's other tasks decide when it ends.
	return EStateTreeRunStatus::Running;
}
