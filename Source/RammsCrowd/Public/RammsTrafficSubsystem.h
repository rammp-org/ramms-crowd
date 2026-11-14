// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RammsTrafficTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ZoneGraphTypes.h"
#include "RammsTrafficSubsystem.generated.h"

class AZoneGraphData;
class ARammsTrafficSignalClock;
struct FZoneGraphStorage;

/** One vehicle lane of the traffic network (a ZoneGraph lane with the Vehicle tag). */
struct RAMMSCROWD_API FRammsTrafficLane
{
	FZoneGraphLaneHandle Handle;
	float Length = 0.0f;
	/** The intersection (network zone) it is a turning lane of, or INDEX_NONE for a road lane. */
	int32 Zone = INDEX_NONE;
	/** Road lane whose next lanes are an intersection's: it ends at the stop line. */
	bool bApproach = false;
	bool bSignalized = false;
	ERammsTrafficTurn Turn = ERammsTrafficTurn::Straight;
	/** Travel axis (0 X, 1 Y) at the lane's start; the signal an intersection lane obeys. */
	uint8 Axis = 0;
	/** ZoneGraph polygon entry ids (intersection lanes): two lanes from one entry diverge, two to one entry merge. */
	uint16 StartEntry = 0;
	uint16 EndEntry = 0;
	FVector StartPosition = FVector::ZeroVector;
	FVector EndPosition = FVector::ZeroVector;
	TArray<int32, TInlineAllocator<4>> Next;
	/** Intersection lanes of the same zone this one crosses, merges with or passes too close to. */
	TArray<int32, TInlineAllocator<8>> Conflicts;
};

/** A controlled intersection: a ZoneGraph polygon whose lanes carry the Intersection tag. */
struct RAMMSCROWD_API FRammsTrafficZone
{
	bool bSignalized = false;
	FVector Center = FVector::ZeroVector;
	TArray<int32> Lanes;
	TArray<int32> Approaches;
};

/** The vehicle lane graph built from the registered ZoneGraph data. */
struct RAMMSCROWD_API FRammsTrafficNetwork
{
	TArray<FRammsTrafficLane> Lanes;
	TArray<FRammsTrafficZone> Zones;
	TMap<FZoneGraphLaneHandle, int32> LaneByHandle;
	int32 Version = 0;

	bool IsEmpty() const { return Lanes.Num() == 0; }
	bool IsValidLane(const int32 Lane) const { return Lanes.IsValidIndex(Lane); }
	/** The lane after Lane when there is only one (an intersection lane's exit), else INDEX_NONE. */
	int32 UniqueNext(const int32 Lane) const { return Lanes.IsValidIndex(Lane) && Lanes[Lane].Next.Num() == 1 ? Lanes[Lane].Next[0] : INDEX_NONE; }
	bool Conflict(const int32 A, const int32 B) const { return Lanes.IsValidIndex(A) && Lanes[A].Conflicts.Contains(B); }
};

/**
 * World state the traffic shares: the vehicle lane network (from ZoneGraph lanes tagged Vehicle, Intersection
 * and Signalized: names configurable on the spawner), and the signal clock.
 */
UCLASS()
class RAMMSCROWD_API URammsTrafficSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterSignalClock(ARammsTrafficSignalClock* Clock);
	void UnregisterSignalClock(ARammsTrafficSignalClock* Clock);
	ARammsTrafficSignalClock* GetSignalClock() const;

	/** The signals now. False when the world has no signal clock: signalized intersections then run as all-way stops. */
	bool GetSignalState(FRammsSignalState& OutState) const;

	/** ZoneGraph tag names of vehicle lanes, intersection lanes and signalized intersections. Rebuilds the network when they change. */
	void SetTagNames(FName InVehicleTag, FName InIntersectionTag, FName InSignalizedTag);

	/** The vehicle lane network, built on first use and again when ZoneGraph data comes or goes. Null when there are no vehicle lanes (yet). */
	const FRammsTrafficNetwork* GetNetwork();

	/** The storage a lane handle points into, or null. */
	const FZoneGraphStorage* GetStorage(const FZoneGraphLaneHandle& Handle) const;

	/** Network rebuilds so far (vehicles holding an older version re-place themselves). */
	int32 GetNetworkVersion() const { return Network.Version; }

private:
	void HandleZoneGraphChanged(const AZoneGraphData* ZoneGraphData);
	void BuildNetwork();

	FRammsTrafficNetwork Network;
	bool bNetworkDirty = true;
	bool bWarnedNoLanes = false;
	TWeakObjectPtr<ARammsTrafficSignalClock> SignalClock;
	FName VehicleTagName = FName(TEXT("Vehicle"));
	FName IntersectionTagName = FName(TEXT("Intersection"));
	FName SignalizedTagName = FName(TEXT("Signalized"));
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
};
