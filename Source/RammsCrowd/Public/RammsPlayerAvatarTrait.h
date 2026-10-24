// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTypes.h"
#include "MassTranslator.h"
#include "RammsPlayerAvatarTrait.generated.h"

/** Tags entities that mirror an actor (the player robot) into Mass via URammsPlayerAvatarTranslator. */
USTRUCT()
struct FRammsPlayerAvatarTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * The robot's footprint as crowd agents see it: where it sits on the actor and how
 * much room agents keep from it (URammsRobotClearanceProcessor).
 */
USTRUCT()
struct RAMMSCROWD_API FRammsAvatarFootprintFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Footprint centre in the actor's frame (cm; X forward, Y right). */
	FVector2D LocalOffset = FVector2D::ZeroVector;

	/** Fit the collider to the actor's colliding components on the first sync. */
	bool bAutoFootprint = true;
	bool bResolved = false;
	float PaddingCm = 5.0f;

	/** Keep crowd agents outside the footprint (hard boundary + approach band). */
	bool bKeepCrowdClear = true;
	float ClearanceMarginCm = 10.0f;
	float ApproachBandCm = 80.0f;
};

/** Finite-differencing state for the avatar translator. */
USTRUCT()
struct FRammsAvatarSyncFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector PreviousLocation = FVector::ZeroVector;
	bool bHasPrevious = false;
};

/**
 * Makes an actor-backed Mass entity (created by UMassAgentComponent on e.g. the MeBot
 * wheelchair pawn) visible to crowd avoidance: transform, velocity, agent radius, and
 * avoidance collider, synced actor->Mass every frame by URammsPlayerAvatarTranslator.
 *
 * Unlike the engine's capsule/character sync traits this works for any pawn — the
 * MeBot's root is a physics-driven skeletal mesh with no capsule or CharacterMovement.
 * Pair with UMassNavigationObstacleTrait (avoidance grid) and UMassLookAtTargetTrait
 * (gaze target) in the player avatar entity config.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "RAMMS Player Avatar"))
class RAMMSCROWD_API URammsPlayerAvatarTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** Avoidance footprint radius, cm (~wheelchair half-width incl. clearance). */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "1.0"))
	float AgentRadiusCm = 50.0f;

	/** Use a pill (capsule-on-its-side) collider to represent the wheelchair length. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	bool bUsePillCollider = true;

	/** Pill half-length along the forward axis, cm. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "1.0", EditCondition = "bUsePillCollider"))
	float PillHalfLengthCm = 55.0f;

	/**
	 * Fit the avoidance collider to the robot on its first sync: the box of the actor's
	 * physically colliding components in its own frame (arm, casters and mounts; not
	 * query-only triggers) gives the footprint centre, a pill along the longer side and
	 * its radius. Replaces
	 * AgentRadiusCm / PillHalfLengthCm, which stay the fallback when the box is empty.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	bool bAutoFootprint = true;

	/** Added to the fitted footprint radius, cm. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "0.0", EditCondition = "bAutoFootprint"))
	float FootprintPaddingCm = 5.0f;

	/**
	 * Treat the footprint as solid for crowd agents (URammsRobotClearanceProcessor):
	 * agents never enter it, slide around it, and yield when the robot drives at them.
	 * Without it only Mass avoidance steers them, which does not stop an agent whose
	 * lane runs through a stopped robot.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	bool bKeepCrowdClear = true;

	/** Gap kept between an agent's radius and the footprint, cm. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "0.0", EditCondition = "bKeepCrowdClear"))
	float ClearanceMarginCm = 10.0f;

	/** Distance beyond the clearance over which agents start turning around the robot, cm. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "0.0", EditCondition = "bKeepCrowdClear"))
	float ApproachBandCm = 80.0f;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

/**
 * Copies the avatar actor's transform + velocity into Mass fragments each frame
 * (actor -> Mass), at the footprint centre, and fits the footprint once if asked.
 */
UCLASS()
class RAMMSCROWD_API URammsPlayerAvatarTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	URammsPlayerAvatarTranslator();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
