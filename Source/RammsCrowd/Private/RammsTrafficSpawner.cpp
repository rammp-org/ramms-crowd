// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficSpawner.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityTemplate.h"
#include "MassEntityUtils.h"
#include "MassSpawnerSubsystem.h"
#include "RammsCrowdLog.h"
#include "RammsTrafficSubsystem.h"
#include "RammsTrafficVehicleActor.h"
#include "RammsTrafficVehicleTrait.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

ARammsTrafficSpawner::ARammsTrafficSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Spawning and destroying entities is synchronous Mass work: tick where no Mass processing phase runs (Mass runs
	// its phases in parallel with the actor ticks of their tick groups).
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	VehicleActorClass = ARammsTrafficVehicleActor::StaticClass();
}

void ARammsTrafficSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (bSpawnOnBeginPlay)
	{
		// The ZoneGraph and Mass may come up after this actor: try every tick until the lanes are there.
		WaitedSeconds = 0.0f;
		SetActorTickEnabled(true);
	}
}

void ARammsTrafficSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DespawnTraffic();
	Super::EndPlay(EndPlayReason);
}

void ARammsTrafficSpawner::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	WaitedSeconds += DeltaSeconds;
	if (SpawnTraffic())
	{
		SetActorTickEnabled(false);
		return;
	}
	if (bWaitingForMass)
	{
		return; // Mass was mid-frame: again next tick
	}
	if (WaitedSeconds > 10.0f && !bWarnedNoLanes)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Traffic: still no ZoneGraph lanes tagged '%s' after %.0f s (build the traffic lanes, then the ZoneGraph); waiting."),
			*GetName(), *VehicleTagName.ToString(), WaitedSeconds);
		bWarnedNoLanes = true;
	}
	if (WaitedSeconds > 120.0f)
	{
		SetActorTickEnabled(false);
	}
}

void ARammsTrafficSpawner::DespawnTraffic()
{
	for (ARammsTrafficVehicleActor* Actor : Actors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Actors.Reset();
	if (Entities.Num() > 0)
	{
		UWorld* World = GetWorld();
		UMassSpawnerSubsystem* MassSpawner = World != nullptr ? UWorld::GetSubsystem<UMassSpawnerSubsystem>(World) : nullptr;
		FMassEntityManager* EntityManager = World != nullptr ? UE::Mass::Utils::GetEntityManager(World) : nullptr;
		if (EntityManager != nullptr)
		{
			TArray<FMassEntityHandle> Valid;
			for (const FMassEntityHandle& Entity : Entities)
			{
				if (EntityManager->IsEntityValid(Entity))
				{
					Valid.Add(Entity);
				}
			}
			if (Valid.Num() > 0)
			{
				if (EntityManager->IsProcessing() || MassSpawner == nullptr)
				{
					// Mid-frame (called from another actor's tick while Mass runs in parallel): destroy them when the
					// frame's commands are flushed, never leave them driving without a body.
					TSharedPtr<FMassCommandBuffer> Commands = MakeShared<FMassCommandBuffer>();
					Commands->DestroyEntities(Valid);
					EntityManager->AppendCommands(Commands);
				}
				else
				{
					MassSpawner->DestroyEntities(Valid);
				}
			}
		}
		Entities.Reset();
	}
}

bool ARammsTrafficSpawner::SpawnTraffic()
{
	UWorld* World = GetWorld();
	URammsTrafficSubsystem* Traffic = World != nullptr ? UWorld::GetSubsystem<URammsTrafficSubsystem>(World) : nullptr;
	UMassSpawnerSubsystem* MassSpawner = World != nullptr ? UWorld::GetSubsystem<UMassSpawnerSubsystem>(World) : nullptr;
	const UZoneGraphSubsystem* ZoneGraph = World != nullptr ? UWorld::GetSubsystem<UZoneGraphSubsystem>(World) : nullptr;
	const FMassEntityManager* Manager = World != nullptr ? UE::Mass::Utils::GetEntityManager(World) : nullptr;
	bWaitingForMass = false;
	if (Traffic == nullptr || MassSpawner == nullptr || ZoneGraph == nullptr || Manager == nullptr)
	{
		return false;
	}
	if (Manager->IsProcessing())
	{
		// Called while Mass runs (a Blueprint in another actor's tick): spawning now is not allowed; next tick.
		bWaitingForMass = true;
		SetActorTickEnabled(true);
		return false;
	}
	Traffic->SetTagNames(VehicleTagName, IntersectionTagName, SignalizedTagName);
	// The widest car decides which intersection lanes are too close to be used at once.
	float WidestCar = 0.0f;
	for (const FRammsTrafficCarMesh& Car : CarMeshes)
	{
		WidestCar = FMath::Max(WidestCar, Car.Mesh != nullptr ? static_cast<float>(FMath::Max(Car.Footprint.Y, 150.0)) : 0.0f);
	}
	Traffic->SetMaxVehicleWidth(WidestCar > 0.0f ? WidestCar : 190.0f);
	const FRammsTrafficNetwork* Net = Traffic->GetNetwork();
	if (Net == nullptr)
	{
		return false;
	}
	DespawnTraffic();

	// The vehicles' template: this spawner's driving parameters on a vehicle trait (rebuilt, so a respawn picks up
	// changes), inside their editor ranges whatever set them.
	const FRammsTrafficDrivingParams Params = Driving.Sanitized();
	if (RuntimeTrait == nullptr)
	{
		RuntimeTrait = NewObject<URammsTrafficVehicleTrait>(this, TEXT("RuntimeVehicleTrait"), RF_Transient);
	}
	RuntimeTrait->Driving = Params;
	RuntimeConfig.DestroyEntityTemplate(*World);
	RuntimeConfig = FMassEntityConfig(*this);
	RuntimeConfig.AddTrait(*RuntimeTrait);
	const FMassEntityTemplate& Template = RuntimeConfig.GetOrCreateEntityTemplate(*World);
	if (!Template.IsValid())
	{
		UE_LOG(LogRammsCrowd, Error, TEXT("[%s] Traffic: could not build the vehicle entity template."), *GetName());
		return true;
	}

	// Spawn places: along every road lane (not the intersections), MinSpawnGap apart, away from the player and the starts.
	TArray<FVector> Avoid;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pawn = It->Get() != nullptr ? It->Get()->GetPawn() : nullptr)
		{
			Avoid.Add(Pawn->GetActorLocation());
		}
	}
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Avoid.Add(It->GetActorLocation());
	}
	struct FPlace
	{
		int32 Lane = INDEX_NONE;
		float Distance = 0.0f;
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
	};
	TArray<FPlace> Places;
	double LaneCm = 0.0;
	const float Gap = FMath::Max(MinSpawnGap, 500.0f);
	for (int32 LaneIndex = 0; LaneIndex < Net->Lanes.Num(); ++LaneIndex)
	{
		const FRammsTrafficLane& Lane = Net->Lanes[LaneIndex];
		const FZoneGraphStorage* Storage = ZoneGraph->GetZoneGraphStorage(Lane.Handle.DataHandle);
		if (Lane.Zone != INDEX_NONE || Storage == nullptr)
		{
			continue;
		}
		LaneCm += Lane.Length;
		for (float Distance = Gap * 0.5f; Distance < Lane.Length - 400.0f; Distance += Gap)
		{
			FZoneGraphLaneLocation Location;
			if (!UE::ZoneGraph::Query::CalculateLocationAlongLane(*Storage, Lane.Handle, Distance, Location))
			{
				continue;
			}
			bool bNear = false;
			for (const FVector& A : Avoid)
			{
				bNear |= FVector::DistSquared2D(A, Location.Position) < FMath::Square(static_cast<double>(MinSpawnDistance));
			}
			if (!bNear)
			{
				Places.Add(FPlace{ LaneIndex, Distance, Location.Position, Location.Direction });
			}
		}
	}
	FRandomStream Rng(Seed);
	for (int32 i = Places.Num() - 1; i > 0; --i)
	{
		Places.Swap(i, Rng.RandRange(0, i));
	}
	const int32 Wanted = VehicleCount > 0 ? VehicleCount : FMath::RoundToInt32(DensityPerKm * LaneCm / 100000.0);
	const int32 Count = FMath::Min(Wanted, Places.Num());
	if (Count <= 0)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Traffic: no room for vehicles (%d wanted, %d places on %.2f km of lanes)."), *GetName(), Wanted, Places.Num(), LaneCm / 100000.0);
		return true;
	}

	// Car picks by weight.
	float TotalWeight = 0.0f;
	for (const FRammsTrafficCarMesh& Car : CarMeshes)
	{
		TotalWeight += Car.Mesh != nullptr ? FMath::Max(Car.Weight, 0.0f) : 0.0f;
	}
	auto PickCar = [this, &Rng, TotalWeight]() -> int32
	{
		if (TotalWeight <= 0.0f)
		{
			return INDEX_NONE;
		}
		float Pick = Rng.FRand() * TotalWeight;
		int32 Last = INDEX_NONE;
		for (int32 i = 0; i < CarMeshes.Num(); ++i)
		{
			if (CarMeshes[i].Mesh == nullptr || CarMeshes[i].Weight <= 0.0f)
			{
				continue;
			}
			Last = i;
			Pick -= CarMeshes[i].Weight;
			if (Pick <= 0.0f)
			{
				return i;
			}
		}
		return Last;
	};

	TSharedPtr<FMassEntityManager::FEntityCreationContext> Creation = MassSpawner->SpawnEntities(Template, Count, Entities);
	FMassEntityManager& EntityManager = MassSpawner->GetEntityManagerChecked();
	const TSubclassOf<ARammsTrafficVehicleActor> ActorClass = VehicleActorClass != nullptr ? VehicleActorClass : TSubclassOf<ARammsTrafficVehicleActor>(ARammsTrafficVehicleActor::StaticClass());
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FPlace& Place = Places[i];
		const int32 Visual = PickCar();
		const FRammsTrafficCarMesh Car = CarMeshes.IsValidIndex(Visual) ? CarMeshes[Visual] : FRammsTrafficCarMesh();

		FRammsTrafficVehicleFragment& V = EntityManager.GetFragmentDataChecked<FRammsTrafficVehicleFragment>(Entities[i]);
		V.Lane = Place.Lane;
		V.NextLane = INDEX_NONE;
		V.NetworkVersion = Net->Version;
		V.Distance = Place.Distance;
		V.HalfLength = static_cast<float>(FMath::Max(Car.Footprint.X, 300.0) * 0.5);
		V.HalfWidth = static_cast<float>(FMath::Max(Car.Footprint.Y, 150.0) * 0.5);
		V.DesiredSpeed = Params.SpeedLimit * (1.0f + Params.SpeedVariation * Rng.FRandRange(-1.0f, 1.0f));
		V.Speed = V.DesiredSpeed * Rng.FRandRange(0.3f, 0.8f);
		V.RandomState = HashCombine(GetTypeHash(Seed), GetTypeHash(i)) | 1u;
		V.Visual = Visual;
		V.bTeleport = true;

		const FTransform Transform(Place.Direction.Rotation(), Place.Position);
		EntityManager.GetFragmentDataChecked<FTransformFragment>(Entities[i]).SetTransform(Transform);

		FRammsTrafficVisualFragment& VisualFragment = EntityManager.GetFragmentDataChecked<FRammsTrafficVisualFragment>(Entities[i]);
		VisualFragment.CollisionRadius = CollisionRadius;
		VisualFragment.GroundTraceRadius = GroundTraceRadius;
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		ARammsTrafficVehicleActor* Actor = World->SpawnActor<ARammsTrafficVehicleActor>(ActorClass, Transform, SpawnParams);
		if (Actor != nullptr)
		{
			Actor->SetupVehicle(Car);
#if WITH_EDITOR
			Actor->SetActorLabel(FString::Printf(TEXT("TrafficVehicle%02d"), i));
#endif
			VisualFragment.Actor = Actor;
			VisualFragment.bCollisionOn = true;
			Actors.Add(Actor);
		}
	}
	Creation.Reset();

	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Traffic: %d vehicles (%d wanted) on %.2f km of road lanes, %d car meshes, speed limit %.0f km/h, seed %d."),
		*GetName(), Entities.Num(), Wanted, LaneCm / 100000.0, CarMeshes.Num(), Params.SpeedLimit * 0.036f, Seed);
	return true;
}
