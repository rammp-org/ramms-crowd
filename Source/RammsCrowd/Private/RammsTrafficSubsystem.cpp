// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficSubsystem.h"

#include "Algo/BinarySearch.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassCrowdSettings.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopeExit.h"
#include "RammsCrowdLog.h"
#include "RammsTrafficSignalClock.h"
#include "ZoneGraphData.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	double PolylineDistance2D(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < A.Num(); ++i)
		{
			const FVector A0(A[i].X, A[i].Y, 0.0);
			const FVector A1(A[i + 1].X, A[i + 1].Y, 0.0);
			for (int32 j = 0; j + 1 < B.Num(); ++j)
			{
				const FVector B0(B[j].X, B[j].Y, 0.0);
				const FVector B1(B[j + 1].X, B[j + 1].Y, 0.0);
				FVector P, Q;
				FMath::SegmentDistToSegmentSafe(A0, A1, B0, B1, P, Q);
				Best = FMath::Min(Best, FVector::Dist(P, Q));
			}
		}
		return Best;
	}

	/** Where segment A0-A1 crosses B0-B1 (2D), as fractions of A and of B; false when they do not cross. */
	bool SegmentsCross2D(const FVector2D& A0, const FVector2D& A1, const FVector2D& B0, const FVector2D& B1, double& OutT, double& OutU)
	{
		const FVector2D R = A1 - A0;
		const FVector2D S = B1 - B0;
		const double Den = R.X * S.Y - R.Y * S.X;
		if (FMath::Abs(Den) < 1.0e-6)
		{
			return false;
		}
		const FVector2D Q = B0 - A0;
		const double T = (Q.X * S.Y - Q.Y * S.X) / Den;
		const double U = (Q.X * R.Y - Q.Y * R.X) / Den;
		OutT = T;
		OutU = U;
		return T >= 0.0 && T <= 1.0 && U >= 0.0 && U <= 1.0;
	}

	FBox2D LaneBounds2D(const FZoneGraphStorage& Storage, const FZoneLaneData& Data)
	{
		FBox2D Box(ForceInit);
		for (int32 i = Data.PointsBegin; i < Data.PointsEnd; ++i)
		{
			Box += FVector2D(Storage.LanePoints[i]);
		}
		return Box;
	}

	int32 GTrafficDumpRequests = 0;

	FAutoConsoleCommand GTrafficDumpCommand(
		TEXT("ramms.Traffic.Dump"),
		TEXT("Log every Mass traffic vehicle (lane, distance, speed, next lane and turn, what limits its acceleration and what it waits for) and every intersection's claims, once, on the next traffic frame (LogRammsCrowd). ramms.Traffic.DumpInterval repeats it."),
		FConsoleCommandDelegate::CreateStatic(&URammsTrafficSubsystem::RequestDump));
}

bool FRammsTrafficNetwork::Sample(const int32 Lane, const float Distance, FVector& OutPosition, FVector& OutDirection) const
{
	if (!Lanes.IsValidIndex(Lane))
	{
		return false;
	}
	const FRammsTrafficLane& L = Lanes[Lane];
	const int32 Num = L.Points.Num();
	if (Num < 2 || L.Progress.Num() != Num)
	{
		return false;
	}
	const float D = FMath::Clamp(Distance, 0.0f, L.Progress.Last());
	const int32 i = FMath::Clamp(static_cast<int32>(Algo::UpperBound(L.Progress, D)) - 1, 0, Num - 2);
	const float Segment = L.Progress[i + 1] - L.Progress[i];
	const float T = Segment > UE_KINDA_SMALL_NUMBER ? FMath::Clamp((D - L.Progress[i]) / Segment, 0.0f, 1.0f) : 0.0f;
	OutPosition = FMath::Lerp(L.Points[i], L.Points[i + 1], static_cast<double>(T));
	OutDirection = (L.Points[i + 1] - L.Points[i]).GetSafeNormal();
	return true;
}

namespace RammsTraffic
{
	double ConflictDistance(const double MaxVehicleWidth)
	{
		return FMath::Max(MaxVehicleWidth, 100.0) + 30.0;
	}

	FRammsTrafficNetworkStats FinalizeNetwork(FRammsTrafficNetwork& Network, const double ConflictDistanceCm)
	{
		FRammsTrafficNetworkStats Stats;

		// Lanes: lengths and ends from their polylines; the zones from the lanes that name them.
		int32 ZoneCount = Network.Zones.Num();
		for (FRammsTrafficLane& Lane : Network.Lanes)
		{
			if (Lane.Progress.Num() > 0 && Lane.Length <= 0.0f)
			{
				Lane.Length = Lane.Progress.Last();
			}
			if (Lane.Points.Num() > 0)
			{
				Lane.StartPosition = Lane.Points[0];
				Lane.EndPosition = Lane.Points.Last();
			}
			Lane.bApproach = false;
			Lane.Conflicts.Reset();
			Lane.Siblings.Reset();
			ZoneCount = FMath::Max(ZoneCount, Lane.Zone + 1);
		}
		Network.Zones.SetNum(ZoneCount);
		for (FRammsTrafficZone& Zone : Network.Zones)
		{
			Zone.Lanes.Reset();
			Zone.Approaches.Reset();
			Zone.bSignalized = false;
		}
		for (int32 Index = 0; Index < Network.Lanes.Num(); ++Index)
		{
			const FRammsTrafficLane& Lane = Network.Lanes[Index];
			if (Lane.Zone != INDEX_NONE)
			{
				Network.Zones[Lane.Zone].Lanes.Add(Index);
				Network.Zones[Lane.Zone].bSignalized |= Lane.bSignalized;
			}
		}

		// Approaches: lanes whose next lanes are an intersection's (a road lane, or another intersection's lane).
		for (int32 Index = 0; Index < Network.Lanes.Num(); ++Index)
		{
			FRammsTrafficLane& Lane = Network.Lanes[Index];
			bool bIntoAnotherZone = false;
			for (const int32 Next : Lane.Next)
			{
				const int32 Zone = Network.IsValidLane(Next) ? Network.Lanes[Next].Zone : INDEX_NONE;
				if (Zone != INDEX_NONE && Zone != Lane.Zone)
				{
					Lane.bApproach = true;
					Network.Zones[Zone].Approaches.AddUnique(Index);
					bIntoAnotherZone |= Lane.Zone != INDEX_NONE;
				}
			}
			Stats.ZoneToZone += bIntoAnotherZone ? 1 : 0;
			Stats.DeadEnds += Lane.Next.Num() == 0 ? 1 : 0;
		}

		// Zone centres, lane conflicts and siblings.
		for (FRammsTrafficZone& Zone : Network.Zones)
		{
			Stats.Signalized += Zone.bSignalized ? 1 : 0;
			FVector Sum = FVector::ZeroVector;
			for (const int32 A : Zone.Lanes)
			{
				Sum += Network.Lanes[A].StartPosition + Network.Lanes[A].EndPosition;
			}
			Zone.Center = Zone.Lanes.Num() > 0 ? Sum / (2.0 * Zone.Lanes.Num()) : FVector::ZeroVector;
			for (int32 i = 0; i < Zone.Lanes.Num(); ++i)
			{
				FRammsTrafficLane& A = Network.Lanes[Zone.Lanes[i]];
				for (int32 j = i + 1; j < Zone.Lanes.Num(); ++j)
				{
					FRammsTrafficLane& B = Network.Lanes[Zone.Lanes[j]];
					bool bConflict = false;
					if (A.EndEntry == B.EndEntry)
					{
						bConflict = true; // merging into one exit
					}
					else if (A.StartEntry != B.StartEntry)
					{
						// (Lanes from one entry diverge: never in each other's way.)
						bConflict = PolylineDistance2D(A.Points, B.Points) < ConflictDistanceCm;
					}
					if (bConflict)
					{
						A.Conflicts.Add(Zone.Lanes[j]);
						B.Conflicts.Add(Zone.Lanes[i]);
						++Stats.Conflicts;
					}
				}
			}
			// Diverging lanes (same zone, same entry): a car that just turned off is still beside a car going on.
			for (const int32 A : Zone.Lanes)
			{
				for (const int32 B : Zone.Lanes)
				{
					if (A != B && Network.Lanes[A].StartEntry == Network.Lanes[B].StartEntry)
					{
						Network.Lanes[A].Siblings.Add(B);
					}
				}
			}
		}
		return Stats;
	}
}

void URammsTrafficSubsystem::RequestDump()
{
	++GTrafficDumpRequests;
}

int32 URammsTrafficSubsystem::GetDumpRequestSerial()
{
	return GTrafficDumpRequests;
}

bool URammsTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Traffic runs where the game runs; editor and preview worlds have no use for it.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
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
	{
		FWriteScopeLock Lock(StreetCrossingLock);
		StreetCrossingLanes.Reset();
	}
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

void URammsTrafficSubsystem::SetMaxVehicleWidth(const float WidthCm)
{
	const float Width = FMath::Max(WidthCm, 100.0f);
	if (!FMath::IsNearlyEqual(Width, MaxVehicleWidth, 1.0f))
	{
		MaxVehicleWidth = Width;
		bNetworkDirty = true;
	}
}

const FRammsTrafficNetwork* URammsTrafficSubsystem::GetNetwork()
{
	if (bNetworkDirty && ensureMsgf(IsInGameThread(), TEXT("Traffic: the network is built on the game thread only.")))
	{
		BuildNetwork();
	}
	return Network.IsEmpty() ? nullptr : &Network;
}

bool URammsTrafficSubsystem::IsStreetCrossing(const FZoneGraphLaneHandle& Lane) const
{
	FReadScopeLock Lock(StreetCrossingLock);
	return StreetCrossingLanes.IsValid() && StreetCrossingLanes->Contains(Lane);
}

void URammsTrafficSubsystem::WarnWalkersOnVehicleLanes(const int32 Count)
{
	if (Count > 0 && !bWarnedWalkersOnVehicleLanes)
	{
		bWarnedWalkersOnVehicleLanes = true;
		UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: %d crowd members walk on the traffic's '%s' lanes (they snapped onto them when spawned). Give the entity config's ZoneGraph Navigation trait a LaneFilter with NotTags=%s, and spawn on pedestrian lanes (RAMMS Crowd Spawner ExcludeLaneTagName)."),
			Count, *VehicleTagName.ToString(), *VehicleTagName.ToString());
	}
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
	ON_SCOPE_EXIT
	{
		// Other threads see the street crossings of the network just built (none when it is empty).
		TSharedPtr<TSet<FZoneGraphLaneHandle>, ESPMode::ThreadSafe> Lanes = MakeShared<TSet<FZoneGraphLaneHandle>, ESPMode::ThreadSafe>();
		for (const TPair<FZoneGraphLaneHandle, FRammsStreetCrossing>& Pair : Network.StreetCrossings)
		{
			Lanes->Add(Pair.Key);
		}
		FWriteScopeLock Lock(StreetCrossingLock);
		StreetCrossingLanes = Lanes;
	};

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

	// Lanes (with copies of their polylines), and the intersections they belong to (one network zone per ZoneGraph
	// polygon whose lanes carry the Intersection tag).
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
			Lane.HalfWidth = Data.Width > 1.0f ? Data.Width * 0.5f : 160.0f;
			Lane.Points.Reserve(Data.PointsEnd - Data.PointsBegin);
			Lane.Progress.Reserve(Data.PointsEnd - Data.PointsBegin);
			for (int32 Point = Data.PointsBegin; Point < Data.PointsEnd; ++Point)
			{
				Lane.Points.Add(Storage.LanePoints[Point]);
				Lane.Progress.Add(Storage.LanePointProgressions[Point]);
			}
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

	// Approaches, zone centres, lane conflicts, siblings.
	const FRammsTrafficNetworkStats Stats = RammsTraffic::FinalizeNetwork(Network, RammsTraffic::ConflictDistance(MaxVehicleWidth));

	// Pedestrian lanes (MassCrowd's CrowdTag) that cross each vehicle lane: the street crossings.
	int32 CrossingPoints = 0;
	const UMassCrowdSettings* CrowdSettings = GetDefault<UMassCrowdSettings>();
	if (CrowdSettings != nullptr && CrowdSettings->CrowdTag.IsValid())
	{
		struct FPedestrianLane
		{
			FZoneGraphLaneHandle Handle;
			const FZoneGraphStorage* Storage = nullptr;
			int32 Index = 0;
			FBox2D Bounds;
		};
		TArray<FPedestrianLane> PedestrianLanes;
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
				if (Data.Tags.Contains(CrowdSettings->CrowdTag) && !Data.Tags.Contains(VehicleTag) && Data.PointsEnd - Data.PointsBegin >= 2)
				{
					PedestrianLanes.Add(FPedestrianLane{ FZoneGraphLaneHandle(LaneIndex, Storage.DataHandle), &Storage, LaneIndex, LaneBounds2D(Storage, Data) });
				}
			}
		}
		for (FRammsTrafficLane& Lane : Network.Lanes)
		{
			const FZoneGraphStorage* Storage = ZoneGraph->GetZoneGraphStorage(Lane.Handle.DataHandle);
			if (Storage == nullptr)
			{
				continue;
			}
			const FZoneLaneData& Data = Storage->Lanes[Lane.Handle.Index];
			const FBox2D Bounds = LaneBounds2D(*Storage, Data).ExpandBy(10.0);
			for (const FPedestrianLane& Pedestrian : PedestrianLanes)
			{
				if (!Bounds.Intersect(Pedestrian.Bounds))
				{
					continue;
				}
				const FZoneLaneData& PedestrianData = Pedestrian.Storage->Lanes[Pedestrian.Index];
				bool bFound = false;
				for (int32 i = Data.PointsBegin; i + 1 < Data.PointsEnd && !bFound; ++i)
				{
					const FVector2D A0(Storage->LanePoints[i]);
					const FVector2D A1(Storage->LanePoints[i + 1]);
					for (int32 j = PedestrianData.PointsBegin; j + 1 < PedestrianData.PointsEnd; ++j)
					{
						const FVector2D B0(Pedestrian.Storage->LanePoints[j]);
						const FVector2D B1(Pedestrian.Storage->LanePoints[j + 1]);
						double T = 0.0, U = 0.0;
						if (SegmentsCross2D(A0, A1, B0, B1, T, U))
						{
							const float Along = Storage->LanePointProgressions[i] + static_cast<float>(T) * (Storage->LanePointProgressions[i + 1] - Storage->LanePointProgressions[i]);
							Lane.Crossings.Add(FRammsTrafficCrossingPoint{ Pedestrian.Handle, Along });
							// Along the pedestrian lane: where it meets this lane's centre, and the lane's edges either side
							// (half its width over the sine of the angle between them).
							const TArray<float>& Progressions = Pedestrian.Storage->LanePointProgressions;
							const float PedestrianAlong = Progressions[j] + static_cast<float>(U) * (Progressions[j + 1] - Progressions[j]) - Progressions[PedestrianData.PointsBegin];
							const double Sine = FMath::Abs(FVector2D::CrossProduct((A1 - A0).GetSafeNormal(), (B1 - B0).GetSafeNormal()));
							const float Half = Lane.HalfWidth / static_cast<float>(FMath::Max(Sine, 0.3));
							FRammsStreetCrossing* Crossing = Network.StreetCrossings.Find(Pedestrian.Handle);
							if (Crossing == nullptr)
							{
								Crossing = &Network.StreetCrossings.Add(Pedestrian.Handle, FRammsStreetCrossing{ PedestrianAlong, PedestrianAlong, PedestrianAlong - Half, PedestrianAlong + Half });
							}
							Crossing->RoadStart = FMath::Min(Crossing->RoadStart, PedestrianAlong);
							Crossing->RoadEnd = FMath::Max(Crossing->RoadEnd, PedestrianAlong);
							Crossing->EdgeStart = FMath::Min(Crossing->EdgeStart, PedestrianAlong - Half);
							Crossing->EdgeEnd = FMath::Max(Crossing->EdgeEnd, PedestrianAlong + Half);
							++CrossingPoints;
							bFound = true;
							break;
						}
					}
				}
			}
			Lane.Crossings.Sort([](const FRammsTrafficCrossingPoint& A, const FRammsTrafficCrossingPoint& B) { return A.Distance < B.Distance; });
		}
	}

	for (const FRammsTrafficLane& Lane : Network.Lanes)
	{
		if (Lane.Next.Num() == 0)
		{
			UE_LOG(LogRammsCrowd, Verbose, TEXT("Traffic: lane %s ends at (%.0f, %.0f, %.0f) without a next vehicle lane."),
				*Lane.Handle.ToString(), Lane.EndPosition.X, Lane.EndPosition.Y, Lane.EndPosition.Z);
		}
	}
	if (!Network.IsEmpty())
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("Traffic network v%d: %d vehicle lanes, %d intersections (%d signalized), %d lane conflicts (within %.0f cm), %d dead ends, %d pedestrian crossing points (%d street-crossing lanes)."),
			Network.Version, Network.Lanes.Num(), Network.Zones.Num(), Stats.Signalized, Stats.Conflicts, RammsTraffic::ConflictDistance(MaxVehicleWidth), Stats.DeadEnds, CrossingPoints, Network.StreetCrossings.Num());
		if (Stats.DeadEnds > 0)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: %d vehicle lanes lead nowhere (a road and an intersection that do not meet); cars stop at their end and start again elsewhere once nobody is near. 'log LogRammsCrowd Verbose' lists them."), Stats.DeadEnds);
		}
		if (Stats.ZoneToZone > 0)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: %d intersection lanes lead straight into another intersection (no road lane between); cars decide on the second one at the boundary, inside the first."), Stats.ZoneToZone);
		}
	}
}
