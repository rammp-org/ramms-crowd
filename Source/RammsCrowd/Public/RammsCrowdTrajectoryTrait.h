// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "MassCharacterTrajectoryFragments.h"
#include "MassEntityTraitBase.h"
#include "MassProcessor.h"
#include "RammsCrowdTrajectoryTrait.generated.h"

/** The spring state behind a crowd agent's predicted trajectory (see URammsCrowdTrajectoryTrait). */
USTRUCT()
struct FRammsCrowdTrajectoryStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Smoothed acceleration, from the change in the agent's velocity. */
	FVector Acceleration = FVector::ZeroVector;
	/** Last frame's velocity and facing, to measure the change. */
	FVector PreviousVelocity = FVector::ZeroVector;
	FQuat PreviousFacing = FQuat::Identity;
	/** Smoothed angular velocity (axis * rad/s). */
	FVector AngularVelocity = FVector::ZeroVector;
	bool bInitialized = false;
};

/** Settings for the prediction. */
USTRUCT()
struct FRammsCrowdTrajectoryParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()

	/** How long the predicted velocity takes to settle on what Mass is steering toward, s. */
	UPROPERTY(EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.01"))
	float VelocitySmoothingTime = 0.15f;

	/** How long the predicted facing takes to settle on the steering's facing, s. */
	UPROPERTY(EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.01"))
	float FacingSmoothingTime = 0.15f;

	/** Below this speed the prediction is a standstill, cm/s. */
	UPROPERTY(EditAnywhere, Category = "Prediction", meta = (ClampMin = "0.0"))
	float VelocityDeadzone = 1.0f;
};

/**
 * Predicts each crowd agent's trajectory for motion matching, into the engine's
 * FCharacterTrajectoryFragment (UE 5.8 MassCharacterTrajectory, the fragment the
 * MetaHuman Crowd reads), in the mesh's space: the recorded path behind, then a
 * critically damped spring (Epic's SpringMath) from the agent's current velocity and
 * acceleration toward the velocity Mass is steering it to, after avoidance.
 *
 * This is the engine's Spring Movement + Character Trajectory Generation pair with
 * one difference: the spring predicts but does not move the agent. Epic's spring
 * drives the body, which adds its smoothing time of lag that avoidance cannot see;
 * in a crowd that tripled the close contacts. Here Mass moves the agent as usual
 * and the prediction starts from what it actually did. The engine's own generation
 * processor is switched off in DefaultMass.ini so it doesn't overwrite this one.
 *
 * Add to the crowd's entity config. Offset is the mesh-to-agent rotation (the crowd
 * bodies' representation yaw offset, -90 degrees).
 */
UCLASS(meta = (DisplayName = "RAMMS Crowd Trajectory"))
class RAMMSCROWD_API URammsCrowdTrajectoryTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	UPROPERTY(EditAnywhere, Category = "Trajectory")
	FCharacterTrajectoryParameters TrajectoryParameters;

	UPROPERTY(EditAnywhere, Category = "Trajectory")
	FRammsCrowdTrajectoryParameters Prediction;
};

UCLASS()
class RAMMSCROWD_API URammsCrowdTrajectoryProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrowdTrajectoryProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
