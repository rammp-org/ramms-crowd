// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RammsCrowdAnimSet.generated.h"

class UAnimSequence;
class UPoseSearchDatabase;
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
 * Pose search databases for crowd motion matching, by what the pedestrian is doing.
 * Searching only the set that fits the moment (as the Game Animation Sample's
 * chooser does) keeps costs from different schemas from competing.
 */
USTRUCT(BlueprintType)
struct RAMMSCROWD_API FRammsCrowdMotionDatabases
{
	GENERATED_BODY()

	/** Standing: idles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UPoseSearchDatabase>> Stand;

	/** Standing but turning to face somewhere else: turns in place (searched with Stand). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UPoseSearchDatabase>> TurnInPlace;

	/** Setting off from standing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UPoseSearchDatabase>> Start;

	/** Walking: loops, turns, pivots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UPoseSearchDatabase>> Move;

	/** Coming to a stop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd")
	TArray<TObjectPtr<UPoseSearchDatabase>> Stop;

	bool IsEmpty() const { return Stand.IsEmpty() && TurnInPlace.IsEmpty() && Start.IsEmpty() && Move.IsEmpty() && Stop.IsEmpty(); }
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

	/**
	 * Stride warping: when the agent walks faster or slower than the clip, cover part
	 * of the difference by lengthening or shortening the steps along the direction of
	 * travel (legs re-solved with IK) instead of only changing playback speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion")
	bool bStrideWarping = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float MinStrideScale = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "1.0", ClampMax = "1.6"))
	float MaxStrideScale = 1.25f;

	/**
	 * Foot lift scaling: a slow walker lifts and rotates its feet less than the fast
	 * walk the clip was captured at. Below the clip's speed, the foot's height above
	 * the ground and its heel-rise / toe-lift rotation are scaled toward flat by the
	 * speed ratio (not below MinFootLiftScale).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion")
	bool bScaleFootLift = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MinFootLiftScale = 0.45f;

	/**
	 * Foot locking: when the animation plants a foot, keep it where it touched down in
	 * the world and bend the leg (two-bone IK) to reach it until the clip lifts the
	 * foot again. The pinned point rolls heel to ball like a real step. Contact comes
	 * from the clips' contact_l / contact_r curves when they have them (GASP clips
	 * do), else from foot heights. Removes the sliding left when Mass moves the body at a different
	 * speed or direction than the clip's feet (most noticeable in motion-matched
	 * starts, stops and turns).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion")
	bool bLockPlantedFeet = true;

	/** A foot counts as planted while its ball is below this height (component space, cm); it releases a few cm above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float FootPlantHeightCm = 5.0f;

	/**
	 * Once the body has moved this far from a locked foot (a standing pedestrian
	 * nudged by avoidance, a walk slower or faster than the clip), the foot takes a
	 * procedural step back under the body instead of stretching the leg or sliding, cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float FootLockMaxDistanceCm = 12.0f;

	/** Duration and height of that step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.05"))
	float FootStepSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Locomotion", meta = (ClampMin = "0.0"))
	float FootStepHeightCm = 7.0f;

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

	/**
	 * Motion matching (experimental): when set, locomotion comes from searching pose
	 * search databases (built on the crowd skeleton) for the clip and frame that best
	 * continue the current pose along the pedestrian's predicted path, instead of
	 * blending the per-body walk and directional clips. Starts, stops, turns and
	 * pivots then come from captured motion. Carry, reactions and look-at layer on
	 * top as before. See Content/Python/ramms_crowd_build_motion_matching.py.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	FRammsCrowdMotionDatabases MotionMatching;

	/** Switch motion matching off without clearing the databases. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	bool bUseMotionMatching = true;

	/** Below this speed (now and over the predicted path) a pedestrian counts as standing, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.0"))
	float MotionMatchingStandSpeedCmS = 25.0f;

	/** Seconds between searches (the current clip keeps playing in between). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.0"))
	float MotionMatchingSearchInterval = 0.1f;

	/** Crossfade into a newly selected clip, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.0"))
	float MotionMatchingBlendSeconds = 0.25f;

	/** How quickly the predicted path bends from the current velocity to Mass's desired velocity, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.01"))
	float TrajectoryResponseSeconds = 0.35f;

	/** Clip playback is scaled toward the agent's speed within this range, so the feet keep up without obvious slow motion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.1"))
	float MotionMatchingMinPlayRate = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching", meta = (ClampMin = "0.1"))
	float MotionMatchingMaxPlayRate = 1.2f;

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
