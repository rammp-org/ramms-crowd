// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficSignalClock.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MassCrowdSettings.h"
#include "MassCrowdSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "RammsCrowdLog.h"
#include "RammsTrafficSubsystem.h"
#include "ZoneGraphData.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphSubsystem.h"

ARammsTrafficSignalClock::ARammsTrafficSignalClock()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ARammsTrafficSignalClock::BeginPlay()
{
	Super::BeginPlay();
	if (URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(GetWorld()))
	{
		Traffic->RegisterSignalClock(this);
	}
	AddedHandle = UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.AddUObject(this, &ARammsTrafficSignalClock::HandleZoneGraphChanged);
	RemovedHandle = UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.AddUObject(this, &ARammsTrafficSignalClock::HandleZoneGraphChanged);
	if (SignalParameters == nullptr)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Traffic signal clock: no SignalParameters collection; the signal heads keep their resting look."), *GetName());
	}
	WarnIfNoWalk();
}

void ARammsTrafficSignalClock::WarnIfNoWalk() const
{
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		if (Timing.GetWalkSeconds(Axis) <= UE_KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Traffic signal clock: pedestrians walking along %s never get Walk (PedestrianClearanceSeconds %.1f s >= the parallel green %.1f s); their crossings stay closed and walkers wait at the kerb for good."),
				*GetName(), Axis == 0 ? TEXT("X") : TEXT("Y"), Timing.PedestrianClearanceSeconds, Axis == 0 ? Timing.GreenSecondsX : Timing.GreenSecondsY);
		}
	}
}

#if WITH_EDITOR
void ARammsTrafficSignalClock::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	WarnIfNoWalk();
}
#endif

void ARammsTrafficSignalClock::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.Remove(AddedHandle);
	UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.Remove(RemovedHandle);
	if (URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(GetWorld()))
	{
		Traffic->UnregisterSignalClock(this);
	}
	Super::EndPlay(EndPlayReason);
}

FRammsSignalState ARammsTrafficSignalClock::GetSignalStateAt(const double WorldTimeSeconds) const
{
	return FRammsSignalCycle::Evaluate(Timing, WorldTimeSeconds);
}

FRammsSignalState ARammsTrafficSignalClock::GetSignalState() const
{
	const UWorld* World = GetWorld();
	return GetSignalStateAt(World != nullptr ? World->GetTimeSeconds() : 0.0);
}

void ARammsTrafficSignalClock::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Time = World->GetTimeSeconds();
	const FRammsSignalState State = GetSignalStateAt(Time);
	PushParameters(State, Time);
	if (bControlCrossings)
	{
		UpdateCrossings(State);
	}
}

void ARammsTrafficSignalClock::PushParameters(const FRammsSignalState& State, const double Time)
{
	UWorld* World = GetWorld();
	if (SignalParameters == nullptr || World == nullptr)
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(SignalParameters);
	if (Instance == nullptr)
	{
		return;
	}
	const double Period = FMath::Max(HandFlashPeriod, 0.1f);
	const bool bBlinkOn = FMath::Fmod(Time, Period) < Period * 0.5;
	auto Lamp = [](const ERammsSignalLight Light, const ERammsSignalLight Which) { return Light == Which ? 1.0f : 0.0f; };
	auto Hand = [bBlinkOn](const ERammsWalkSignal Walk)
	{
		return Walk == ERammsWalkSignal::DontWalk ? 1.0f : Walk == ERammsWalkSignal::FlashingDontWalk ? (bBlinkOn ? 1.0f : 0.0f) : 0.0f;
	};
	// The pedestrian head on a pole whose vehicle heads face along X faces across the road those heads control:
	// it serves people walking along Y (and the other way round).
	const TPair<FName, float> Values[] = {
		TPair<FName, float>(FName(TEXT("GreenX")), Lamp(State.LightX, ERammsSignalLight::Green)),
		TPair<FName, float>(FName(TEXT("AmberX")), Lamp(State.LightX, ERammsSignalLight::Amber)),
		TPair<FName, float>(FName(TEXT("RedX")), Lamp(State.LightX, ERammsSignalLight::Red)),
		TPair<FName, float>(FName(TEXT("GreenY")), Lamp(State.LightY, ERammsSignalLight::Green)),
		TPair<FName, float>(FName(TEXT("AmberY")), Lamp(State.LightY, ERammsSignalLight::Amber)),
		TPair<FName, float>(FName(TEXT("RedY")), Lamp(State.LightY, ERammsSignalLight::Red)),
		TPair<FName, float>(FName(TEXT("WalkX")), State.WalkAlongY == ERammsWalkSignal::Walk ? 1.0f : 0.0f),
		TPair<FName, float>(FName(TEXT("HandX")), Hand(State.WalkAlongY)),
		TPair<FName, float>(FName(TEXT("WalkY")), State.WalkAlongX == ERammsWalkSignal::Walk ? 1.0f : 0.0f),
		TPair<FName, float>(FName(TEXT("HandY")), Hand(State.WalkAlongX)),
	};
	for (const TPair<FName, float>& Value : Values)
	{
		const float* Last = LastParameters.Find(Value.Key);
		if (Last == nullptr || *Last != Value.Value)
		{
			Instance->SetScalarParameterValue(Value.Key, Value.Value);
			LastParameters.Add(Value.Key, Value.Value);
		}
	}
}

void ARammsTrafficSignalClock::HandleZoneGraphChanged(const AZoneGraphData* ZoneGraphData)
{
	if (ZoneGraphData != nullptr && ZoneGraphData->GetWorld() == GetWorld())
	{
		bCrossingsCollected = false;
	}
}

bool ARammsTrafficSignalClock::CollectCrossings()
{
	Crossings.Reset();
	const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	const UMassCrowdSubsystem* Crowd = UWorld::GetSubsystem<UMassCrowdSubsystem>(GetWorld());
	if (CrowdSettings == nullptr || ZoneGraph == nullptr || Crowd == nullptr)
	{
		return false;
	}
	const FZoneGraphTag CrowdTag = CrowdSettings->CrowdTag;
	const FZoneGraphTag CrossingTag = CrowdSettings->CrossingTag;
	if (!CrossingTag.IsValid())
	{
		bCrossingsCollected = true;
		return true;
	}
	for (const FRegisteredZoneGraphData& Registered : ZoneGraph->GetRegisteredZoneGraphData())
	{
		if (!Registered.bInUse || Registered.ZoneGraphData == nullptr)
		{
			continue;
		}
		const FZoneGraphStorage& Storage = Registered.ZoneGraphData->GetStorage();
		const int32 Before = Crossings.Num();
		for (int32 LaneIndex = 0; LaneIndex < Storage.Lanes.Num(); ++LaneIndex)
		{
			const FZoneLaneData& Data = Storage.Lanes[LaneIndex];
			if (!Data.Tags.Contains(CrossingTag) || (CrowdTag.IsValid() && !Data.Tags.Contains(CrowdTag)) || Data.PointsEnd - Data.PointsBegin < 2)
			{
				continue;
			}
			FCrossing& Crossing = Crossings.AddDefaulted_GetRef();
			Crossing.Lane = FZoneGraphLaneHandle(LaneIndex, Storage.DataHandle);
			Crossing.Axis = RammsTraffic::TravelAxis(FVector2D(Storage.LanePoints[Data.PointsEnd - 1] - Storage.LanePoints[Data.PointsBegin]));
		}
		if (Crossings.Num() > Before && !Crowd->HasCrowdDataForZoneGraph(Storage.DataHandle))
		{
			// The crowd has not built its lane data for this graph yet: try again next tick.
			Crossings.Reset();
			return false;
		}
	}
	bCrossingsCollected = true;
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Traffic signal clock controls %d crossing lanes (cycle %.1f s)."), *GetName(), Crossings.Num(), Timing.GetCycleSeconds());
	return true;
}

void ARammsTrafficSignalClock::UpdateCrossings(const FRammsSignalState& State)
{
	if (!bCrossingsCollected && !CollectCrossings())
	{
		return;
	}
	UMassCrowdSubsystem* Crowd = UWorld::GetSubsystem<UMassCrowdSubsystem>(GetWorld());
	if (Crowd == nullptr)
	{
		return;
	}
	for (FCrossing& Crossing : Crossings)
	{
		const int8 Want = State.GetWalk(Crossing.Axis) == ERammsWalkSignal::Walk ? 1 : 0;
		if (Crossing.Applied != Want && Crowd->HasCrowdDataForZoneGraph(Crossing.Lane.DataHandle)
			&& Crowd->SetLaneState(Crossing.Lane, Want ? ECrowdLaneState::Opened : ECrowdLaneState::Closed))
		{
			Crossing.Applied = Want;
		}
	}
}
