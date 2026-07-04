// Copyright (c) RAMMP. All rights reserved.

#include "RammsBackAwayTask.h"

#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

namespace UE::RammsCrowd
{
/** Poll interval for signal-driven task completion; Mass StateTrees do not tick per-frame. */
constexpr float BackAwayPollIntervalSeconds = 0.25f;
} // namespace UE::RammsCrowd

bool FRammsBackAwayTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(StimulusHandle);
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(MoveTargetHandle);
	Linker.LinkExternalData(MovementParamsHandle);
	Linker.LinkExternalData(ReactionParamsHandle);
	Linker.LinkExternalData(MassSignalSubsystemHandle);
	return true;
}

void FRammsBackAwayTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(StimulusHandle);
	Builder.AddReadOnly(TransformHandle);
	Builder.AddReadWrite(MoveTargetHandle);
	Builder.AddReadOnly(MovementParamsHandle);
	Builder.AddReadOnly(ReactionParamsHandle);
	Builder.AddReadWrite(MassSignalSubsystemHandle);
}

EStateTreeRunStatus FRammsBackAwayTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	const FRammsStimulusFragment& Stimulus = Context.GetExternalData(StimulusHandle);
	const FTransformFragment& Transform = Context.GetExternalData(TransformHandle);
	const FMassMovementParameters& MovementParams = Context.GetExternalData(MovementParamsHandle);
	const FRammsReactionParamsFragment& ReactionParams = Context.GetExternalData(ReactionParamsHandle);
	FMassMoveTargetFragment& MoveTarget = Context.GetExternalData(MoveTargetHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	const UWorld* World = Context.GetWorld();
	checkf(World != nullptr, TEXT("A valid world is expected from the execution context"));

	const float BackAwayDistance = InstanceData.DistanceOverrideCm > 0.0f ? InstanceData.DistanceOverrideCm : ReactionParams.BackAwayDistanceCm;
	const FVector AgentLocation = Transform.GetTransform().GetLocation();

	MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
	MoveTarget.Center = AgentLocation + Stimulus.AwayDirection * BackAwayDistance;
	// Face the stimulus while retreating (social back-away, avoids a snap-turn).
	MoveTarget.Forward = -Stimulus.AwayDirection;
	MoveTarget.DistanceToGoal = BackAwayDistance;
	MoveTarget.SlackRadius = 30.0f;
	MoveTarget.DesiredSpeed = FMassInt16Real(MovementParams.DefaultDesiredSpeed * ReactionParams.BackAwaySpeedScale);
	MoveTarget.IntentAtGoal = EMassMovementAction::Stand;
	// We are deliberately leaving the lane corridor.
	MoveTarget.bOffBoundaries = true;

	InstanceData.Time = 0.0f;

	UMassSignalSubsystem& SignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
	SignalSubsystem.DelaySignalEntityDeferred(MassContext.GetMassEntityExecutionContext(), UE::Mass::Signals::NewStateTreeTaskRequired,
		MassContext.GetEntity(), UE::RammsCrowd::BackAwayPollIntervalSeconds);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FRammsBackAwayTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	const FRammsStimulusFragment& Stimulus = Context.GetExternalData(StimulusHandle);
	const FTransformFragment& Transform = Context.GetExternalData(TransformHandle);
	const FMassMoveTargetFragment& MoveTarget = Context.GetExternalData(MoveTargetHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.Time += DeltaTime;

	const float DistanceToGoal = static_cast<float>(FVector::Dist2D(Transform.GetTransform().GetLocation(), MoveTarget.Center));
	const bool bArrived = DistanceToGoal <= MoveTarget.SlackRadius;
	const bool bCalmedDown = Stimulus.Zone < ERammsStimulusZone::Startle;
	const bool bTimedOut = InstanceData.Time >= InstanceData.MaxDuration;

	if (bArrived || bCalmedDown || bTimedOut)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Re-arm the poll; without it the (signal-driven) tree would never tick this task again.
	UMassSignalSubsystem& SignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
	SignalSubsystem.DelaySignalEntityDeferred(MassContext.GetMassEntityExecutionContext(), UE::Mass::Signals::NewStateTreeTaskRequired,
		MassContext.GetEntity(), UE::RammsCrowd::BackAwayPollIntervalSeconds);

	return EStateTreeRunStatus::Running;
}
