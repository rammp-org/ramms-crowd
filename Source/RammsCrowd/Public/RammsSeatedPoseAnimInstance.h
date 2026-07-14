// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RammsSeatedPoseAnimInstance.generated.h"

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

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	/**
	 * Self-configuration: occupant blueprints (City Sample) re-create anim
	 * instances during deferred assembly, wiping externally-pushed offsets. On
	 * every (re)initialization, pull the pose from the owning seat component by
	 * walking the attachment chain.
	 */
	virtual void NativeInitializeAnimation() override;

	friend struct FRammsSeatedPoseProxy;
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
};
