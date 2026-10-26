// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityHandle.h"
#include "RammsCrowdStimulusTypes.h"
#include "RammsCrowdAnimComponent.generated.h"

class UAnimationAsset;
class UAnimSequenceBase;
class UBlendSpace;
class ULODSyncComponent;
class URammsCrowdAnimSet;
class USkeletalMeshComponent;

/**
 * Bridge from Mass simulation data to the AnimBP of a high-LOD crowd representation
 * actor (BP_CrowdPerson). Each tick it re-resolves the owning actor's Mass entity —
 * representation actors are POOLED and recycled, so a cached handle goes stale — and
 * mirrors velocity/movement intent into BlueprintReadOnly properties.
 *
 * With an AnimSet it installs URammsCrowdAnimInstance on the actor's leader mesh,
 * which reads these properties (variety, speed-matched directional walks, carried
 * props, head look-at, reactions). Without one it falls back to driving a single
 * blend space or idle/walk pair itself. A mesh with its own Animation Blueprint class
 * can read the same properties instead.
 */
UCLASS(ClassGroup = (Ramms), meta = (BlueprintSpawnableComponent))
class RAMMSCROWD_API URammsCrowdAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URammsCrowdAnimComponent();

	/** Mass velocity of the entity this actor currently represents, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FVector MassVelocity = FVector::ZeroVector;

	/** The velocity Mass is steering toward (path following, avoidance), cm/s; where the pedestrian is about to go. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FVector MassDesiredVelocity = FVector::ZeroVector;

	/** Where the pedestrian's path heads next (unit, 2D), the speed it wants on it, cm/s, and how far its current goal is, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FVector MassPathDirection = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float MassDesiredSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float MassDistanceToGoal = 0.0f;

	/** True when the pedestrian means to stand once it reaches its goal. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	bool bMassStopsAtGoal = false;

	/**
	 * How far the pedestrian itself still travels before it stands, cm (large when it
	 * isn't stopping). Mass's DistanceToGoal is measured from its steering target,
	 * which runs ahead of the agent; this is from the agent, and includes the final
	 * approach Mass makes after the target has arrived.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float MassDistanceToStop = 1.0e6f;

	/** 2D speed, cm/s — feed the walk blendspace with this. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float MassSpeed = 0.0f;

	/** True while the entity's current movement action is Move. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	bool bMassIsMoving = false;

	/** Per-kind gait variation scalar from FRammsCrowdProfileParameters (stride/cadence noise). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float GaitVariation = 0.25f;

	/** World-space look-at direction pushed by URammsCrowdLookAtSyncProcessor. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FVector LookAtDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	bool bHasLookAtTarget = false;

	/** Called by the look-at sync processor; not intended for gameplay use. */
	void SetLookAt(const FVector& InDirection, bool bInHasTarget);

	/** True while the entity perceives a stimulus (the robot) in any proxemic zone. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	bool bHasStimulus = false;

	/** The proxemic zone the stimulus is in (aware, react, startle). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	ERammsStimulusZone StimulusZone = ERammsStimulusZone::None;

	/** World location of the perceived stimulus. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FVector StimulusLocation = FVector::ZeroVector;

	/** World time of the entity's last startle (robot closing fast inside the startle radius). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float LastStartleTime = 0.0f;

	/** How many times the robot has bumped this entity (see URammsRobotClearanceProcessor). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 RobotContactCount = 0;

	/** Side the entity plans to pass the robot on: +1 its right, -1 its left, 0 none (see URammsRobotClearanceProcessor::PlanPass). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 RobotPassSide = 0;

	/** The Mass entity this (pooled) actor represents this frame. */
	FMassEntityHandle GetRepresentedEntity() const { return CachedHandle; }

	/**
	 * Animation content for URammsCrowdAnimInstance. When set, the component installs
	 * that anim instance on the leader mesh at begin play and the single-node driver
	 * below stands down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Animation")
	TObjectPtr<URammsCrowdAnimSet> AnimSet;

	/**
	 * When true (and the sibling mesh is NOT using an Animation Blueprint), this
	 * component drives the mesh's single-node animation directly: idle/walk selection
	 * from Mass movement state, play rate synced to MassSpeed, and per-entity
	 * phase/rate jitter from GaitVariation. A proper AnimBP (blendspace + head aim)
	 * supersedes all of this: set the mesh to Use Animation Blueprint and this logic
	 * steps aside automatically.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation")
	bool bDriveSingleNodeAnimation = true;

	/**
	 * Preferred: a locomotion blend space (X = signed direction deg -180..180 relative
	 * to facing, Y = ground speed cm/s). When set it supersedes IdleAnim/WalkAnim —
	 * the component plays it as a single node and feeds (direction, speed) each tick,
	 * getting blended turns/starts/stops without an Animation Blueprint.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** Fallback pair used when no LocomotionBlendSpace is set. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation")
	TObjectPtr<UAnimSequenceBase> IdleAnim;

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation")
	TObjectPtr<UAnimSequenceBase> WalkAnim;

	/** World-space direction of travel relative to character facing, degrees (-180..180). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float DirectionAngleDegrees = 0.0f;

	/** Ground speed (cm/s) the walk animation was authored for; play rate = MassSpeed / this. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation", meta = (ClampMin = "1.0"))
	float WalkAnimReferenceSpeedCmS = 140.0f;

	/** Below this speed the idle animation plays. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|Single Node Animation", meta = (ClampMin = "0.0"))
	float IdleSpeedThresholdCmS = 10.0f;

	/**
	 * Clamp a sibling LODSync component's LOD by distance to the nearest Mass viewer.
	 * City Sample crowd meshes strip arm bones at their higher LODs (far crowd was
	 * designed for baked vertex animation), which leaves arms frozen at reference
	 * pose — this keeps full-skeleton LODs within visual/sensor range.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|LOD")
	bool bManageLODSyncByDistance = true;

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|LOD", meta = (ClampMin = "0.0"))
	float ForceLOD0DistanceCm = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|LOD", meta = (ClampMin = "0.0"))
	float ForceLOD1DistanceCm = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd|LOD", meta = (ClampMin = "0.0"))
	float ForceLOD2DistanceCm = 6000.0f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FMassEntityHandle ResolveEntityHandle();
	USkeletalMeshComponent* ResolveDrivenMesh();
	void InstallAnimInstance();
	void UpdateDirection();
	void UpdateDrivenAnimation();
	void ManageLODSync(float ViewerDistanceCm);

	FMassEntityHandle CachedHandle;
	TWeakObjectPtr<USkeletalMeshComponent> DrivenMesh;
	TWeakObjectPtr<ULODSyncComponent> LODSync;
	TObjectPtr<UAnimationAsset> CurrentAnim;
	/** Entity index the gait jitter/phase was seeded for; re-seeded when the pooled actor is recycled. */
	int32 SeededEntityIndex = INDEX_NONE;
	float GaitRateScale = 1.0f;
};
