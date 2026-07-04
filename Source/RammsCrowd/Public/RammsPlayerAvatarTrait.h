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

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

/** Copies the avatar actor's transform + velocity into Mass fragments each frame (actor -> Mass). */
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
