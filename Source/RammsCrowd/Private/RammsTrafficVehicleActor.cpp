// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficVehicleActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Bumper height: the collision box starts this far above the road so it never rubs on it. */
	constexpr double BoxGroundClearanceCm = 15.0;
}

ARammsTrafficVehicleActor::ARammsTrafficVehicleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->ComponentTags.Add(FName(TEXT("seg:Car")));

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Root);
	Collision->SetMobility(EComponentMobility::Movable);
	Collision->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
	Collision->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);
	Collision->SetHiddenInGame(true);
	Collision->SetBoxExtent(FVector(225.0, 95.0, 70.0), false);
	Collision->SetRelativeLocation(FVector(0.0, 0.0, BoxGroundClearanceCm + 70.0));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		FallbackMesh = CubeFinder.Object;
	}
}

void ARammsTrafficVehicleActor::SetupVehicle(const FRammsTrafficCarMesh& Car)
{
	const double Length = FMath::Max(Car.Footprint.X, 100.0);
	const double Width = FMath::Max(Car.Footprint.Y, 80.0);
	const double Height = FMath::Max(static_cast<double>(Car.Height), 80.0);
	if (Car.Mesh != nullptr)
	{
		Body->SetStaticMesh(Car.Mesh);
		Body->SetRelativeTransform(FTransform(Car.RotationOffset, Car.LocationOffset, FVector(Car.Scale)));
	}
	else if (FallbackMesh != nullptr)
	{
		// No car meshes configured: a car-sized box (the engine cube is 100 cm, centred on its pivot).
		Body->SetStaticMesh(FallbackMesh);
		Body->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, Height * 0.5), FVector(Length, Width, Height) / 100.0));
	}
	const double BoxHalfHeight = FMath::Max((Height - BoxGroundClearanceCm) * 0.5, 20.0);
	Collision->SetBoxExtent(FVector(Length * 0.5, Width * 0.5, BoxHalfHeight), false);
	Collision->SetRelativeLocation(FVector(0.0, 0.0, BoxGroundClearanceCm + BoxHalfHeight));
}

void ARammsTrafficVehicleActor::SetCollisionActive(const bool bActive)
{
	if (bActive == bCollisionActive)
	{
		return;
	}
	bCollisionActive = bActive;
	Body->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Collision->SetCollisionEnabled(bActive ? ECollisionEnabled::PhysicsOnly : ECollisionEnabled::NoCollision);
}
