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

	/**
	 * Chance (0..1) that this walk ends in a stand. Otherwise the path keeps its Move
	 * intent and the agent walks straight on into the next leg (bind StandAtEnd to the
	 * state's transitions to pick Stand or another Wander).
	 */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StandChance = 1.0f;

	/** Whether this walk was chosen to end in a stand. */
	UPROPERTY(EditAnywhere, Category = Output)
	bool bStandAtEnd = true;
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

USTRUCT()
struct FRammsRandomDurationTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MinSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0"))
	float MaxSeconds = 8.0f;

	/** Picked on entering the state, between MinSeconds and MaxSeconds. */
	UPROPERTY(EditAnywhere, Category = Output)
	float Duration = 0.0f;
};

/**
 * Picks a random duration when the state is entered, for a later task in the same
 * state to bind (e.g. ZG Stand's Duration), so pedestrians don't all pause for the
 * same time.
 */
USTRUCT(meta = (DisplayName = "RAMMS Random Duration"))
struct RAMMSCROWD_API FRammsRandomDurationTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsRandomDurationTaskInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
