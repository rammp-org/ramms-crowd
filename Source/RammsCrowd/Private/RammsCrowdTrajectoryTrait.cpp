// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdTrajectoryTrait.h"

#include "Components/SkeletalMeshComponent.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityManager.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "Math/SpringMath.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "RammsCrowdAgentTrait.h"

void URammsCrowdTrajectoryTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FCharacterTrajectoryFragment>();
	BuildContext.AddFragment<FRammsCrowdTrajectoryStateFragment>();
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);
	BuildContext.AddConstSharedFragment(EntityManager.GetOrCreateConstSharedFragment(TrajectoryParameters));
	BuildContext.AddConstSharedFragment(EntityManager.GetOrCreateConstSharedFragment(Prediction));

	// As the engine's Character Trajectory Generation trait: the mesh's offset from
	// the agent, read from the representation actor when one is bound.
	BuildContext.GetMutableObjectFragmentInitializers().Add([](UObject& Owner, const FMassEntityView& EntityView, const EMassTranslationDirection CurrentDirection)
	{
		if (CurrentDirection == EMassTranslationDirection::MassToActor)
		{
			return;
		}
		const AActor* AsActor = Cast<AActor>(&Owner);
		const USkeletalMeshComponent* MeshComponent = AsActor != nullptr ? AsActor->FindComponentByClass<USkeletalMeshComponent>() : Cast<USkeletalMeshComponent>(&Owner);
		if (MeshComponent != nullptr)
		{
			EntityView.GetFragmentData<FCharacterTrajectoryFragment>().MeshRelativeTransform = MeshComponent->GetRelativeTransform();
		}
	});
}

URammsCrowdTrajectoryProcessor::URammsCrowdTrajectoryProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	// After the agent has moved this frame, so the prediction starts from what it did,
	// and before the actors are updated. Not in the engine's CharacterTrajectoryGeneration
	// group: its processors run before Movement, and a group that must run both before
	// and after Movement is a cycle the scheduler breaks by reordering the pipeline
	// (avoidance ended up after movement; pedestrians walked into each other).
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
}

void URammsCrowdTrajectoryProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FCharacterTrajectoryFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRammsCrowdTrajectoryStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddConstSharedRequirement<FMassMovementParameters>(EMassFragmentPresence::All);
	EntityQuery.AddConstSharedRequirement<FCharacterTrajectoryParameters>(EMassFragmentPresence::All);
	EntityQuery.AddConstSharedRequirement<FRammsCrowdTrajectoryParameters>(EMassFragmentPresence::All);
	EntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
}

void URammsCrowdTrajectoryProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = FMath::Max(Context.GetDeltaTimeSeconds(), 1.0e-4f);
	EntityQuery.ParallelForEachEntityChunk(Context, [DeltaTime](FMassExecutionContext& Context)
	{
		const FCharacterTrajectoryParameters& TrajectoryParams = Context.GetConstSharedFragment<FCharacterTrajectoryParameters>();
		const FRammsCrowdTrajectoryParameters& Prediction = Context.GetConstSharedFragment<FRammsCrowdTrajectoryParameters>();
		const TArrayView<FCharacterTrajectoryFragment> TrajectoryList = Context.GetMutableFragmentView<FCharacterTrajectoryFragment>();
		const TArrayView<FRammsCrowdTrajectoryStateFragment> StateList = Context.GetMutableFragmentView<FRammsCrowdTrajectoryStateFragment>();
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FMassDesiredMovementFragment> DesiredList = Context.GetFragmentView<FMassDesiredMovementFragment>();
		const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();
		const FMassMovementParameters& Movement = Context.GetConstSharedFragment<FMassMovementParameters>();
		const float Braking = FMath::Max(Movement.MaxAcceleration * 0.6f, 50.0f);

		FPoseSearchTrajectoryData::FSampling Sampling;
		Sampling.NumHistorySamples = TrajectoryParams.NumHistorySamples;
		Sampling.SecondsPerHistorySample = -1.0f;
		Sampling.NumPredictionSamples = TrajectoryParams.NumPredictionSamples;
		Sampling.SecondsPerPredictionSample = TrajectoryParams.PredictionSamplingInterval;
		const int32 NumPredictions = FMath::Max(TrajectoryParams.NumPredictionSamples, 1);

		TArray<FVector, TInlineAllocator<32>> Positions;
		TArray<FVector, TInlineAllocator<32>> Velocities;
		TArray<FVector, TInlineAllocator<32>> Accelerations;
		TArray<FQuat, TInlineAllocator<32>> Facings;
		TArray<FVector, TInlineAllocator<32>> AngularVelocities;
		Positions.SetNumUninitialized(NumPredictions);
		Velocities.SetNumUninitialized(NumPredictions);
		Accelerations.SetNumUninitialized(NumPredictions);
		Facings.SetNumUninitialized(NumPredictions);
		AngularVelocities.SetNumUninitialized(NumPredictions);

		const float Smoothing = 1.0f - FMath::Exp(-DeltaTime / 0.1f);
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			FCharacterTrajectoryFragment& Trajectory = TrajectoryList[EntityIt];
			FRammsCrowdTrajectoryStateFragment& State = StateList[EntityIt];
			const FTransform MeshRelative = TrajectoryParams.Offset * Trajectory.MeshRelativeTransform;
			const FTransform Current = MeshRelative * TransformList[EntityIt].GetTransform();
			const FVector Velocity = FVector(VelocityList[EntityIt].Value.X, VelocityList[EntityIt].Value.Y, 0.0);
			const FQuat Facing = Current.GetRotation();

			if (!State.bInitialized)
			{
				State.PreviousVelocity = Velocity;
				State.PreviousFacing = Facing;
				State.bInitialized = true;
			}
			State.Acceleration += ((Velocity - State.PreviousVelocity) / DeltaTime - State.Acceleration) * Smoothing;
			State.PreviousVelocity = Velocity;
			{
				const FQuat Delta = Facing * State.PreviousFacing.Inverse();
				FVector Axis;
				double Angle;
				Delta.ToAxisAndAngle(Axis, Angle);
				if (Angle > UE_PI)
				{
					Angle -= 2.0 * UE_PI;
				}
				State.AngularVelocity += (Axis * (Angle / DeltaTime) - State.AngularVelocity) * Smoothing;
				State.PreviousFacing = Facing;
			}

			// History: the engine's sample layout, shifted each frame.
			UPoseSearchTrajectoryLibrary::InitTrajectorySamples(Trajectory.Trajectory, Current.GetLocation(), Facing, Sampling, DeltaTime);
			UPoseSearchTrajectoryLibrary::UpdateHistory_WorldSpace(Trajectory.Trajectory, Sampling, DeltaTime);

			// Where the agent is going: along its path at the speed it wants, braking to a
			// stop at a goal it will stand at, but never faster than it is managing (its
			// speed plus the acceleration it is getting; a held-up agent is predicted to
			// stop, which is what it does). The spring eases the prediction from the
			// current velocity toward that, a step at a time.
			const FMassDesiredMovementFragment& Desired = DesiredList[EntityIt];
			const FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIt];
			const FVector AgentLocation = TransformList[EntityIt].GetTransform().GetLocation();
			const bool bMoving = MoveTarget.GetCurrentAction() == EMassMovementAction::Move;
			FVector PathDirection = MoveTarget.Forward.GetSafeNormal2D();
			if (PathDirection.IsNearlyZero())
			{
				PathDirection = Velocity.GetSafeNormal2D();
			}
			// Speed and its change along the way the agent is actually going (which, while
			// it detours round someone, is not the path direction).
			const float Speed = static_cast<float>(Velocity.Size2D());
			const float PathSpeed = Speed;
			const float PathAcceleration = Speed > 1.0f ? static_cast<float>(FVector::DotProduct(State.Acceleration, Velocity / Speed)) : static_cast<float>(State.Acceleration.Size2D());
			float Remaining = UE_BIG_NUMBER;
			float WantedSpeed = 0.0f;
			if (bMoving)
			{
				WantedSpeed = FMath::Clamp(PathSpeed + FMath::Max(PathAcceleration, 0.0f) * 0.6f, 0.0f, FMath::Max(MoveTarget.DesiredSpeed.Get(), Speed));
				if (MoveTarget.IntentAtGoal == EMassMovementAction::Stand)
				{
					const bool bEntityDistance = MoveTarget.EntityDistanceToGoal != FMassMoveTargetFragment::UnsetDistance;
					const double Behind = FMath::Max(0.0, FVector::DotProduct(MoveTarget.Center - AgentLocation, MoveTarget.Forward));
					Remaining = static_cast<float>(bEntityDistance ? MoveTarget.EntityDistanceToGoal : MoveTarget.DistanceToGoal + Behind);
				}
			}
			else
			{
				// Standing, perhaps still walking onto the stand point.
				const double ToCenter = FVector::Dist2D(AgentLocation, MoveTarget.Center);
				Remaining = ToCenter > 15.0 ? static_cast<float>(ToCenter) : 0.0f;
				WantedSpeed = Remaining > 0.0f ? Speed : 0.0f;
			}
			if (WantedSpeed < Prediction.VelocityDeadzone)
			{
				WantedSpeed = 0.0f;
			}
			{
				FVector P = Current.GetLocation();
				FVector V = Velocity;
				FVector A = State.Acceleration;
				for (int32 Index = 0; Index < NumPredictions; ++Index)
				{
					const float Allowed = FMath::Min(WantedSpeed, FMath::Sqrt(2.0f * Braking * FMath::Max(Remaining, 0.0f)));
					const FVector Target = Allowed < Prediction.VelocityDeadzone ? FVector::ZeroVector : PathDirection * Allowed;
					const FVector Before = P;
					SpringMath::SpringCharacterUpdate(P, V, A, Target, Prediction.VelocitySmoothingTime, TrajectoryParams.PredictionSamplingInterval, Prediction.VelocityDeadzone);
					Remaining -= static_cast<float>(FVector::Dist2D(P, Before));
					Positions[Index] = P;
					Velocities[Index] = V;
				}
			}
			// Facing: people turn to face where they walk; standing, where the steering faces.
			FQuat TargetFacing = (Desired.DesiredFacing * MeshRelative.GetRotation()).GetNormalized();
			{
				const FVector Toward = Velocities.Last().Size2D() > Prediction.VelocityDeadzone * 10.0f ? Velocities.Last() : (WantedSpeed > 0.0f ? PathDirection : FVector::ZeroVector);
				if (!Toward.IsNearlyZero())
				{
					TargetFacing = (FQuat(FVector::UpVector, FMath::Atan2(Toward.Y, Toward.X)) * MeshRelative.GetRotation()).GetNormalized();
				}
			}
			SpringMath::CriticalSpringDamperQuatPredict(TArrayView<FQuat>(Facings), TArrayView<FVector>(AngularVelocities), NumPredictions,
				Facing, State.AngularVelocity, TargetFacing, Prediction.FacingSmoothingTime, TrajectoryParams.PredictionSamplingInterval);

			// The "current" sample at t = DeltaTime, then the predictions.
			{
				FTransformTrajectorySample& Sample = Trajectory.Trajectory.Samples[TrajectoryParams.NumHistorySamples];
				Sample.TimeInSeconds = DeltaTime;
				Sample.Position = Current.GetLocation() + Velocity * DeltaTime;
				Sample.Facing = Facing;
			}
			for (int32 Index = 0; Index < NumPredictions; ++Index)
			{
				FTransformTrajectorySample& Sample = Trajectory.Trajectory.Samples[TrajectoryParams.NumHistorySamples + 1 + Index];
				Sample.TimeInSeconds = DeltaTime + (Index + 1) * TrajectoryParams.PredictionSamplingInterval;
				Sample.Position = Positions[Index];
				Sample.Facing = Facings[Index];
			}
			Trajectory.SteeringTarget = Trajectory.Trajectory.GetSampleAtTime(DeltaTime).Facing;
		}
	});
}
