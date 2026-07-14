// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "RammsSeatComponent.generated.h"

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

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ConfigureOccupantActor(AActor* Actor);
	void ApplyOccupantOffset();
	USkeletalMeshComponent* GetOccupantLeaderMesh() const;
	void EnforcePoseTick();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Occupant;

	/**
	 * Occupant blueprints (City Sample) finish assembling asynchronously — late
	 * mesh swaps / anim-mode changes silently clear the pose instance. For a few
	 * seconds after spawn the pose is re-asserted periodically.
	 */
	FTimerHandle PoseEnforceTimer;
	int32 PoseEnforceTicksRemaining = 0;
};
