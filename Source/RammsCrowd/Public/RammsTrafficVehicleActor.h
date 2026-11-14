// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RammsTrafficTypes.h"
#include "RammsTrafficVehicleActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * What a traffic vehicle looks like and collides as. The root is the car's facing frame (front +X, base centre
 * on the road), moved kinematically every frame by URammsTrafficVisualProcessor (no teleport, so the physics
 * bodies get a kinematic target and push with the car's real velocity):
 *
 *  - Body: the car mesh, placed with its palette offsets; movable, so it casts dynamic shadows, writes motion
 *    vectors and is in the ray tracing scene (GPU sensors see it). Query-only collision (object type Vehicle):
 *    line and sweep traces (CPU ToF / sonar, line of sight) hit the mesh itself.
 *  - Collision: a box over the car's footprint and height, physics only (object type Vehicle), so the Chaos
 *    robot bumps into a solid, cheap kinematic shape rather than a moving triangle mesh.
 *
 * Collision is switched off away from the player (FRammsTrafficVisualFragment::CollisionRadius). GetVelocity
 * reports the Mass vehicle's velocity. Components are tagged seg:Car for CameraCapture segmentation.
 */
UCLASS(NotPlaceable, Transient)
class RAMMSCROWD_API ARammsTrafficVehicleActor : public AActor
{
	GENERATED_BODY()

public:
	ARammsTrafficVehicleActor();

	/** Give it a car: mesh, its placement in the facing frame, and the collision box. */
	void SetupVehicle(const FRammsTrafficCarMesh& Car);

	void SetCollisionActive(bool bActive);
	bool IsCollisionActive() const { return bCollisionActive; }

	void SetReportedVelocity(const FVector& InVelocity) { ReportedVelocity = InVelocity; }
	virtual FVector GetVelocity() const override { return ReportedVelocity; }

	UStaticMeshComponent* GetBody() const { return Body; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Ramms|Traffic")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Ramms|Traffic")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Ramms|Traffic")
	TObjectPtr<UBoxComponent> Collision;

	/** A car-sized box when the spawner has no car meshes. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> FallbackMesh = nullptr;

private:
	FVector ReportedVelocity = FVector::ZeroVector;
	bool bCollisionActive = true;
};
