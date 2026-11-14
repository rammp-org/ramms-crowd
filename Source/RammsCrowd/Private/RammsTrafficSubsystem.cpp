// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficSubsystem.h"

#include "Engine/World.h"
#include "Math/UnrealMathUtility.h"
#include "RammsCrowdLog.h"
#include "RammsTrafficSignalClock.h"
#include "ZoneGraphData.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	/** Intersection lanes closer than this anywhere (outside a shared entry) cannot both be used at once. */
	constexpr double ConflictDistanceCm = 220.0;

	double LaneDistance2D(const FZoneGraphStorage& StorageA, const FZoneLaneData& A, const FZoneGraphStorage& StorageB, const FZoneLaneData& B)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = A.PointsBegin; i + 1 < A.PointsEnd; ++i)
		{
			const FVector A0(StorageA.LanePoints[i].X, StorageA.LanePoints[i].Y, 0.0);
			const FVector A1(StorageA.LanePoints[i + 1].X, StorageA.LanePoints[i + 1].Y, 0.0);
			for (int32 j = B.PointsBegin; j + 1 < B.PointsEnd; ++j)
			{
				const FVector B0(StorageB.LanePoints[j].X, StorageB.LanePoints[j].Y, 0.0);
				const FVector B1(StorageB.LanePoints[j + 1].X, StorageB.LanePoints[j + 1].Y, 0.0);
				FVector P, Q;
				FMath::SegmentDistToSegmentSafe(A0, A1, B0, B1, P, Q);
				Best = FMath::Min(Best, FVector::Dist(P, Q));
			}
		}
		return Best;
	}
}

void URammsTrafficSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AddedHandle = UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.AddUObject(this, &URammsTrafficSubsystem::HandleZoneGraphChanged);
	RemovedHandle = UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.AddUObject(this, &URammsTrafficSubsystem::HandleZoneGraphChanged);
}

void URammsTrafficSubsystem::Deinitialize()
{
	UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.Remove(AddedHandle);
	UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.Remove(RemovedHandle);
	Network = FRammsTrafficNetwork();
	Super::Deinitialize();
}

void URammsTrafficSubsystem::RegisterSignalClock(ARammsTrafficSignalClock* Clock)
{
	if (SignalClock.IsValid() && SignalClock.Get() != Clock)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: a second signal clock (%s) replaces %s; all signalized intersections share one clock."), *GetNameSafe(Clock), *GetNameSafe(SignalClock.Get()));
	}
	SignalClock = Clock;
}

void URammsTrafficSubsystem::UnregisterSignalClock(ARammsTrafficSignalClock* Clock)
{
	if (SignalClock.Get() == Clock)
	{
		SignalClock.Reset();
	}
}

ARammsTrafficSignalClock* URammsTrafficSubsystem::GetSignalClock() const
{
	return SignalClock.Get();
}

bool URammsTrafficSubsystem::GetSignalState(FRammsSignalState& OutState) const
{
	const ARammsTrafficSignalClock* Clock = SignalClock.Get();
	const UWorld* World = GetWorld();
	if (Clock == nullptr || World == nullptr)
	{
		return false;
	}
	OutState = Clock->GetSignalStateAt(World->GetTimeSeconds());
	return true;
}

void URammsTrafficSubsystem::SetTagNames(const FName InVehicleTag, const FName InIntersectionTag, const FName InSignalizedTag)
{
	if (InVehicleTag != VehicleTagName || InIntersectionTag != IntersectionTagName || InSignalizedTag != SignalizedTagName)
	{
		VehicleTagName = InVehicleTag;
		IntersectionTagName = InIntersectionTag;
		SignalizedTagName = InSignalizedTag;
		bNetworkDirty = true;
	}
}

const FRammsTrafficNetwork* URammsTrafficSubsystem::GetNetwork()
{
	if (bNetworkDirty)
	{
		BuildNetwork();
	}
	return Network.IsEmpty() ? nullptr : &Network;
}

const FZoneGraphStorage* URammsTrafficSubsystem::GetStorage(const FZoneGraphLaneHandle& Handle) const
{
	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	return ZoneGraph != nullptr ? ZoneGraph->GetZoneGraphStorage(Handle.DataHandle) : nullptr;
}

void URammsTrafficSubsystem::HandleZoneGraphChanged(const AZoneGraphData* ZoneGraphData)
{
	if (ZoneGraphData != nullptr && ZoneGraphData->GetWorld() == GetWorld())
	{
		bNetworkDirty = true;
	}
}

void URammsTrafficSubsystem::BuildNetwork()
{
	const int32 Version = Network.Version + 1;
	Network = FRammsTrafficNetwork();
	Network.Version = Version;
	bNetworkDirty = false;

	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld());
	if (ZoneGraph == nullptr)
	{
		return;
	}
	const FZoneGraphTag VehicleTag = ZoneGraph->GetTagByName(VehicleTagName);
	const FZoneGraphTag IntersectionTag = ZoneGraph->GetTagByName(IntersectionTagName);
	const FZoneGraphTag SignalizedTag = ZoneGraph->GetTagByName(SignalizedTagName);
	if (!VehicleTag.IsValid())
	{
		if (!bWarnedNoLanes)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: no ZoneGraph tag named '%s' (Project Settings > Zone Graph); no vehicle lanes."), *VehicleTagName.ToString());
			bWarnedNoLanes = true;
		}
		return;
	}

	// Lanes, and the intersections they belong to (one network zone per ZoneGraph polygon whose lanes carry the Intersection tag).
	TMap<uint64, int32> ZoneByKey;
	for (const FRegisteredZoneGraphData& Registered : ZoneGraph->GetRegisteredZoneGraphData())
	{
		if (!Registered.bInUse || Registered.ZoneGraphData == nullptr)
		{
			continue;
		}
		const FZoneGraphStorage& Storage = Registered.ZoneGraphData->GetStorage();
		for (int32 LaneIndex = 0; LaneIndex < Storage.Lanes.Num(); ++LaneIndex)
		{
			const FZoneLaneData& Data = Storage.Lanes[LaneIndex];
			if (!Data.Tags.Contains(VehicleTag) || Data.PointsEnd - Data.PointsBegin < 2)
			{
				continue;
			}
			const int32 Index = Network.Lanes.AddDefaulted();
			FRammsTrafficLane& Lane = Network.Lanes[Index];
			Lane.Handle = FZoneGraphLaneHandle(LaneIndex, Storage.DataHandle);
			Lane.Length = Storage.LanePointProgressions[Data.PointsEnd - 1];
			Lane.StartPosition = Storage.LanePoints[Data.PointsBegin];
			Lane.EndPosition = Storage.LanePoints[Data.PointsEnd - 1];
			const FVector2D StartDirection(Storage.LaneTangentVectors[Data.PointsBegin]);
			const FVector2D EndDirection(Storage.LaneTangentVectors[Data.PointsEnd - 1]);
			Lane.Axis = static_cast<uint8>(RammsTraffic::TravelAxis(StartDirection));
			Lane.Turn = RammsTraffic::ClassifyTurn(StartDirection, EndDirection);
			Lane.StartEntry = Data.StartEntryId;
			Lane.EndEntry = Data.EndEntryId;
			Lane.bSignalized = SignalizedTag.IsValid() && Data.Tags.Contains(SignalizedTag);
			if (IntersectionTag.IsValid() && Data.Tags.Contains(IntersectionTag))
			{
				const uint64 Key = (static_cast<uint64>(Storage.DataHandle.Index) << 32) | static_cast<uint64>(static_cast<uint32>(Data.ZoneIndex));
				int32* Zone = ZoneByKey.Find(Key);
				if (Zone == nullptr)
				{
					Zone = &ZoneByKey.Add(Key, Network.Zones.AddDefaulted());
				}
				Lane.Zone = *Zone;
				Network.Zones[*Zone].Lanes.Add(Index);
				Network.Zones[*Zone].bSignalized |= Lane.bSignalized;
			}
			Network.LaneByHandle.Add(Lane.Handle, Index);
		}
	}

	// Links between vehicle lanes.
	TArray<FZoneGraphLinkedLane> Links;
	for (int32 Index = 0; Index < Network.Lanes.Num(); ++Index)
	{
		FRammsTrafficLane& Lane = Network.Lanes[Index];
		const FZoneGraphStorage* Storage = ZoneGraph->GetZoneGraphStorage(Lane.Handle.DataHandle);
		if (Storage == nullptr)
		{
			continue;
		}
		Links.Reset();
		UE::ZoneGraph::Query::GetLinkedLanes(*Storage, static_cast<uint32>(Lane.Handle.Index), EZoneLaneLinkType::Outgoing, EZoneLaneLinkFlags::All, EZoneLaneLinkFlags::None, Links);
		for (const FZoneGraphLinkedLane& Link : Links)
		{
			if (const int32* Dest = Network.LaneByHandle.Find(Link.DestLane))
			{
				Lane.Next.AddUnique(*Dest);
			}
		}
	}

	// Approaches (road lanes that end at an intersection's stop line), zone centres and lane conflicts.
	for (int32 Index = 0; Index < Network.Lanes.Num(); ++Index)
	{
		FRammsTrafficLane& Lane = Network.Lanes[Index];
		if (Lane.Zone != INDEX_NONE)
		{
			continue;
		}
		for (const int32 Next : Lane.Next)
		{
			const int32 Zone = Network.Lanes[Next].Zone;
			if (Zone != INDEX_NONE)
			{
				Lane.bApproach = true;
				Network.Zones[Zone].Approaches.AddUnique(Index);
			}
		}
	}
	int32 Conflicts = 0;
	for (FRammsTrafficZone& Zone : Network.Zones)
	{
		FVector Sum = FVector::ZeroVector;
		for (const int32 A : Zone.Lanes)
		{
			Sum += Network.Lanes[A].StartPosition + Network.Lanes[A].EndPosition;
		}
		Zone.Center = Zone.Lanes.Num() > 0 ? Sum / (2.0 * Zone.Lanes.Num()) : FVector::ZeroVector;
		for (int32 i = 0; i < Zone.Lanes.Num(); ++i)
		{
			FRammsTrafficLane& A = Network.Lanes[Zone.Lanes[i]];
			const FZoneGraphStorage* StorageA = ZoneGraph->GetZoneGraphStorage(A.Handle.DataHandle);
			for (int32 j = i + 1; j < Zone.Lanes.Num(); ++j)
			{
				FRammsTrafficLane& B = Network.Lanes[Zone.Lanes[j]];
				const FZoneGraphStorage* StorageB = ZoneGraph->GetZoneGraphStorage(B.Handle.DataHandle);
				bool bConflict = false;
				if (A.EndEntry == B.EndEntry)
				{
					bConflict = true; // merging into one exit
				}
				else if (A.StartEntry != B.StartEntry && StorageA != nullptr && StorageB != nullptr)
				{
					// (Lanes from one entry diverge: never in each other's way.)
					bConflict = LaneDistance2D(*StorageA, StorageA->Lanes[A.Handle.Index], *StorageB, StorageB->Lanes[B.Handle.Index]) < ConflictDistanceCm;
				}
				if (bConflict)
				{
					A.Conflicts.Add(Zone.Lanes[j]);
					B.Conflicts.Add(Zone.Lanes[i]);
					++Conflicts;
				}
			}
		}
	}

	int32 Signalized = 0;
	for (const FRammsTrafficZone& Zone : Network.Zones)
	{
		Signalized += Zone.bSignalized ? 1 : 0;
	}
	if (!Network.IsEmpty())
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("Traffic network v%d: %d vehicle lanes, %d intersections (%d signalized), %d lane conflicts."),
			Network.Version, Network.Lanes.Num(), Network.Zones.Num(), Signalized, Conflicts);
	}
}
