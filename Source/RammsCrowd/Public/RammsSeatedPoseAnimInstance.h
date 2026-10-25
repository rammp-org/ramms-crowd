// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RammsSeatedPoseAnimInstance.generated.h"

class URammsSeatComponent;

/** One limb placement for the proxy, in the occupant mesh's component space. */
struct FRammsSeatLimbSolve
{
	bool bEnabled = false;
	FName Upper;
	FName Lower;
	FName End;
	/** Target for the end bone; when RelativeToBone is set, an offset from that bone. */
	FVector Location = FVector::ZeroVector;
	FName RelativeToBone;
	FVector Pole = FVector::ZeroVector;
	bool bKeepEndRotation = false;
	float Weight = 1.0f;
};

/** Upper-body motion for the proxy: angles in degrees about the seat's axes (given in component space). */
struct FRammsSeatMotion
{
	FVector SeatForward = FVector::ForwardVector;
	FVector SeatRight = FVector::RightVector;
	FVector SeatUp = FVector::UpVector;
	/** Passive lean: + forward, + toward the occupant's right. */
	float SwayPitch = 0.0f;
	float SwayRoll = 0.0f;
	/** Share of the lean the neck and head take back to keep the gaze level. */
	float HeadRighting = 0.0f;
	/** Breathing: chest pitch and shoulder rise. */
	float Breath = 0.0f;
	/** Head turn: + right, + up. */
	float LookYaw = 0.0f;
	float LookPitch = 0.0f;
	/** Slow voluntary trunk shift. */
	float TrunkYaw = 0.0f;
};

/** Seat fit for the proxy: pelvis alignment and limb placements, component space. */
struct FRammsSeatFit
{
	bool bAlignPelvis = false;
	FVector PelvisLocation = FVector::ZeroVector;
	FRammsSeatLimbSolve Limbs[4];
	FRammsSeatMotion Motion;
};

/**
 * Graph-less anim instance that produces a parametric static pose: the reference
 * pose with per-bone local-space rotation offsets applied. Used by
 * URammsSeatComponent to sit humanoid skeletal meshes (City Sample, mannequin —
 * any skeleton using standard bone names) without needing authored sit animations.
 * Bones missing from the target skeleton are skipped, so one offset map can serve
 * multiple skeletons.
 */
UCLASS()
class RAMMSCROWD_API URammsSeatedPoseAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Local-space rotation offsets applied on top of the reference pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat")
	TMap<FName, FRotator> BoneOffsets;

	/** Pure: puts a two-bone chain's end at Effector, bending toward Pole. Positions and rotations in one space. */
	static void SolveLimb(FTransform& Upper, FTransform& Lower, FTransform& End, const FVector& Effector, const FVector& Pole, bool bKeepEndRotation);

	/** Current passive lean (degrees) and head turn, for debugging and tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Seat")
	float SwayPitchDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Seat")
	float SwayRollDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Seat")
	float LookYawDegrees = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ramms|Seat")
	float LookPitchDegrees = 0.0f;

	/**
	 * Pure: one step of the passive lean, a damped spring driven by the seat's
	 * acceleration along that axis (cm/s^2; the body lags it). Angle in radians.
	 */
	static void StepSway(float& InOutAngle, float& InOutRate, float Acceleration, float TrunkControl, float DeltaSeconds);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	/**
	 * Self-configuration: occupant blueprints (City Sample) re-create anim
	 * instances during deferred assembly, wiping externally-pushed offsets. On
	 * every (re)initialization, pull the pose from the owning seat component by
	 * walking the attachment chain.
	 */
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	friend struct FRammsSeatedPoseProxy;

private:
	void UpdateMotion(const URammsSeatComponent& SeatComp, const FTransform& SeatToComponent, float DeltaSeconds);

	TWeakObjectPtr<URammsSeatComponent> Seat;
	FRammsSeatFit Fit;

	// Passive lean state.
	bool bHaveSeatHistory = false;
	FVector PrevSeatLocation = FVector::ZeroVector;
	FVector SeatVelocity = FVector::ZeroVector;
	FVector SeatAccel = FVector::ZeroVector;
	float PrevSeatYaw = 0.0f;
	float SeatYawRate = 0.0f;
	float SwayPitch = 0.0f;
	float SwayPitchRate = 0.0f;
	float SwayRoll = 0.0f;
	float SwayRollRate = 0.0f;

	// Voluntary motion state.
	FRandomStream MotionRandom;
	float Time = 0.0f;
	float GlanceUntil = 0.0f;
	float NextGlance = 3.0f;
	float GlanceYaw = 0.0f;
	float GlancePitch = 0.0f;
	float NextPeopleCheck = 0.0f;
	float PeopleCooldownUntil = 0.0f;
	TWeakObjectPtr<AActor> WatchedPerson;
	float WatchUntil = 0.0f;
	float LookYaw = 0.0f;
	float LookPitch = 0.0f;
};

/** Proxy that evaluates the parametric pose directly (no anim graph). */
struct FRammsSeatedPoseProxy : public FAnimInstanceProxy
{
	FRammsSeatedPoseProxy() = default;
	explicit FRammsSeatedPoseProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** Game-thread copy of the instance's offsets, taken in PreUpdate. */
	TArray<TPair<FName, FQuat>> Offsets;
	FRammsSeatFit Fit;
};
