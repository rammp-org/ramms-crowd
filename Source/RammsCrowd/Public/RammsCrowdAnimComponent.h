// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityHandle.h"
#include "RammsCrowdAnimComponent.generated.h"

class UAnimationAsset;
class UAnimSequenceBase;
class UBlendSpace;
class ULODSyncComponent;
class USkeletalMeshComponent;

/**
 * Bridge from Mass simulation data to the AnimBP of a high-LOD crowd representation
 * actor (BP_CrowdPerson). Each tick it re-resolves the owning actor's Mass entity —
 * representation actors are POOLED and recycled, so a cached handle goes stale — and
 * mirrors velocity/movement intent into BlueprintReadOnly properties.
 *
 * AnimBP usage: drive the idle<->walk blendspace with MassSpeed, gate locomotion on
 * bMassIsMoving, and (optionally) head aim with LookAtDirection/bHasLookAtTarget
 * (pushed by URammsCrowdLookAtSyncProcessor).
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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FMassEntityHandle ResolveEntityHandle();
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
