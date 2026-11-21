// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityConfigAsset.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	#include "MassEntityHandle.h"
#else
	#include "Mass/EntityHandle.h" // UE 5.8 moved it into MassCore
#endif
#include "RammsTrafficTypes.h"
#include "RammsTrafficSpawner.generated.h"

class ARammsTrafficVehicleActor;
class URammsTrafficVehicleTrait;

/**
 * Puts Mass traffic on the level's ZoneGraph vehicle lanes when play begins (as soon as the ZoneGraph is in):
 * VehicleCount vehicles (or DensityPerKm per km of road lane), at least MinSpawnGap apart and
 * MinSpawnDistance from the player and every PlayerStart, each with a car mesh picked from CarMeshes by weight
 * and its own desired speed around the speed limit -- all deterministic for a Seed. Each vehicle is a Mass
 * entity (URammsTrafficVehicleTrait) with one ARammsTrafficVehicleActor as its body.
 *
 * Placed by the level builder (RammsWorldGen traffic.py), which fills CarMeshes from the palette's Car slot.
 */
UCLASS(BlueprintType)
class RAMMSCROWD_API ARammsTrafficSpawner : public AActor
{
	GENERATED_BODY()

public:
	ARammsTrafficSpawner();

	/** How many vehicles. 0: DensityPerKm decides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0"))
	int32 VehicleCount = 24;

	/** Vehicles per km of road lane, when VehicleCount is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0"))
	float DensityPerKm = 12.0f;

	/** Seeds the spawn places, the car picks, desired speeds and route choices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	int32 Seed = 1;

	/** No vehicle starts closer than this to a player pawn or a PlayerStart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "cm"))
	float MinSpawnDistance = 2500.0f;

	/** Spacing of the spawn places along a lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "500.0", Units = "cm"))
	float MinSpawnGap = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	FRammsTrafficDrivingParams Driving;

	/** The cars (the level builder copies the palette's Car slot). Empty: car-sized boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	TArray<FRammsTrafficCarMesh> CarMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	TSubclassOf<ARammsTrafficVehicleActor> VehicleActorClass;

	/** Vehicles collide (Chaos, CPU traces) within this distance of a player pawn; 0 = always. GPU ray tracing sees them everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "cm"))
	float CollisionRadius = 6000.0f;

	/** Vehicles trace the road under their axles (pitch, crowns, humps) within this distance of a player pawn; 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundTraceRadius = 6000.0f;

	/** ZoneGraph tags (Project Settings > Zone Graph) of vehicle lanes, intersection lanes and signalized intersections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Lanes")
	FName VehicleTagName = FName(TEXT("Vehicle"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Lanes")
	FName IntersectionTagName = FName(TEXT("Intersection"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic|Lanes")
	FName SignalizedTagName = FName(TEXT("Signalized"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	bool bSpawnOnBeginPlay = true;

	/** Spawn now (replacing any traffic it spawned before). False when there are no vehicle lanes yet, or when Mass is
	 *  processing (called from a tick that runs alongside it): it then spawns on its own next tick. */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Traffic")
	bool SpawnTraffic();

	/** Removes the traffic it spawned (while Mass is processing, the entities go when the frame's commands are flushed). */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Traffic")
	void DespawnTraffic();

	UFUNCTION(BlueprintPure, Category = "Ramms|Traffic")
	int32 GetSpawnedCount() const { return Entities.Num(); }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<URammsTrafficVehicleTrait> RuntimeTrait = nullptr;

	UPROPERTY(Transient)
	FMassEntityConfig RuntimeConfig;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARammsTrafficVehicleActor>> Actors;

	TArray<FMassEntityHandle> Entities;
	float WaitedSeconds = 0.0f;
	bool bWarnedNoLanes = false;
	/** The last SpawnTraffic found Mass processing: the next tick tries again. */
	bool bWaitingForMass = false;
};
