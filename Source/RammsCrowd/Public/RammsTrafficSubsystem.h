// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "RammsTrafficTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SharedPointer.h"
#include "ZoneGraphTypes.h"
#include "RammsTrafficSubsystem.generated.h"

class AZoneGraphData;
class ARammsTrafficSignalClock;
struct FZoneGraphStorage;

/** Where a pedestrian lane (MassCrowd CrowdTag: a street crossing) crosses a vehicle lane. */
struct RAMMSCROWD_API FRammsTrafficCrossingPoint
{
	FZoneGraphLaneHandle PedestrianLane;
	/** Along the vehicle lane, cm. */
	float Distance = 0.0f;
};

/** A pedestrian lane that crosses vehicle lanes (a street crossing), as distances along it, cm. */
struct RAMMSCROWD_API FRammsStreetCrossing
{
	/** The first and last vehicle lane centres it crosses. */
	float RoadStart = 0.0f;
	float RoadEnd = 0.0f;
	/** Where the traffic's lanes begin and end along it (the outer edges of those two lanes). */
	float EdgeStart = 0.0f;
	float EdgeEnd = 0.0f;
};

/** One vehicle lane of the traffic network (a ZoneGraph lane with the Vehicle tag). */
struct RAMMSCROWD_API FRammsTrafficLane
{
	FZoneGraphLaneHandle Handle;
	float Length = 0.0f;
	/** Half the lane's width, cm. */
	float HalfWidth = 160.0f;
	/** The intersection (network zone) it is a turning lane of, or INDEX_NONE for a road lane. */
	int32 Zone = INDEX_NONE;
	/** A lane whose next lanes are another intersection's: it ends at a stop line. */
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
	/** The lane's polyline (world space) and each point's distance along it, cm (a copy of its ZoneGraph points, so
	 *  the traffic samples lanes without the ZoneGraph storage). */
	TArray<FVector> Points;
	TArray<float> Progress;
	TArray<int32, TInlineAllocator<4>> Next;
	/** Intersection lanes of the same zone this one crosses, merges with or passes too close to. */
	TArray<int32, TInlineAllocator<8>> Conflicts;
	/** Intersection lanes of the same zone that start at the same entry (they diverge from this one). */
	TArray<int32, TInlineAllocator<4>> Siblings;
	/** Pedestrian lanes that cross this lane, by distance along it. */
	TArray<FRammsTrafficCrossingPoint> Crossings;
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
	/** Pedestrian lanes that cross a vehicle lane (street crossings). */
	TMap<FZoneGraphLaneHandle, FRammsStreetCrossing> StreetCrossings;
	int32 Version = 0;

	bool IsEmpty() const { return Lanes.Num() == 0; }
	bool IsValidLane(const int32 Lane) const { return Lanes.IsValidIndex(Lane); }
	/** The lane after Lane when there is only one (an intersection lane's exit), else INDEX_NONE. */
	int32 UniqueNext(const int32 Lane) const { return Lanes.IsValidIndex(Lane) && Lanes[Lane].Next.Num() == 1 ? Lanes[Lane].Next[0] : INDEX_NONE; }
	bool Conflict(const int32 A, const int32 B) const { return Lanes.IsValidIndex(A) && Lanes[A].Conflicts.Contains(B); }
	/** Position and direction Distance along Lane (clamped to it); false when the lane has no polyline. */
	bool Sample(int32 Lane, float Distance, FVector& OutPosition, FVector& OutDirection) const;
};

/** What RammsTraffic::FinalizeNetwork found. */
struct RAMMSCROWD_API FRammsTrafficNetworkStats
{
	int32 Conflicts = 0;
	int32 Signalized = 0;
	/** Lanes without a next vehicle lane. */
	int32 DeadEnds = 0;
	/** Intersection lanes that lead straight into another intersection's lanes (no road lane between). */
	int32 ZoneToZone = 0;
};

namespace RammsTraffic
{
	/** Intersection lanes from different entries closer than this cannot both be used at once: the widest vehicle and
	 *  some clearance, cm (220 for 190 cm cars). */
	RAMMSCROWD_API double ConflictDistance(double MaxVehicleWidth);

	/**
	 * Completes a network whose lanes have their polylines, zones, entries and next lanes: lane lengths from the
	 * polylines when missing, approaches (lanes whose next lanes are another zone's), zone centres, lane conflicts
	 * (lanes of a zone to one exit, or from different entries within ConflictDistanceCm of each other) and siblings
	 * (lanes of a zone from one entry). Used by URammsTrafficSubsystem and by the tests' hand-made networks.
	 */
	RAMMSCROWD_API FRammsTrafficNetworkStats FinalizeNetwork(FRammsTrafficNetwork& Network, double ConflictDistanceCm);
}

/**
 * Street crossings walkers are held at (URammsTrafficSubsystem::SetHeldCrossings), each with a world time: the gate
 * holds a walker who came onto the lane at or after it. Holds belong to the traffic frame that set them and go
 * stale: a traffic frame publishes a new set every time it runs (an empty one when it has nothing to drive), and
 * the gate ignores a set older than MaxAgeSeconds, so no hold outlives the traffic that made it.
 */
struct RAMMSCROWD_API FRammsHeldCrossings
{
	static constexpr double MaxAgeSeconds = 1.0;

	void Set(TMap<FZoneGraphLaneHandle, float>&& InHeld, const double InTime)
	{
		Held = MoveTemp(InHeld);
		Time = InTime;
	}

	bool IsFresh(const double Now) const { return Time >= 0.0 && FMath::Abs(Now - Time) <= MaxAgeSeconds; }
	const float* Find(const FZoneGraphLaneHandle& Lane, const double Now) const { return IsFresh(Now) ? Held.Find(Lane) : nullptr; }
	int32 Num(const double Now) const { return IsFresh(Now) ? Held.Num() : 0; }
	const TMap<FZoneGraphLaneHandle, float>& GetAll() const { return Held; }

private:
	TMap<FZoneGraphLaneHandle, float> Held;
	double Time = -1.0;
};

/**
 * World state the traffic shares: the vehicle lane network (from ZoneGraph lanes tagged Vehicle, Intersection
 * and Signalized: names configurable on the spawner), and the signal clock. Game and PIE worlds only.
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

	/** The widest vehicle (cm): intersection lanes closer than RammsTraffic::ConflictDistance of it conflict. Rebuilds the network when it changes. */
	void SetMaxVehicleWidth(float WidthCm);

	/**
	 * The vehicle lane network, built on first use and again when ZoneGraph data comes or goes. Null when there are no
	 * vehicle lanes (yet). Game thread only (the traffic processor and the spawner); other threads use IsStreetCrossing.
	 */
	const FRammsTrafficNetwork* GetNetwork();

	/** The storage a lane handle points into, or null. */
	const FZoneGraphStorage* GetStorage(const FZoneGraphLaneHandle& Handle) const;

	/** Network rebuilds so far (vehicles holding an older version re-place themselves). */
	int32 GetNetworkVersion() const { return Network.Version; }

	/**
	 * Pedestrian lanes a vehicle is over or about to cross (set by every traffic frame, at world time Time), each with a
	 * world time: URammsCrossingGateProcessor holds at its start a walker who came onto the lane at or after that time,
	 * and keeps holding one it already holds. A car driving on, or standing over a crossing, holds everyone (the lowest
	 * float); a car yielding to walkers holds just those who come after it began to (who was first goes first).
	 */
	void SetHeldCrossings(TMap<FZoneGraphLaneHandle, float>&& InHeld, const double Time) { HeldCrossings.Set(MoveTemp(InHeld), Time); }
	/** The hold on a crossing, or null (none, or the last traffic frame is older than FRammsHeldCrossings::MaxAgeSeconds). */
	const float* FindCrossingHold(const FZoneGraphLaneHandle& Lane, const double Now) const { return HeldCrossings.Find(Lane, Now); }
	int32 GetHeldCrossingCount(const double Now) const { return HeldCrossings.Num(Now); }

	/** Whether a pedestrian lane crosses a vehicle lane (a street crossing). False until the network is built (without
	 *  traffic, never). Thread-safe: StateTree tasks call it from Mass worker threads while the game thread may rebuild
	 *  the network (it reads an immutable copy). */
	bool IsStreetCrossing(const FZoneGraphLaneHandle& Lane) const;

	/** The street crossing on a pedestrian lane, or null. Game thread only. */
	const FRammsStreetCrossing* FindStreetCrossing(const FZoneGraphLaneHandle& Lane) const { return Network.StreetCrossings.Find(Lane); }

	/** Whether a lane is one of the traffic's vehicle lanes (as last built). Game thread only. */
	bool IsVehicleLane(const FZoneGraphLaneHandle& Lane) const { return Network.LaneByHandle.Contains(Lane); }

	/** Warns once per world that crowd members walk on vehicle lanes (URammsCrossingGateProcessor finds them). */
	void WarnWalkersOnVehicleLanes(int32 Count);

	/** ramms.Traffic.Dump: requests are counted globally (any world) and served by the next traffic frame. */
	static void RequestDump();
	static int32 GetDumpRequestSerial();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleZoneGraphChanged(const AZoneGraphData* ZoneGraphData);
	void BuildNetwork();

	FRammsTrafficNetwork Network;
	FRammsHeldCrossings HeldCrossings;
	/** Street crossing lanes as last built, for other threads (IsStreetCrossing); replaced, never changed, under the lock. */
	TSharedPtr<const TSet<FZoneGraphLaneHandle>, ESPMode::ThreadSafe> StreetCrossingLanes;
	mutable FRWLock StreetCrossingLock;
	bool bNetworkDirty = true;
	bool bWarnedNoLanes = false;
	bool bWarnedWalkersOnVehicleLanes = false;
	float MaxVehicleWidth = 190.0f;
	TWeakObjectPtr<ARammsTrafficSignalClock> SignalClock;
	FName VehicleTagName = FName(TEXT("Vehicle"));
	FName IntersectionTagName = FName(TEXT("Intersection"));
	FName SignalizedTagName = FName(TEXT("Signalized"));
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
};
