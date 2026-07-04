// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "RammsCrowdStimulusTypes.h"
#include "RammsBackAwayTask.generated.h"

struct FStateTreeExecutionContext;
struct FMassMoveTargetFragment;
struct FMassMovementParameters;
struct FTransformFragment;
class UMassSignalSubsystem;
namespace UE::MassBehavior
{
struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FRammsBackAwayTaskInstanceData
{
	GENERATED_BODY()

	/** Hard cap on how long the back-away runs before succeeding, s. */
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.1"))
	float MaxDuration = 2.0f;

	/** Override for the back-away distance; <= 0 uses the reaction profile's BackAwayDistanceCm. */
	UPROPERTY(EditAnywhere, Category = Parameter)
	float DistanceOverrideCm = -1.0f;

	UPROPERTY()
	float Time = 0.0f;
};

/**
 * Startle response: the agent leaves its lane corridor and backs away from the stimulus
 * while facing it, at BackAwaySpeedScale x default speed. Succeeds on arrival, when the
 * startle zone is exited, or after MaxDuration.
 *
 * Authoring contract: this task owns FMassMoveTargetFragment while active — it must be
 * a sibling state that INTERRUPTS path-follow/stand (never a parallel task). The next
 * state's engine task re-plans from the (frozen) lane location, so keep the back-away
 * distance small (<= ~200 cm).
 */
USTRUCT(meta = (DisplayName = "RAMMS Back Away"))
struct RAMMSCROWD_API FRammsBackAwayTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsBackAwayTaskInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	TStateTreeExternalDataHandle<FRammsStimulusFragment> StimulusHandle;
	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<FMassMoveTargetFragment> MoveTargetHandle;
	TStateTreeExternalDataHandle<FMassMovementParameters> MovementParamsHandle;
	TStateTreeExternalDataHandle<FRammsReactionParamsFragment> ReactionParamsHandle;
	TStateTreeExternalDataHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;
};
