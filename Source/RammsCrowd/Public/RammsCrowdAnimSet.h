// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RammsCrowdAnimSet.generated.h"

class UAnimSequence;
class UStaticMesh;

/** One looping locomotion clip and the ground motion it was authored with. */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsCrowdLocomotionClip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TObjectPtr<UAnimSequence> Sequence;

	/** Ground speed of the clip's root motion, cm/s. Playback is scaled so the feet match the agent's real speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd", meta = (ClampMin = "1.0"))
	float SpeedCmS = 140.0f;

	/** Direction of travel relative to facing, degrees: 0 forward, +90 right, -90 left, 180 back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float DirectionDegrees = 0.0f;
};

/** Which arms (and whether the head) a carry pose is layered onto. */
UENUM(BlueprintType)
enum class ERammsCrowdCarryArms : uint8
{
	Left,
	Right,
	Both,
};

/** Something a pedestrian carries: the hold pose layered over their arms, and the prop. */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsCrowdCarryStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	FName Name;

	/** Relative chance among this body's carry styles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Looping hold pose; only the masked arms (and head) are taken from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TObjectPtr<UAnimSequence> Pose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	ERammsCrowdCarryArms Arms = ERammsCrowdCarryArms::Left;

	/** Also take the neck and head from the pose (looking down at a phone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	bool bLayerHead = false;

	/** One is picked per pedestrian; empty for none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UStaticMesh>> Props;

	/** Bone or socket on the leader mesh the prop attaches to (weapon_l, weapon_r, Accessory_Backpack). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	FName Socket;
};

/** The animations for one body type (gender and build). */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsCrowdBodyAnims
{
	GENERATED_BODY()

	/**
	 * Matched against the leader mesh name, e.g. "f_nrw" for f_tal_nrw_base (City
	 * Sample naming: gender, height, build). The first entry is the fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	FString BodyKey;

	/** Forward walk variants; one is picked per pedestrian. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<FRammsCrowdLocomotionClip> Walks;

	/** Forward brisk walk, blended in above walk speed. Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	FRammsCrowdLocomotionClip QuickWalk;

	/** Standing idles; one is picked per pedestrian. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UAnimSequence>> Idles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<FRammsCrowdCarryStyle> Carries;
};

/**
 * Animation content for URammsCrowdAnimInstance: per-body walks, idles and carry
 * styles, shared directional walks and reactions, and the tuning that turns Mass
 * movement into a pose. Built for the City Sample crowd by
 * Content/Python/ramms_crowd_build_anim_set.py.
 */
UCLASS(BlueprintType)
class RAMMSCROWD_API URammsCrowdAnimSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Content")
	TArray<FRammsCrowdBodyAnims> Bodies;

	/**
	 * Walks in directions other than forward (sidesteps, diagonals, backwards), shared
	 * by every body. Blended with the body's forward walk by the angle between
	 * facing and travel, so a pedestrian side-stepping the robot steps sideways
	 * instead of sliding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Content")
	TArray<FRammsCrowdLocomotionClip> Directionals;

	/** Upper-body flinches played when a pedestrian is startled by or bumps the robot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Content")
	TArray<TObjectPtr<UAnimSequence>> Reactions;

	/** Chance a pedestrian carries something (phone, cup, bag), 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Variety", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CarryChance = 0.5f;

	/** Name of the actor's static mesh component that holds the carried prop; created if missing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Variety")
	FName AccessoryComponentName = TEXT("Accessory");

	/** Slowest and fastest playback of a walk; outside this the feet slide rather than the gait looking wrong. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.1"))
	float MinPlayRate = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.1"))
	float MaxPlayRate = 1.6f;

	/** Below this ground speed the pedestrian stands (idle); walk blends in up to IdleBlendEndCmS. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float IdleBlendStartCmS = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float IdleBlendEndCmS = 45.0f;

	/** Smoothing time constants for measured speed and travel direction, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float SpeedSmoothingSeconds = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float DirectionSmoothingSeconds = 0.1f;

	/**
	 * Chance a pedestrian who is merely aware of the robot (outside the react radius)
	 * watches it; inside the react radius everyone does. Mass look-at targets, when a
	 * StateTree sets one, take precedence.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Look At", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WatchRobotChance = 0.6f;

	/** Height above the stimulus location to look at (the seated user's face), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Look At")
	float LookAtStimulusHeightCm = 100.0f;

	/** Head turn limits and speed when looking at a target (the robot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Look At", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float LookAtMaxYawDegrees = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Look At", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float LookAtMaxPitchDegrees = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Look At", meta = (ClampMin = "0.01"))
	float LookAtBlendSeconds = 0.35f;

	/** Seconds before the same pedestrian can react again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Reactions", meta = (ClampMin = "0.0"))
	float ReactionCooldownSeconds = 3.0f;

	/** Peak weight of a reaction over the upper body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Reactions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReactionWeight = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Reactions", meta = (ClampMin = "0.0"))
	float ReactionBlendInSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Reactions", meta = (ClampMin = "0.0"))
	float ReactionBlendOutSeconds = 0.4f;

	/** React to a startle (the robot approaching fast) as well as to contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Reactions")
	bool bReactToStartle = true;

	/** The entry whose BodyKey occurs in MeshName, else the first entry, else null. */
	const FRammsCrowdBodyAnims* FindBody(const FString& MeshName) const;
};
