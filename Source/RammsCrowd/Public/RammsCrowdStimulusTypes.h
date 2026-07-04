// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "RammsCrowdStimulusTypes.generated.h"

/** Proxemics zone an NPC is in relative to the nearest stimulus source (e.g. the player robot). */
UENUM(BlueprintType)
enum class ERammsStimulusZone : uint8
{
	None	UMETA(DisplayName = "None"),
	Aware	UMETA(DisplayName = "Aware"),
	React	UMETA(DisplayName = "React"),
	Startle UMETA(DisplayName = "Startle")
};

/**
 * Per-entity perception result written by URammsCrowdStimulusProcessor and read by
 * StateTree conditions/evaluators/tasks. Entities get this fragment from
 * URammsCrowdReactionTrait.
 */
USTRUCT()
struct RAMMSCROWD_API FRammsStimulusFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Mass entity of the stimulus source (e.g. player avatar puppet); target for look-at tasks. */
	FMassEntityHandle StimulusEntity;

	/** World location of the nearest stimulus source. */
	FVector StimulusLocation = FVector::ZeroVector;

	/** XY-normalized direction pointing from the stimulus toward this entity (i.e. the away direction). */
	FVector AwayDirection = FVector::ForwardVector;

	/** Surface distance (centers minus radii) to the nearest stimulus, cm. FLT_MAX when no stimulus. */
	float Distance = FLT_MAX;

	/** Closing speed along the separation axis, cm/s. Positive = approaching. */
	float ClosingSpeed = 0.0f;

	/** Normalized stimulus strength 0..1 (distance falloff x source strength scale). */
	float Strength = 0.0f;

	/** Current proxemics zone (hysteresis and startle rule already applied). */
	ERammsStimulusZone Zone = ERammsStimulusZone::None;

	/** Zone during the previous perception update. */
	ERammsStimulusZone PreviousZone = ERammsStimulusZone::None;

	/** World time (s) the current zone was entered. */
	float ZoneEnterTime = 0.0f;

	/** World time (s) the startle zone was last entered; drives the startle cooldown. */
	float LastStartleTime = -FLT_MAX;
};

/**
 * Per-archetype reaction tuning baked from URammsCrowdReactionProfile by
 * URammsCrowdReactionTrait. Const-shared: one copy per profile.
 */
USTRUCT()
struct RAMMSCROWD_API FRammsReactionParamsFragment : public FMassConstSharedFragment
{
	GENERATED_BODY()

	/** NPC notices the stimulus (glance/acknowledge) within this range, cm. */
	UPROPERTY(EditAnywhere, Category = "Proxemics", meta = (ClampMin = "0.0"))
	float AwareRadiusCm = 600.0f;

	/** NPC actively reacts (yield/sidestep) within this range, cm. */
	UPROPERTY(EditAnywhere, Category = "Proxemics", meta = (ClampMin = "0.0"))
	float ReactRadiusCm = 300.0f;

	/** Personal-space violation range, cm. */
	UPROPERTY(EditAnywhere, Category = "Proxemics", meta = (ClampMin = "0.0"))
	float StartleRadiusCm = 120.0f;

	/** Zone exit distance multiplier (hysteresis) to avoid flip-flopping at boundaries. */
	UPROPERTY(EditAnywhere, Category = "Proxemics", meta = (ClampMin = "1.0"))
	float ExitHysteresis = 1.15f;

	/** Approach speed (cm/s) that triggers a startle while inside the React zone. */
	UPROPERTY(EditAnywhere, Category = "Startle", meta = (ClampMin = "0.0"))
	float StartleClosingSpeedCmS = 150.0f;

	/** Minimum time between startle activations, s. */
	UPROPERTY(EditAnywhere, Category = "Startle", meta = (ClampMin = "0.0"))
	float StartleCooldownSeconds = 4.0f;

	/** How far a startled NPC backs away, cm. Keep small (<= 200) so lane re-entry stays cheap. */
	UPROPERTY(EditAnywhere, Category = "Response", meta = (ClampMin = "0.0"))
	float BackAwayDistanceCm = 150.0f;

	/** Desired-speed multiplier while backing away. */
	UPROPERTY(EditAnywhere, Category = "Response", meta = (ClampMin = "0.1"))
	float BackAwaySpeedScale = 1.4f;

	/** Desired-speed multiplier while fleeing. */
	UPROPERTY(EditAnywhere, Category = "Response", meta = (ClampMin = "0.1"))
	float FleeSpeedScale = 1.8f;
};

namespace UE::RammsCrowd::Signals
{
/** Raised (in addition to NewStateTreeTaskRequired) whenever an entity's proxemics zone changes. */
const FName StimulusZoneChanged = FName(TEXT("RammsStimulusZoneChanged"));
/** Raised when an entity enters the Startle zone. */
const FName Startled = FName(TEXT("RammsStartled"));
} // namespace UE::RammsCrowd::Signals
