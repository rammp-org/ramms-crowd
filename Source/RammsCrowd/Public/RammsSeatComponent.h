// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "RammsSeatComponent.generated.h"

class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
#if WITH_EDITOR
class FProperty;
struct FPropertyChangedEvent;
#endif

UENUM(BlueprintType)
enum class ERammsSeatOccupantMode : uint8
{
	/** Spawn OccupantActorClass (default: the City Sample crowd character). */
	ActorClass,
	/** Spawn a bare skeletal mesh actor using OccupantMesh. */
	ExplicitMesh,
};

/**
 * Puts a posed skeletal-mesh occupant into a seat. Add to any actor (wheelchair,
 * bench, vehicle), position the component at the seat surface (its transform is
 * the seat origin: X forward, Z up), and it spawns/attaches an occupant on
 * BeginPlay — City Sample characters (optionally appearance-randomized) or any
 * skeletal mesh — collision-disabled and posed by the parametric seated pose
 * (URammsSeatedPoseAnimInstance; per-bone offsets editable live on this
 * component). Editor preview via the Spawn/Clear Preview buttons.
 */
UCLASS(ClassGroup = (Ramms), meta = (BlueprintSpawnableComponent))
class RAMMSCROWD_API URammsSeatComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	URammsSeatComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat")
	ERammsSeatOccupantMode OccupantMode = ERammsSeatOccupantMode::ActorClass;

	/**
	 * Occupant actor class for ActorClass mode. Defaults to the City Sample crowd
	 * character (soft reference — resolved at spawn; if the pack is not installed
	 * the seat stays empty and logs a warning).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat", meta = (EditCondition = "OccupantMode == ERammsSeatOccupantMode::ActorClass"))
	TSoftClassPtr<AActor> OccupantActorClass;

	/** Re-roll the City Sample appearance per spawn (sets the BP's RandomOptions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat", meta = (EditCondition = "OccupantMode == ERammsSeatOccupantMode::ActorClass"))
	bool bRandomizeAppearance = true;

	/** Skeletal mesh for ExplicitMesh mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat", meta = (EditCondition = "OccupantMode == ERammsSeatOccupantMode::ExplicitMesh"))
	TSoftObjectPtr<USkeletalMesh> OccupantMesh;

	/**
	 * Transform of the occupant (its mesh root, at the feet) relative to this
	 * component. Applied live — edits move the current occupant immediately.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat")
	FTransform OccupantOffset = FTransform::Identity;

	/**
	 * The parametric seated pose: local-space rotation offsets per bone, applied
	 * over the reference pose. Defaults approximate a relaxed sit for UE-standard
	 * humanoid skeletons — tune per seat in the details panel (live in PIE).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Pose")
	TMap<FName, FRotator> PoseBoneOffsets;

	/** Spawns (or respawns) the occupant and applies the pose. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Ramms|Seat")
	void SpawnOccupant();

	/** Removes the current occupant. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Ramms|Seat")
	void ClearOccupant();

	/** Re-applies PoseBoneOffsets to the current occupant. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Ramms|Seat")
	void ApplyPose();

	UFUNCTION(BlueprintPure, Category = "Ramms|Seat")
	AActor* GetOccupant() const { return Occupant; }

	// ── Head tracking ───────────────────────────────────────────────────────
	//
	// A first-person camera authored on the owning actor sits where the author
	// guessed a head would be. Occupants vary -- that is the point of the City
	// Sample crowd -- so for most of them it is in the wrong place. This moves a
	// named component of the OWNER onto the occupant's head bone instead.
	//
	// Nothing is re-parented. The component stays owned by, and attached under,
	// whatever it was authored on; only its world LOCATION is driven each frame.
	// That keeps camera discovery working -- URammsRobotCameraComponent
	// enumerates Owner->GetComponents<UCameraComponent>(), which is about
	// ownership -- and, more importantly, leaves its ROTATION the vehicle's
	// business, which is what anything aiming the rig assumes.

	/** Drive TrackedComponentName from the occupant's head bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking")
	bool bTrackOccupantHead = false;

	/**
	 * Component on the OWNING actor to put on the head -- typically the
	 * first-person camera. Any USceneComponent; it stays owned by the owner, so
	 * anything that finds it by owner still finds it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking", meta = (EditCondition = "bTrackOccupantHead"))
	FName TrackedComponentName;

	/**
	 * Bone to attach to. Falls back through HeadBoneFallbacks when the occupant's
	 * skeleton does not have this one, because the crowd does not guarantee one
	 * skeleton.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking", meta = (EditCondition = "bTrackOccupantHead"))
	FName HeadBoneName = FName("head");

	/** Tried in order when HeadBoneName is absent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking", meta = (EditCondition = "bTrackOccupantHead"))
	TArray<FName> HeadBoneFallbacks = { FName("Head"), FName("head_01"), FName("neck_01"), FName("Neck") };

	/**
	 * Where the eyes are relative to the head BONE.
	 *
	 * The bone sits inside the skull, not at the eyes, and its axes are not a
	 * camera's: measured on the City Sample crowd skeleton, bone +Y points the
	 * way the face does and bone +X points up through the skull. The default
	 * rotation is the permutation that turns those into a camera's forward and
	 * up -- pitch 0, yaw 90, roll -90, derived by sweeping orientations against
	 * the actor's own forward vector -- and the default location is 8 cm forward
	 * and 6 cm up from the bone, which lands at the eyes.
	 *
	 * The first sweep got this wrong in a way worth recording: it built
	 * candidates with unreal.Rotator(pitch, yaw, roll) from Python, and that
	 * constructor actually takes (roll, pitch, yaw). Every candidate was
	 * therefore a different rotation than its label, and the "winner" shipped
	 * here produced a camera rolled 77 degrees. Build rotators by named field
	 * when it matters.
	 *
	 * One offset serves every occupant sharing a skeleton AND this seat's pose --
	 * not a skeleton alone. PoseBoneOffsets rotates the head bone, so the same
	 * skeleton posed differently needs a different offset; measured on an unposed
	 * occupant the shipped default comes out rolled onto its side. Tune it once
	 * per seat, with occupants in the pose that seat applies.
	 *
	 * Note the camera therefore looks where the HEAD looks, idle head turns
	 * included, rather than always straight ahead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking", meta = (EditCondition = "bTrackOccupantHead"))
	FTransform HeadSocketOffset = FTransform(FRotator(0.0f, 90.0f, -90.0f), FVector(6.0f, 8.0f, 0.0f));

	/**
	 * Take the head's ORIENTATION as well as its position.
	 *
	 * Off by default, and that default is about the controls rather than taste.
	 * Anything that aims this component does so in its PARENT's space --
	 * URammsRobotCameraComponent::Orbit adds to the spring arm's relative yaw,
	 * clamps its relative pitch and zeroes its relative roll -- and all of that
	 * was written for a parent aligned with the vehicle, and reparenting a rig
	 * onto a head bone whose axes are permuted turns "yaw" about a sideways axis
	 * and levels "roll = 0" against the skull.
	 *
	 * With this off, rotation is simply never written. The component keeps its
	 * authored parent and parent-relative rotation, so it still inherits the
	 * vehicle's heading and every control keeps meaning what it meant, while its
	 * position comes from the bone -- the eyes move with whoever is sitting
	 * there.
	 *
	 * Not the same as making the rotation absolute, which was the first attempt:
	 * that also stopped the rig inheriting the VEHICLE's rotation, so turning
	 * the chair moved the camera with the head and left the view facing its old
	 * heading.
	 *
	 * Turn it on for a view that genuinely follows the occupant's gaze, and
	 * expect anything that orbits it to need rewriting in head space.
	 * HeadSocketOffset's rotation is only used when this is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Seat|Head Tracking", meta = (EditCondition = "bTrackOccupantHead"))
	bool bTrackHeadRotation = false;

	/** Start driving the tracked component from the occupant's head, or report
	 *  why it cannot. Despite the name it attaches nothing -- see the class
	 *  comment -- and is kept for callers that already use it. */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Seat|Head Tracking")
	bool AttachTrackedComponentToHead();

	/** Stop, and put it back where it was authored. */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Seat|Head Tracking")
	void DetachTrackedComponentFromHead();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The bone actually used, or NAME_None when not attached. */
	UFUNCTION(BlueprintPure, Category = "Ramms|Seat|Head Tracking")
	FName GetResolvedHeadBone() const { return ResolvedHeadBone; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void					ConfigureOccupantActor(AActor* Actor);
	void					ApplyOccupantOffset();
	USkeletalMeshComponent* GetOccupantLeaderMesh() const;
	void					EnforcePoseTick();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Occupant;

	/**
	 * Occupant blueprints (City Sample) finish assembling asynchronously — late
	 * mesh swaps / anim-mode changes silently clear the pose instance. For a few
	 * seconds after spawn the pose is re-asserted periodically.
	 */
	FTimerHandle PoseEnforceTimer;
	int32		 PoseEnforceTicksRemaining = 0;

	/** The owner's component named by TrackedComponentName, found once. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TrackedComponent;

	/** Where the tracked component sat before it was driven, so stopping restores
	 *  it rather than leaving it at the last head pose. */
	FTransform TrackedComponentOriginalRelative = FTransform::Identity;

	/** The name TrackedComponent was resolved from, so a later change to
	 *  TrackedComponentName re-resolves instead of silently driving the old one. */
	FName ResolvedComponentName = NAME_None;

	bool bDrivingTrackedComponent = false;

	FName ResolvedHeadBone = NAME_None;

	/** The owner's component named by TrackedComponentName, re-resolved when that
	 *  name changes. Null, and logged, when there is no such component. */
	USceneComponent* ResolveTrackedComponent();

	/** Put the tracked component at the head for this frame. */
	void UpdateTrackedComponentFromHead();

	/** Stop driving it and restore its authored transform. */
	void StopDrivingTrackedComponent();

	/** First of HeadBoneName / HeadBoneFallbacks the mesh actually has. */
	FName ResolveHeadBone(const USkeletalMeshComponent* Mesh) const;
};
