// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "StateTreePropertyRef.h"
#include "RammsStandAtPathEndTask.generated.h"

USTRUCT()
struct FRammsStandAtPathEndTaskInstanceData
{
	GENERATED_BODY()

	/** The path target to adjust (bind the wander target here, before the path follow task reads it). */
	UPROPERTY(EditAnywhere, Category = Input, meta = (RefType = "/Script/MassAIBehavior.MassZoneGraphTargetLocation"))
	FStateTreePropertyRef TargetLocation;
};

/**
 * Marks a ZoneGraph path target as ending in a stand. The engine's wander target
 * ends paths with a Move intent (as if walking straight on), so Mass applies no
 * arrival slowdown: agents reach the goal at full speed, overshoot it and creep
 * back, which reads as sliding and shuffling. With Stand at the end, Mass brakes
 * into the goal and the stop animation has a stop to match. Use it between the
 * target-finding task and the path follow task of a state that stands next.
 */
USTRUCT(meta = (DisplayName = "RAMMS Stand At Path End"))
struct RAMMSCROWD_API FRammsStandAtPathEndTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsStandAtPathEndTaskInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
