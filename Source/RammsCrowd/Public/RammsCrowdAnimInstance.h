// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RammsCrowdAnimInstance.generated.h"

class UAnimSequence;
class URammsCrowdAnimComponent;
class URammsCrowdAnimSet;
struct FRammsCrowdBodyAnims;
struct FRammsCrowdCarryStyle;
struct FRammsCrowdMotionMatching;

namespace UE::PoseSearch
{
	struct FPoseHistory;
}

namespace RammsCrowdAnim
{
	/**
	 * Blend between the two clips whose travel directions bracket AngleDegrees, going
	 * round the circle (so 170° blends the 135° and 180° clips, -170° the -135° and
	 * 180° ones). Returns false if Directions is empty; OutB == OutA when one clip covers it.
	 */
	RAMMSCROWD_API bool BracketDirection(TConstArrayView<float> Directions, float AngleDegrees, int32& OutA, int32& OutB, float& OutAlphaB);

	/**
	 * A clip's gait cycles, from its "L" sync markers: cycle k runs from CycleStarts[k]
	 * to the next start (wrapping to the first plus the length). Every locomotion clip
	 * shares one gait phase 0..1, so blended clips put the same foot down together
	 * whatever their length or number of steps.
	 */
	struct RAMMSCROWD_API FGaitTrack
	{
		TArray<float> CycleStarts;
		float Length = 0.0f;
		int32 Cycle = 0;

		/** Reads the clip's "L" markers; a clip without any is one cycle starting at 0. */
		void Init(const UAnimSequence* Sequence);
		void InitFromMarkers(TConstArrayView<float> LeftFootTimes, float InLength);
		float CycleDuration() const;
		float TimeAt(float Phase) const;
		void NextCycle();
	};
}

/** One sampled clip of the frame's pose. */
struct FRammsCrowdPoseSample
{
	const UAnimSequence* Sequence = nullptr;
	float Time = 0.0f;
	float Weight = 0.0f;
};

/** Everything the worker thread needs to build one frame's pose, prepared on the game thread. */
struct FRammsCrowdAnimFrame
{
	TArray<FRammsCrowdPoseSample, TInlineAllocator<4>> Locomotion;
	FRammsCrowdPoseSample Idle;
	/** 0 standing (idle) .. 1 walking (locomotion). */
	float LocomotionAlpha = 0.0f;
	FRammsCrowdPoseSample Carry;
	float CarryLeftArm = 0.0f;
	float CarryRightArm = 0.0f;
	float CarryHead = 0.0f;
	/** Weight is the current blend over the upper body. */
	FRammsCrowdPoseSample Reaction;
	/** Head turn in component space, degrees, and its weight. */
	float LookYawDegrees = 0.0f;
	float LookPitchDegrees = 0.0f;
	float LookAlpha = 0.0f;
	/** Motion matching: the pose history to record the finished pose into (owned by the instance). */
	UE::PoseSearch::FPoseHistory* PoseHistory = nullptr;
	bool bResetPoseHistory = false;
	/** Pin planted feet in the world (see URammsCrowdAnimSet::bLockPlantedFeet); reset when the actor becomes someone else. */
	bool bLockFeet = false;
	bool bResetFootLocks = false;
	/** Stride warping: step length scale along the agent's velocity (world, cm/s). */
	float StrideScale = 1.0f;
	FVector AgentVelocity = FVector::ZeroVector;
	/** Foot lift / rotation scale (1 = as captured). */
	float FootLiftScale = 1.0f;
};

/**
 * Graph-less anim instance for City Sample style crowd pedestrians, driven by Mass
 * through URammsCrowdAnimComponent and the content in a URammsCrowdAnimSet:
 *
 *  - Variety: each pedestrian gets its body type's walk variant, idle and, by
 *    chance, something to carry (phone, cup, bag, briefcase) with the prop on its
 *    hand or back. Picked per Mass entity, so a pooled actor recycled onto another
 *    entity becomes someone else.
 *  - Locomotion without foot sliding: the forward walk, brisk walk and directional
 *    walks are blended by travel direction relative to facing and by speed, kept on
 *    one gait phase through their foot sync markers, and played back at the rate
 *    that makes the clip's root speed equal the agent's measured speed.
 *  - Carry poses layered over just the holding arm(s); the head turns toward the
 *    robot once it is noticed (or a Mass look-at target); an upper-body flinch when
 *    the robot startles or bumps the pedestrian.
 *
 * URammsCrowdAnimComponent installs it on the actor's leader mesh when given an
 * anim set, so no Animation Blueprint asset is involved.
 */
UCLASS(Transient)
class RAMMSCROWD_API URammsCrowdAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	TObjectPtr<URammsCrowdAnimSet> AnimSet;

	/** What this pedestrian ended up with (for debugging and tests). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FString BodyKey;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FName WalkName;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FName IdleName;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	FName CarryName;

	/** Playback rate of the locomotion clips last frame (1 = as authored). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float LocomotionPlayRate = 1.0f;

	/** Ground speed the blended clips' feet move at, cm/s, after rate scaling; compare with the agent's speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float FootSpeedCmS = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float LocomotionAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 ReactionCount = 0;

	/** Motion matching: whether it drives locomotion, the clip it last selected, and how many clips it has switched to. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	bool bMotionMatching = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	FName MotionMatchedClip;

	/** Stand, Start, Move or Stop: which databases are being searched. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	FName MotionPhase;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd|Motion Matching")
	int32 MotionMatchSwitches = 0;

	/** Feet currently pinned to the ground (0..2), for debugging and tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 LockedFeet = 0;

	/** Largest foot-lock correction applied last frame, cm, and how many locks were let go because the body moved too far. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float FootLockCorrectionCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 FootLockOverstretches = 0;

	/** How many feet took their contact from the clip's contact curves last frame (0..2; else foot heights). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	int32 FootContactFromCurves = 0;

	/** Foot lift and heel / toe rotation scale applied last frame (1 = as captured). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float FootLiftScale = 1.0f;

	/** Step length scale applied last frame (stride warping; 1 = as captured). */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Crowd")
	float StrideScale = 1.0f;

	/** Plays a reaction now (ignores the cooldown). */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	void PlayReaction();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	struct FLocoClip
	{
		const UAnimSequence* Sequence = nullptr;
		float SpeedCmS = 0.0f;
		float DirectionDegrees = 0.0f;
		RammsCrowdAnim::FGaitTrack Gait;
	};

	void SelectStyle(uint32 Seed);
	void ApplyAccessory(const FRammsCrowdCarryStyle* Style, int32 PropIndex);
	void UpdateLocomotion(float DeltaSeconds, float Speed, float DirectionDegrees);
	/** Motion-matched locomotion into Frame; false when motion matching isn't available (falls back to the blended clips). */
	bool UpdateMotionMatching(float DeltaSeconds);
	void UpdateReaction(float DeltaSeconds);
	void UpdateLookAt(float DeltaSeconds);
	void StartReaction();

	TWeakObjectPtr<URammsCrowdAnimComponent> Bridge;
	uint64 SelectedEntityKey = MAX_uint64;
	FRandomStream Random;

	/** 0 forward walk, 1 brisk walk (may be empty), then the directionals. */
	TArray<FLocoClip> LocoClips;
	TArray<float> DirectionalAngles;
	float GaitPhase = 0.0f;
	float SmoothedSpeed = 0.0f;
	float SmoothedDirection = 0.0f;

	const UAnimSequence* IdleSequence = nullptr;
	float IdleTime = 0.0f;

	const UAnimSequence* CarrySequence = nullptr;
	float CarryTime = 0.0f;
	float CarryLeft = 0.0f;
	float CarryRight = 0.0f;
	float CarryHead = 0.0f;

	const UAnimSequence* ReactionSequence = nullptr;
	float ReactionTime = 0.0f;
	double LastReactionWorldTime = -1.0e9;
	int32 SeenContactCount = -1;
	float SeenStartleTime = 0.0f;
	bool bSeenStimulus = false;

	/** Watches the robot from the aware zone (rolled per pedestrian). */
	bool bWatchesRobot = false;
	float LookYaw = 0.0f;
	float LookPitch = 0.0f;
	float LookAlpha = 0.0f;

	FRammsCrowdAnimFrame Frame;

	TSharedPtr<FRammsCrowdMotionMatching> MotionMatching;
	bool bPendingFootReset = true;

	friend struct FRammsCrowdAnimProxy;
};

/** Builds the pose from the instance's FRammsCrowdAnimFrame (no anim graph). */
struct FRammsCrowdAnimProxy : public FAnimInstanceProxy
{
	FRammsCrowdAnimProxy() = default;
	explicit FRammsCrowdAnimProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** One foot's lock: where it was planted in the world and how much of the correction applies. */
	struct FFootLock
	{
		bool bLocked = false;
		FVector LockedWorld = FVector::ZeroVector;
		/** Last correction in component space; fades out after release. */
		FVector Offset = FVector::ZeroVector;
		float Alpha = 0.0f;
		/** 0..1 through a procedural step, or < 0 when not stepping. */
		float StepTime = -1.0f;
		FVector StepFrom = FVector::ZeroVector;
		/** The pinned point on the foot (in the foot bone's space): the heel, then the ball. */
		FVector PinLocal = FVector::ZeroVector;
		bool bPinBall = false;
	};

	/** Stride warping and foot locking, on the finished locomotion pose. */
	void AdjustFeet(FCompactPose& Pose, const FBlendedCurve& Curve);

	FRammsCrowdAnimFrame Frame;
	FFootLock FootLocks[2];
	int32 LockedFeet = 0;
	float FootLockCorrection = 0.0f;
	int32 FootLockOverstretches = 0;
	int32 FootContactFromCurves = 0;
	float RefAnkleHeight = 0.0f;
	float RefBallHeight = 0.0f;
	/** Pitch of the ankle-to-ball line in the reference pose, degrees (negative: ball below ankle). */
	float RefFootPitch = 0.0f;
};
