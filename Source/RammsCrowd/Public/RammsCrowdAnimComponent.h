// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityHandle.h"
#include "RammsCrowdAnimComponent.generated.h"

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FMassEntityHandle ResolveEntityHandle();

	FMassEntityHandle CachedHandle;
};
