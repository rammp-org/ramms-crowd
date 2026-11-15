// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficProcessors.h"

#include "Algo/BinarySearch.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsCrowdLog.h"
#include "RammsPlayerAvatarTrait.h"
#include "RammsTrafficSubsystem.h"
#include "RammsTrafficTypes.h"
#include "RammsTrafficVehicleActor.h"
#include "ZoneGraphQuery.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTrafficDebug(
		TEXT("ramms.Traffic.Debug"),
		0,
		TEXT("Draw the Mass traffic: 1 = each vehicle's speed (km/h) and what limits it, 2 = also its corridor ahead."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarTrafficDumpInterval(
		TEXT("ramms.Traffic.DumpInterval"),
		0.0f,
		TEXT("Run ramms.Traffic.Dump every this many seconds of game time (0 = only when asked)."),
		ECVF_Default);

	/** One vehicle of this frame: pointers into its chunk (no entity is created or destroyed during the frame). */
	struct FVehicleRef
	{
		FRammsTrafficVehicleFragment* V = nullptr;
		FTransform* Transform = nullptr;
		const FRammsTrafficVisualFragment* Visual = nullptr;
		const FRammsTrafficDrivingParams* P = nullptr;
	};

	struct FOccupant
	{
		float Distance = 0.0f;
		float Speed = 0.0f;
		float HalfLength = 0.0f;
		float HalfWidth = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	/** A vehicle in an intersection (bInside), or let into it, on Lane. */
	struct FClaim
	{
		int32 Lane = INDEX_NONE;
		int32 Vehicle = INDEX_NONE;
		float HalfLength = 0.0f;
		bool bInside = false;
	};

	/** A vehicle standing at an all-way stop, for NextLane, since Arrival. */
	struct FWaiter
	{
		int32 NextLane = INDEX_NONE;
		float Arrival = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	/** A vehicle on its way into an intersection. */
	struct FApproach
	{
		int32 NextLane = INDEX_NONE;
		float DistanceToLine = 0.0f;
		float Speed = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	struct FObstaclePoint
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Radius = 0.0f;
		float Z = 0.0f;
	};

	struct FObstacleCapsule
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float Radius = 0.0f;
		float Z = 0.0f;
	};

	/** Points on a coarse hash grid: "what is within R of here". */
	struct FPointGrid
	{
		static constexpr double CellSize = 400.0;
		TMap<FIntPoint, TArray<int32, TInlineAllocator<4>>> Cells;

		static FIntPoint CellOf(const FVector2D& P)
		{
			return FIntPoint(FMath::FloorToInt32(P.X / CellSize), FMath::FloorToInt32(P.Y / CellSize));
		}

		void Add(const int32 Index, const FVector2D& P)
		{
			Cells.FindOrAdd(CellOf(P)).Add(Index);
		}

		template <typename FunctionType>
		void ForEachNear(const FVector2D& P, const double Radius, FunctionType&& Function) const
		{
			const FIntPoint Lo = CellOf(P - FVector2D(Radius, Radius));
			const FIntPoint Hi = CellOf(P + FVector2D(Radius, Radius));
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
				{
					if (const TArray<int32, TInlineAllocator<4>>* Cell = Cells.Find(FIntPoint(X, Y)))
					{
						for (const int32 Index : *Cell)
						{
							Function(Index);
						}
					}
				}
			}
		}
	};

	/** Everything one frame of traffic needs to know. */
	struct FTrafficFrame
	{
		UWorld* World = nullptr;
		const UZoneGraphSubsystem* ZoneGraph = nullptr;
		const FRammsTrafficNetwork* Net = nullptr;
		double Now = 0.0;
		float Dt = 0.0f;
		bool bHasSignals = false;
		FRammsSignalState Signals;

		TArray<FVehicleRef> Vehicles;
		TArray<TArray<FOccupant>> Occupancy;
		TArray<TArray<FClaim>> Claims;
		TArray<TArray<FWaiter>> Waiting;
		TArray<TArray<FApproach>> Approaching;

		TArray<FObstaclePoint> Pedestrians;
		FPointGrid PedestrianGrid;
		float MaxPedestrianRadius = 0.0f;
		TArray<FObstacleCapsule> Footprints;
		TArray<FVector> Players;
		TSet<FZoneGraphLaneHandle> HeldCrossings;

		bool Sample(const int32 Lane, const float Distance, FZoneGraphLaneLocation& Out) const
		{
			const FRammsTrafficLane& L = Net->Lanes[Lane];
			const FZoneGraphStorage* Storage = ZoneGraph->GetZoneGraphStorage(L.Handle.DataHandle);
			return Storage != nullptr && UE::ZoneGraph::Query::CalculateLocationAlongLane(*Storage, L.Handle, FMath::Clamp(Distance, 0.0f, L.Length), Out);
		}

		/** Distance along Lane, carried on into Next and then Next's only exit. */
		bool SampleAhead(const int32 Lane, const int32 Next, const float Distance, FZoneGraphLaneLocation& Out) const
		{
			int32 Current = Lane;
			int32 Following = Next;
			float D = Distance;
			for (int32 Hop = 0; Hop < 3; ++Hop)
			{
				const float Length = Net->Lanes[Current].Length;
				if (D <= Length || !Net->IsValidLane(Following))
				{
					return Sample(Current, FMath::Min(D, Length), Out);
				}
				D -= Length;
				Current = Following;
				Following = Net->UniqueNext(Current);
			}
			return Sample(Current, FMath::Min(D, Net->Lanes[Current].Length), Out);
		}

		double NearestPlayerDistanceSquared(const FVector& P) const
		{
			double Best = TNumericLimits<double>::Max();
			for (const FVector& Player : Players)
			{
				Best = FMath::Min(Best, FVector::DistSquared2D(P, Player));
			}
			return Best;
		}

		/** The road lane a vehicle is on approaches an intersection: that zone, else INDEX_NONE. */
		int32 ApproachZone(const FRammsTrafficVehicleFragment& V) const
		{
			if (!Net->IsValidLane(V.Lane) || Net->Lanes[V.Lane].Zone != INDEX_NONE || !Net->IsValidLane(V.NextLane))
			{
				return INDEX_NONE;
			}
			return Net->Lanes[V.NextLane].Zone;
		}
	};

	/** Front bumper to the stopping point at the end of the vehicle's (approach) lane. */
	float DistanceToLine(const FTrafficFrame& F, const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P)
	{
		return F.Net->Lanes[V.Lane].Length - (V.Distance + V.HalfLength) - P.StopLineGap;
	}

	float TurnSpeed(const FRammsTrafficLane& Lane, const FRammsTrafficDrivingParams& Params)
	{
		if (Lane.Zone == INDEX_NONE || Lane.Turn == ERammsTrafficTurn::Straight)
		{
			return 0.0f;
		}
		return Lane.Turn == ERammsTrafficTurn::Right ? Params.RightTurnSpeed : Params.LeftTurnSpeed;
	}

	/** The next lane at random (deterministic per vehicle): at an intersection mostly straight on. */
	int32 ChooseNext(const FRammsTrafficNetwork& Net, const int32 Lane, uint32& RandomState)
	{
		const TArray<int32, TInlineAllocator<4>>& Next = Net.Lanes[Lane].Next;
		if (Next.Num() <= 1)
		{
			return Next.Num() == 1 ? Next[0] : INDEX_NONE;
		}
		TArray<float, TInlineAllocator<4>> Weights;
		float Total = 0.0f;
		for (const int32 Candidate : Next)
		{
			const FRammsTrafficLane& L = Net.Lanes[Candidate];
			const float Weight = L.Zone == INDEX_NONE ? 1.0f : L.Turn == ERammsTrafficTurn::Straight ? 0.55f : L.Turn == ERammsTrafficTurn::Right ? 0.25f : 0.2f;
			Weights.Add(Weight);
			Total += Weight;
		}
		float Pick = RammsTraffic::RandomFloat(RandomState) * Total;
		for (int32 i = 0; i < Next.Num(); ++i)
		{
			Pick -= Weights[i];
			if (Pick <= 0.0f)
			{
				return Next[i];
			}
		}
		return Next.Last();
	}

	/**
	 * Cars on lanes diverging from intersection lane OnLane (same entry) that are still beside it: a car that turned
	 * off ahead of this one keeps being its leader until the lanes have separated by their two half widths.
	 * OffsetToStart: from this vehicle's front to OnLane's start (negative once it is on it).
	 */
	void SiblingLeaders(const FTrafficFrame& F, const FRammsTrafficVehicleFragment& V, const int32 Self, const int32 OnLane, const float OffsetToStart,
		float& InOutGap, float& InOutSpeed, int32& InOutLeader)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		for (const int32 Sibling : Net.Lanes[OnLane].Siblings)
		{
			for (const FOccupant& O : F.Occupancy[Sibling])
			{
				if (O.Distance > 2500.0f)
				{
					break; // sorted: the rest have turned well away
				}
				if (O.Vehicle == Self)
				{
					continue;
				}
				// Both lanes start at the entry, so distances along them compare: only a car whose centre is ahead of
				// this one's leads it (two cars side by side never both brake for each other).
				const float MyCentre = -OffsetToStart - V.HalfLength;
				const float Rear = FMath::Max(O.Distance - O.HalfLength, 0.0f);
				const float Gap = OffsetToStart + Rear;
				if (O.Distance <= MyCentre || Gap >= InOutGap)
				{
					continue; // beside or behind this one, or farther than the leader already found
				}
				FZoneGraphLaneLocation Mine, Theirs;
				if (!F.Sample(OnLane, Rear, Mine) || !F.Sample(Sibling, Rear, Theirs))
				{
					continue;
				}
				if (FVector::Dist2D(Mine.Position, Theirs.Position) < V.HalfWidth + O.HalfWidth + 40.0f)
				{
					InOutGap = Gap;
					InOutSpeed = O.Speed;
					InOutLeader = O.Vehicle;
				}
			}
		}
	}

	/** The nearest vehicle ahead: on the vehicle's lane, else its next two lanes, or one diverging from them it is still beside. */
	bool FindLeader(const FTrafficFrame& F, const FRammsTrafficVehicleFragment& V, const int32 Self, float& OutGap, float& OutSpeed, int32& OutLeader)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		const float Front = V.Distance + V.HalfLength;
		float Gap = TNumericLimits<float>::Max();
		float Speed = 0.0f;
		int32 Leader = INDEX_NONE;
		for (const FOccupant& O : F.Occupancy[V.Lane])
		{
			if (O.Vehicle == Self || O.Distance <= V.Distance)
			{
				continue;
			}
			Gap = O.Distance - O.HalfLength - Front;
			Speed = O.Speed;
			Leader = O.Vehicle;
			break;
		}
		if (Net.Lanes[V.Lane].Zone != INDEX_NONE)
		{
			SiblingLeaders(F, V, Self, V.Lane, -Front, Gap, Speed, Leader);
		}
		float Offset = Net.Lanes[V.Lane].Length - Front;
		int32 Lane = V.NextLane;
		for (int32 Hop = 0; Hop < 2 && Net.IsValidLane(Lane) && Offset < FMath::Min(Gap, 15000.0f); ++Hop)
		{
			for (const FOccupant& O : F.Occupancy[Lane])
			{
				if (O.Vehicle == Self)
				{
					continue;
				}
				if (Offset + O.Distance - O.HalfLength < Gap)
				{
					Gap = Offset + O.Distance - O.HalfLength;
					Speed = O.Speed;
					Leader = O.Vehicle;
				}
				break;
			}
			if (Net.Lanes[Lane].Zone != INDEX_NONE)
			{
				SiblingLeaders(F, V, Self, Lane, Offset, Gap, Speed, Leader);
			}
			Offset += Net.Lanes[Lane].Length;
			Lane = Net.UniqueNext(Lane);
		}
		if (Leader == INDEX_NONE)
		{
			return false;
		}
		OutGap = Gap;
		OutSpeed = Speed;
		OutLeader = Leader;
		return true;
	}

	bool IsLeft(const FRammsTrafficNetwork& Net, const int32 Lane)
	{
		return Net.IsValidLane(Lane) && Net.Lanes[Lane].Turn == ERammsTrafficTurn::Left;
	}

	/**
	 * Whether vehicle Self may enter the intersection ahead (its NextLane's zone). Returns why not, or None (and it
	 * holds a claim on its lane). Called for every approaching vehicle each frame, protected movements before left
	 * turns, so a left turn sees which oncoming vehicles have just been let in.
	 */
	ERammsTrafficYield DecideEntry(FTrafficFrame& F, const int32 Self, int32& OutBlocker)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		FRammsTrafficVehicleFragment& V = *F.Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *F.Vehicles[Self].P;
		const FRammsTrafficLane& Into = Net.Lanes[V.NextLane];
		const int32 Zone = Into.Zone;
		const float ToLine = DistanceToLine(F, V, P);
		const bool bSignalized = Into.bSignalized && F.bHasSignals;
		OutBlocker = INDEX_NONE;

		if (ToLine < -1.0f && V.bGranted)
		{
			return ERammsTrafficYield::None; // already over the line: it goes on in
		}
		// All-way stop: the time it has stood at the line, and when it got there.
		if (!bSignalized && !V.bGranted && V.Speed < 30.0f && ToLine < 150.0f)
		{
			V.StoppedAtLine += F.Dt;
			if (V.ArrivalTime < 0.0f)
			{
				V.ArrivalTime = static_cast<float>(F.Now);
			}
		}

		// Far from the line only the signal matters (braking for red starts early); no claims are taken yet.
		const float DecideDistance = FMath::Max(1500.0f, V.Speed * V.Speed / (2.0f * P.ComfortDeceleration) + 600.0f);
		const ERammsSignalLight Light = bSignalized ? F.Signals.GetLight(Into.Axis) : ERammsSignalLight::Green;
		// A permitted left turn that waited at the line for oncoming traffic goes on the amber (and the all red that
		// follows it): oncoming traffic stops for the amber.
		const int32 OtherAxis = 1 - Into.Axis;
		const bool bAllRedAfterOurs = bSignalized && F.Signals.LightX == ERammsSignalLight::Red && F.Signals.LightY == ERammsSignalLight::Red
			&& (OtherAxis == 0 ? F.Signals.SecondsToChangeX : F.Signals.SecondsToChangeY) < (Into.Axis == 0 ? F.Signals.SecondsToChangeX : F.Signals.SecondsToChangeY);
		const bool bSneak = bSignalized && Into.Turn == ERammsTrafficTurn::Left && V.bLeftWaiting && ToLine < 300.0f
			&& (Light == ERammsSignalLight::Amber || bAllRedAfterOurs);
		if (bSignalized && !bSneak && RammsTraffic::MustStopForSignal(Light, V.Speed, FMath::Max(ToLine, 0.0f), P.AmberStopDeceleration, P.MaxDeceleration))
		{
			V.bGranted = false;
			return ERammsTrafficYield::Signal;
		}
		if (ToLine > DecideDistance)
		{
			V.bGranted = false;
			return bSignalized ? ERammsTrafficYield::None : ERammsTrafficYield::StopSign;
		}

		const bool bDeadlock = V.BlockedTime > P.MaxWaitSeconds;
		if (!bSignalized && !V.bGranted)
		{
			if (V.StoppedAtLine < P.AllWayStopSeconds)
			{
				return ERammsTrafficYield::StopSign;
			}
			if (!bDeadlock)
			{
				for (const FWaiter& W : F.Waiting[Zone])
				{
					const bool bEarlier = W.Arrival < V.ArrivalTime || (W.Arrival == V.ArrivalTime && W.Vehicle < Self);
					if (W.Vehicle != Self && bEarlier && Net.Conflict(W.NextLane, V.NextLane) && !F.Vehicles[W.Vehicle].V->bGranted)
					{
						OutBlocker = W.Vehicle;
						return ERammsTrafficYield::StopSign;
					}
				}
			}
		}

		// Conflicting vehicles in the intersection always count; ones only let in count unless this one is deadlocked.
		for (const FClaim& C : F.Claims[Zone])
		{
			if (C.Vehicle != Self && Net.Conflict(C.Lane, V.NextLane) && (C.bInside || !bDeadlock))
			{
				V.bGranted = false;
				OutBlocker = C.Vehicle;
				return ERammsTrafficYield::Claim;
			}
		}

		if (!V.bGranted && !bDeadlock)
		{
			// Do not block the box: room on the exit beyond a slow queue there, less what is already let in to it.
			const int32 Exit = Net.UniqueNext(V.NextLane);
			if (Net.IsValidLane(Exit))
			{
				float Room = TNumericLimits<float>::Max();
				int32 Last = INDEX_NONE;
				for (const FOccupant& O : F.Occupancy[Exit])
				{
					if (O.Vehicle != Self)
					{
						if (O.Speed < 300.0f)
						{
							Room = O.Distance - O.HalfLength;
							Last = O.Vehicle;
						}
						break; // the car nearest the exit's start is listed first
					}
				}
				if (Room < TNumericLimits<float>::Max())
				{
					for (const FClaim& C : F.Claims[Zone])
					{
						if (C.Vehicle != Self && Net.UniqueNext(C.Lane) == Exit)
						{
							Room -= 2.0f * C.HalfLength + P.MinGap;
						}
					}
					if (Room < 2.0f * V.HalfLength + P.MinGap)
					{
						OutBlocker = Last;
						return ERammsTrafficYield::Box;
					}
				}
			}
		}

		if (bSignalized && Into.Turn == ERammsTrafficTurn::Left && !bDeadlock && !V.bGranted)
		{
			// Permitted left: oncoming straight and right-turning traffic goes first -- the ones just let in, and the
			// ones on their way within the gap. Not one standing at its line held by something else, not another left
			// turn (two opposing lefts sort themselves out through their claims), and on the amber only those
			// committed to going through. Once let in, the left turn keeps its claim: oncoming traffic waits for it.
			for (const FApproach& A : F.Approaching[Zone])
			{
				if (A.Vehicle == Self || IsLeft(Net, A.NextLane) || !Net.Conflict(A.NextLane, V.NextLane))
				{
					continue;
				}
				if (F.Signals.GetLight(Net.Lanes[A.NextLane].Axis) == ERammsSignalLight::Red)
				{
					continue;
				}
				const bool bLetIn = F.Vehicles[A.Vehicle].V->bGranted;
				const bool bComing = A.Speed > 100.0f && A.DistanceToLine / A.Speed < P.LeftTurnGapSeconds;
				if (bLetIn || (!bSneak && bComing))
				{
					V.bGranted = false;
					if (Light == ERammsSignalLight::Green && ToLine < 300.0f)
					{
						V.bLeftWaiting = true;
					}
					OutBlocker = A.Vehicle;
					return ERammsTrafficYield::OncomingLeft;
				}
			}
		}

		if (!V.bGranted)
		{
			V.bGranted = true;
			F.Claims[Zone].Add(FClaim{ V.NextLane, Self, V.HalfLength, false });
		}
		return ERammsTrafficYield::None;
	}

	/** Distance from the front bumper to the first pedestrian or robot in the vehicle's corridor ahead, or +inf. */
	float ScanCorridor(const FTrafficFrame& F, const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, ERammsTrafficYield& OutReason)
	{
		if (F.Pedestrians.IsEmpty() && F.Footprints.IsEmpty())
		{
			return TNumericLimits<float>::Max();
		}
		const float Look = FMath::Max(P.MinLookahead, V.Speed * P.LookaheadSeconds + 800.0f);
		const float HalfWidth = V.HalfWidth + P.CorridorMargin;
		const float Front = V.Distance + V.HalfLength;
		constexpr float Step = 150.0f;
		FZoneGraphLaneLocation Location;
		for (float S = 0.0f; S <= Look; S += Step)
		{
			if (!F.SampleAhead(V.Lane, V.NextLane, Front + S, Location))
			{
				break;
			}
			const FVector2D Here(Location.Position);
			const float Z = static_cast<float>(Location.Position.Z);
			bool bPedestrian = false;
			F.PedestrianGrid.ForEachNear(Here, HalfWidth + F.MaxPedestrianRadius, [&](const int32 Index)
			{
				const FObstaclePoint& O = F.Pedestrians[Index];
				if (!bPedestrian && FMath::Abs(O.Z - Z) < 250.0f && FVector2D::DistSquared(Here, O.Position) < FMath::Square(HalfWidth + O.Radius))
				{
					bPedestrian = true;
				}
			});
			if (bPedestrian)
			{
				OutReason = ERammsTrafficYield::Pedestrian;
				return S;
			}
			for (const FObstacleCapsule& C : F.Footprints)
			{
				if (FMath::Abs(C.Z - Z) < 300.0f && FVector2D::Distance(Here, FMath::ClosestPointOnSegment2D(Here, C.A, C.B)) < HalfWidth + C.Radius)
				{
					OutReason = ERammsTrafficYield::Robot;
					return S;
				}
			}
		}
		return TNumericLimits<float>::Max();
	}

	/** Ground height under P (the generated road, kerbs, humps: world static only), or P.Z when nothing is near. */
	float TraceGround(const UWorld* World, const FVector& P)
	{
		FHitResult Hit;
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(RammsTrafficGround), true);
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.0, 0.0, 80.0), P - FVector(0.0, 0.0, 120.0), Objects, Params)
			&& FMath::Abs(Hit.ImpactPoint.Z - P.Z) < 60.0)
		{
			return static_cast<float>(Hit.ImpactPoint.Z);
		}
		return static_cast<float>(P.Z);
	}

	/** Base centre on the road, yaw along the axle chord, pitch from the axles' heights. */
	void ComputePose(const FTrafficFrame& F, FRammsTrafficVehicleFragment& V, const bool bTraceGround, FTransform& OutTransform)
	{
		const float Axle = V.HalfLength * 0.75f;
		FZoneGraphLaneLocation Center, FrontLocation, RearLocation;
		if (!F.SampleAhead(V.Lane, V.NextLane, V.Distance, Center))
		{
			return;
		}
		FVector FrontPosition = F.SampleAhead(V.Lane, V.NextLane, V.Distance + Axle, FrontLocation) ? FrontLocation.Position : Center.Position + Center.Direction * Axle;
		FVector RearPosition;
		if (V.Distance - Axle >= 0.0f && F.Sample(V.Lane, V.Distance - Axle, RearLocation))
		{
			RearPosition = RearLocation.Position;
		}
		else if (F.Sample(V.Lane, 0.0f, RearLocation))
		{
			// Just onto the lane: carry its start back along its direction.
			RearPosition = RearLocation.Position - RearLocation.Direction * (Axle - V.Distance);
		}
		else
		{
			RearPosition = Center.Position - Center.Direction * Axle;
		}
		if (bTraceGround)
		{
			FrontPosition.Z = TraceGround(F.World, FrontPosition);
			RearPosition.Z = TraceGround(F.World, RearPosition);
		}
		const FVector Chord = FrontPosition - RearPosition;
		const double Horizontal = FVector2D(Chord).Size();
		const FVector Flat = Horizontal > 1.0 ? FVector(Chord.X, Chord.Y, 0.0) / Horizontal : FVector(Center.Direction.X, Center.Direction.Y, 0.0).GetSafeNormal();
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		const double Pitch = Horizontal > 1.0 ? FMath::RadiansToDegrees(FMath::Atan2(Chord.Z, Horizontal)) : 0.0;
		const FVector Base(Center.Position.X, Center.Position.Y, (FrontPosition.Z + RearPosition.Z) * 0.5);
		OutTransform = FTransform(FRotator(Pitch, Yaw, 0.0), Base);
		V.Velocity = OutTransform.GetRotation().GetForwardVector() * V.Speed;
	}

	/** Puts the vehicle somewhere free on a road lane, away from the player. */
	bool Respawn(FTrafficFrame& F, FRammsTrafficVehicleFragment& V, const int32 Self)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		for (int32 Try = 0; Try < 24 && Net.Lanes.Num() > 0; ++Try)
		{
			const int32 Lane = FMath::Min(static_cast<int32>(RammsTraffic::RandomFloat(V.RandomState) * Net.Lanes.Num()), Net.Lanes.Num() - 1);
			const FRammsTrafficLane& L = Net.Lanes[Lane];
			const float Margin = V.HalfLength + 300.0f;
			if (L.Zone != INDEX_NONE || L.Length < 2.0f * Margin + 200.0f)
			{
				continue;
			}
			const float Distance = Margin + RammsTraffic::RandomFloat(V.RandomState) * (L.Length - 2.0f * Margin);
			FZoneGraphLaneLocation Location;
			if (!F.Sample(Lane, Distance, Location) || F.NearestPlayerDistanceSquared(Location.Position) < FMath::Square(4000.0))
			{
				continue;
			}
			bool bFree = true;
			for (const FOccupant& O : F.Occupancy[Lane])
			{
				bFree &= FMath::Abs(O.Distance - Distance) > 1500.0f;
			}
			if (!bFree)
			{
				continue;
			}
			V.Lane = Lane;
			V.NextLane = ChooseNext(Net, Lane, V.RandomState);
			V.PrevLane = INDEX_NONE;
			V.Distance = Distance;
			V.Speed = V.DesiredSpeed * 0.5f;
			V.bGranted = false;
			V.bLeftWaiting = false;
			V.ArrivalTime = -1.0f;
			V.StoppedAtLine = 0.0f;
			V.BlockedTime = 0.0f;
			V.bTeleport = true;
			V.NetworkVersion = Net.Version;
			TArray<FOccupant>& Occupants = F.Occupancy[Lane];
			const int32 At = Algo::LowerBoundBy(Occupants, Distance, &FOccupant::Distance);
			Occupants.Insert(FOccupant{ Distance, V.Speed, V.HalfLength, V.HalfWidth, Self }, At);
			return true;
		}
		return false;
	}

	void AdvanceLanes(FTrafficFrame& F, FRammsTrafficVehicleFragment& V, const int32 Self)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		for (int32 Hop = 0; Hop < 4 && V.Distance > Net.Lanes[V.Lane].Length; ++Hop)
		{
			if (!Net.IsValidLane(V.NextLane))
			{
				// A dead end: start again elsewhere (or wait at the end when there is nowhere).
				if (!Respawn(F, V, Self))
				{
					V.Distance = Net.Lanes[V.Lane].Length;
					V.Speed = 0.0f;
				}
				return;
			}
			const int32 FromLane = V.Lane;
			const int32 FromZone = Net.Lanes[FromLane].Zone;
			V.Distance -= Net.Lanes[FromLane].Length;
			V.Lane = V.NextLane;
			V.NextLane = ChooseNext(Net, V.Lane, V.RandomState);
			const int32 ToZone = Net.Lanes[V.Lane].Zone;
			if (ToZone == INDEX_NONE)
			{
				V.bGranted = false;
				V.PrevLane = FromZone != INDEX_NONE ? FromLane : INDEX_NONE;
			}
			else if (FromZone == INDEX_NONE)
			{
				// Into the intersection: its stop is over.
				V.StoppedAtLine = 0.0f;
				V.ArrivalTime = -1.0f;
				V.BlockedTime = 0.0f;
				V.bLeftWaiting = false;
				V.PrevLane = INDEX_NONE;
			}
		}
		if (V.PrevLane != INDEX_NONE && V.Distance - V.HalfLength > 0.0f)
		{
			V.PrevLane = INDEX_NONE; // its rear is out of the intersection too
		}
	}

	void UpdateVehicle(FTrafficFrame& F, const int32 Self)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		FRammsTrafficVehicleFragment& V = *F.Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *F.Vehicles[Self].P;
		const FRammsTrafficVisualFragment& Visual = *F.Vehicles[Self].Visual;
		FTransform& Transform = *F.Vehicles[Self].Transform;
		if (V.NetworkVersion != Net.Version || !Net.IsValidLane(V.Lane))
		{
			// Placed on another network (the ZoneGraph changed): find a new place.
			if (!Respawn(F, V, Self))
			{
				V.Speed = 0.0f;
				return;
			}
		}
		if (!Net.IsValidLane(V.NextLane))
		{
			V.NextLane = ChooseNext(Net, V.Lane, V.RandomState);
		}

		const FRammsTrafficLane& Lane = Net.Lanes[V.Lane];
		const float Front = V.Distance + V.HalfLength;
		const float ToLaneEnd = Lane.Length - Front;

		FRammsIdm Idm;
		Idm.DesiredSpeed = V.DesiredSpeed;
		Idm.MaxAcceleration = P.MaxAcceleration;
		Idm.ComfortDeceleration = P.ComfortDeceleration;
		Idm.TimeHeadway = P.TimeHeadway;
		Idm.MinGap = P.MinGap;
		float MaxSpeed = V.DesiredSpeed;
		const float TurnHere = TurnSpeed(Lane, P);
		if (TurnHere > 0.0f)
		{
			Idm.DesiredSpeed = FMath::Min(Idm.DesiredSpeed, static_cast<double>(TurnHere));
			MaxSpeed = FMath::Min(MaxSpeed, TurnHere);
		}
		FRammsIdm StopIdm = Idm;
		StopIdm.MinGap = 0.0;

		double Accel = RammsTraffic::IdmFree(V.Speed, Idm);
		ERammsTrafficYield Reason = ERammsTrafficYield::None;
		int32 Blocker = INDEX_NONE;
		float LimitGap = 0.0f;
		double HardStop = -1.0;
		auto Constrain = [&Accel, &Reason, &Blocker, &LimitGap, &HardStop](const double A, const ERammsTrafficYield Why, const double StopAt, const int32 Who, const float Gap)
		{
			if (A < Accel)
			{
				Accel = A;
				Reason = Why;
				Blocker = Who;
				LimitGap = Gap;
			}
			if (StopAt >= 0.0 && (HardStop < 0.0 || StopAt < HardStop))
			{
				HardStop = StopAt;
			}
		};

		// Slow for the turn ahead.
		if (Net.IsValidLane(V.NextLane))
		{
			const float TurnNext = TurnSpeed(Net.Lanes[V.NextLane], P);
			if (TurnNext > 0.0f)
			{
				Constrain(RammsTraffic::SlowToCap(V.Speed, TurnNext, ToLaneEnd), ERammsTrafficYield::Turn, -1.0, INDEX_NONE, ToLaneEnd);
			}
		}

		// Car following.
		float LeaderGap = 0.0f, LeaderSpeed = 0.0f;
		int32 Leader = INDEX_NONE;
		if (FindLeader(F, V, Self, LeaderGap, LeaderSpeed, Leader))
		{
			Constrain(RammsTraffic::IdmFollow(V.Speed, LeaderGap, LeaderSpeed, Idm), ERammsTrafficYield::Leader, -1.0, Leader, LeaderGap);
		}

		// The intersection ahead (decided in the entry pass).
		if (F.ApproachZone(V) != INDEX_NONE && V.Entry != ERammsTrafficYield::None)
		{
			const float ToLine = ToLaneEnd - P.StopLineGap;
			// Past the line without leave (it overshot): stand where it is.
			Constrain(RammsTraffic::IdmFollow(V.Speed, FMath::Max(ToLine, 0.0f), 0.0, StopIdm), V.Entry, FMath::Max(ToLine, 0.0f), V.EntryBlocker, ToLine);
		}

		// Pedestrians and the robot in the way.
		ERammsTrafficYield ObstacleReason = ERammsTrafficYield::None;
		const float Obstacle = ScanCorridor(F, V, P, ObstacleReason);
		if (Obstacle < TNumericLimits<float>::Max())
		{
			const float Gap = Obstacle - P.ObstacleGap;
			Constrain(RammsTraffic::IdmFollow(V.Speed, Gap, 0.0, StopIdm), ObstacleReason, FMath::Max(Gap, 0.0f), INDEX_NONE, Gap);
		}

		Accel = FMath::Clamp(Accel, -static_cast<double>(P.MaxDeceleration), static_cast<double>(P.MaxAcceleration));
		double Speed = V.Speed;
		const double Travel = RammsTraffic::Advance(Speed, Accel, F.Dt, MaxSpeed * 1.05, HardStop);
		V.Speed = static_cast<float>(Speed);
		V.Acceleration = static_cast<float>(Accel);
		V.Yield = Reason;
		V.Blocker = Blocker;
		V.LimitGap = LimitGap;
		// The deadlock breaker counts only time stood at the line held by other vehicles' rules; time at a red light
		// neither counts nor resets it, driving on resets it.
		const bool bHeldByVehicles = V.Entry == ERammsTrafficYield::Claim || V.Entry == ERammsTrafficYield::Box
			|| V.Entry == ERammsTrafficYield::OncomingLeft || (V.Entry == ERammsTrafficYield::StopSign && V.EntryBlocker != INDEX_NONE);
		if (V.Speed < 10.0f && bHeldByVehicles)
		{
			V.BlockedTime += F.Dt;
		}
		else if (V.Speed > 100.0f)
		{
			V.BlockedTime = 0.0f;
		}
		V.Distance += static_cast<float>(Travel);
		AdvanceLanes(F, V, Self);

		const bool bTrace = Visual.GroundTraceRadius > 0.0f && F.NearestPlayerDistanceSquared(Transform.GetLocation()) < FMath::Square(static_cast<double>(Visual.GroundTraceRadius));
		ComputePose(F, V, bTrace, Transform);
	}

	/** The pedestrian lanes this vehicle is over or will cross within CrossingHoldSeconds, if it is going there. */
	void HoldCrossings(FTrafficFrame& F, const int32 Self)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		const FRammsTrafficVehicleFragment& V = *F.Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *F.Vehicles[Self].P;
		if (!Net.IsValidLane(V.Lane))
		{
			return;
		}
		const FRammsTrafficLane& Lane = Net.Lanes[V.Lane];
		const bool bInside = Lane.Zone != INDEX_NONE;
		const bool bApproaching = F.ApproachZone(V) != INDEX_NONE;
		// Driving on, or standing only for walkers already in its way (holding the others lets it go once they are past).
		// Not a car stopped for a light, a queue or other traffic.
		const bool bGoing = V.Speed > 50.0f || V.Yield == ERammsTrafficYield::Pedestrian;
		if (!bGoing)
		{
			return;
		}
		const float Front = V.Distance + V.HalfLength;
		const float Back = V.Distance - V.HalfLength;
		const float Reach = Front + FMath::Clamp(V.Speed * P.CrossingHoldSeconds, 300.0f, 1200.0f);
		for (const FRammsTrafficCrossingPoint& X : Lane.Crossings)
		{
			if (X.Distance >= Back - 50.0f && X.Distance <= Reach)
			{
				F.HeldCrossings.Add(X.PedestrianLane);
			}
		}
		// On into the next lane: always out of an intersection or along a road, into one only when let in.
		if (Net.IsValidLane(V.NextLane) && (bInside || !bApproaching || V.bGranted))
		{
			const float Offset = Lane.Length;
			for (const FRammsTrafficCrossingPoint& X : Net.Lanes[V.NextLane].Crossings)
			{
				if (Offset + X.Distance > Reach)
				{
					break;
				}
				F.HeldCrossings.Add(X.PedestrianLane);
			}
		}
	}

	const TCHAR* YieldName(const ERammsTrafficYield Yield)
	{
		switch (Yield)
		{
		case ERammsTrafficYield::Leader: return TEXT("leader");
		case ERammsTrafficYield::Signal: return TEXT("signal");
		case ERammsTrafficYield::StopSign: return TEXT("stop sign");
		case ERammsTrafficYield::Claim: return TEXT("claim");
		case ERammsTrafficYield::Box: return TEXT("box");
		case ERammsTrafficYield::OncomingLeft: return TEXT("left-yield");
		case ERammsTrafficYield::Pedestrian: return TEXT("pedestrian");
		case ERammsTrafficYield::Robot: return TEXT("robot");
		case ERammsTrafficYield::Turn: return TEXT("turn");
		default: return TEXT("free");
		}
	}

	const TCHAR* LightName(const ERammsSignalLight Light)
	{
		return Light == ERammsSignalLight::Green ? TEXT("G") : Light == ERammsSignalLight::Amber ? TEXT("A") : TEXT("R");
	}

	const TCHAR* TurnName(const ERammsTrafficTurn Turn)
	{
		return Turn == ERammsTrafficTurn::Left ? TEXT("left") : Turn == ERammsTrafficTurn::Right ? TEXT("right") : TEXT("straight");
	}

	FString VehicleName(const FTrafficFrame& F, const int32 Index)
	{
		if (!F.Vehicles.IsValidIndex(Index))
		{
			return TEXT("-");
		}
		const ARammsTrafficVehicleActor* Actor = F.Vehicles[Index].Visual->Actor.Get();
		return Actor != nullptr ? Actor->GetName() : FString::Printf(TEXT("#%d"), Index);
	}

	FString LaneName(const FTrafficFrame& F, const int32 Lane)
	{
		if (!F.Net->IsValidLane(Lane))
		{
			return TEXT("none");
		}
		const FRammsTrafficLane& L = F.Net->Lanes[Lane];
		if (L.Zone == INDEX_NONE)
		{
			return FString::Printf(TEXT("L%d road%s"), Lane, L.bApproach ? TEXT(" approach") : TEXT(""));
		}
		return FString::Printf(TEXT("L%d I%d %s axis %s%s"), Lane, L.Zone, TurnName(L.Turn), L.Axis == 0 ? TEXT("X") : TEXT("Y"), L.bSignalized ? TEXT(" signal") : TEXT(" stop"));
	}

	/** ramms.Traffic.Dump: every vehicle and every intersection's claims, to LogRammsCrowd. */
	void DumpTraffic(const FTrafficFrame& F)
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("[Traffic dump] t=%.1f signals %s X=%s (%.1f s) Y=%s (%.1f s) walkAlongX=%d walkAlongY=%d; %d vehicles, %d held crossings"),
			F.Now, F.bHasSignals ? TEXT("on") : TEXT("off (all-way stops)"), LightName(F.Signals.LightX), F.Signals.SecondsToChangeX, LightName(F.Signals.LightY), F.Signals.SecondsToChangeY,
			static_cast<int32>(F.Signals.WalkAlongX), static_cast<int32>(F.Signals.WalkAlongY), F.Vehicles.Num(), F.HeldCrossings.Num());
		for (int32 i = 0; i < F.Vehicles.Num(); ++i)
		{
			const FRammsTrafficVehicleFragment& V = *F.Vehicles[i].V;
			if (!F.Net->IsValidLane(V.Lane))
			{
				UE_LOG(LogRammsCrowd, Display, TEXT("  %s: off the network"), *VehicleName(F, i));
				continue;
			}
			const FVector Location = F.Vehicles[i].Transform->GetLocation();
			const int32 Zone = F.ApproachZone(V);
			FString Waits = TEXT("-");
			if (Zone != INDEX_NONE)
			{
				const FRammsTrafficDrivingParams& P = *F.Vehicles[i].P;
				const FRammsTrafficLane& Into = F.Net->Lanes[V.NextLane];
				const FString Detail = V.Entry == ERammsTrafficYield::Signal
					? FString::Printf(TEXT(" (%s for axis %s)"), LightName(F.Signals.GetLight(Into.Axis)), Into.Axis == 0 ? TEXT("X") : TEXT("Y"))
					: V.EntryBlocker != INDEX_NONE ? FString::Printf(TEXT(" for %s"), *VehicleName(F, V.EntryBlocker)) : FString();
				Waits = FString::Printf(TEXT("I%d %s: %s%s, to line %.0f cm"), Zone, TurnName(Into.Turn),
					V.Entry == ERammsTrafficYield::None ? (V.bGranted ? TEXT("let in") : TEXT("approaching")) : YieldName(V.Entry),
					*Detail, DistanceToLine(F, V, P));
			}
			UE_LOG(LogRammsCrowd, Display, TEXT("  %s at (%.0f, %.0f, %.0f) yaw %.0f: %s d=%.0f/%.0f v=%.1f km/h a=%.2f m/s2 | next %s | limit %s gap %.0f cm by %s | entry %s | granted %d leftWaiting %d blocked %.1f s stopped %.1f s arrival %.1f"),
				*VehicleName(F, i), Location.X, Location.Y, Location.Z, F.Vehicles[i].Transform->Rotator().Yaw,
				*LaneName(F, V.Lane), V.Distance, F.Net->Lanes[V.Lane].Length, V.Speed * 0.036f, V.Acceleration * 0.01f,
				*LaneName(F, V.NextLane), YieldName(V.Yield), V.LimitGap, *VehicleName(F, V.Blocker), *Waits,
				V.bGranted ? 1 : 0, V.bLeftWaiting ? 1 : 0, V.BlockedTime, V.StoppedAtLine, V.ArrivalTime);
		}
		for (int32 Zone = 0; Zone < F.Claims.Num(); ++Zone)
		{
			if (F.Claims[Zone].IsEmpty() && F.Approaching[Zone].IsEmpty())
			{
				continue;
			}
			FString Claims, Approaching;
			for (const FClaim& C : F.Claims[Zone])
			{
				Claims += FString::Printf(TEXT(" L%d:%s%s"), C.Lane, *VehicleName(F, C.Vehicle), C.bInside ? TEXT("(in)") : TEXT("(let in)"));
			}
			for (const FApproach& A : F.Approaching[Zone])
			{
				Approaching += FString::Printf(TEXT(" %s->L%d %s %.0fcm %.0fkm/h"), *VehicleName(F, A.Vehicle), A.NextLane, TurnName(F.Net->Lanes[A.NextLane].Turn), A.DistanceToLine, A.Speed * 0.036f);
			}
			const FVector C = F.Net->Zones[Zone].Center;
			UE_LOG(LogRammsCrowd, Display, TEXT("  I%d at (%.0f, %.0f) %s: claims%s | approaching%s"), Zone, C.X, C.Y,
				F.Net->Zones[Zone].bSignalized ? TEXT("signalized") : TEXT("all-way stop"), Claims.IsEmpty() ? TEXT(" none") : *Claims, Approaching.IsEmpty() ? TEXT(" none") : *Approaching);
		}
	}

	/** ramms.Traffic.Debug: what limits the car over it, and its corridor ahead (Level 2). */
	void DrawVehicleDebug(const UWorld* World, const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, const FTransform& Transform, const int32 Level)
	{
#if ENABLE_DRAW_DEBUG
		const FColor Color = V.Yield == ERammsTrafficYield::None ? FColor::Green
			: (V.Yield == ERammsTrafficYield::Pedestrian || V.Yield == ERammsTrafficYield::Robot) ? FColor::Red : FColor::Yellow;
		DrawDebugString(World, Transform.GetLocation() + FVector(0.0, 0.0, 260.0), FString::Printf(TEXT("%.0f km/h %s"), V.Speed * 0.036f, YieldName(V.Yield)), nullptr, Color, 0.0f, true);
		if (Level > 1)
		{
			const FVector Ahead = Transform.GetLocation() + Transform.GetRotation().GetForwardVector() * (V.HalfLength + FMath::Max(P.MinLookahead, V.Speed * P.LookaheadSeconds + 800.0f));
			DrawDebugLine(World, Transform.GetLocation() + FVector(0.0, 0.0, 50.0), Ahead + FVector(0.0, 0.0, 50.0), Color, false, -1.0f, 0, 4.0f);
		}
#endif
	}

	void GatherPlayers(const UWorld& World, TArray<FVector>& OutPlayers)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (Controller == nullptr)
			{
				continue;
			}
			if (const APawn* Pawn = Controller->GetPawn())
			{
				OutPlayers.Add(Pawn->GetActorLocation());
			}
			else if (Controller->PlayerCameraManager != nullptr)
			{
				OutPlayers.Add(Controller->PlayerCameraManager->GetCameraLocation());
			}
		}
	}
}

//----------------------------------------------------------------------//
// URammsTrafficVehicleProcessor
//----------------------------------------------------------------------//

URammsTrafficVehicleProcessor::URammsTrafficVehicleProcessor()
	: VehicleQuery(*this)
	, AvatarQuery(*this)
	, CrowdQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	// After the crowd has moved this frame (pedestrian positions are current), before actors follow Mass.
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
	bRequiresGameThreadExecution = true;
}

void URammsTrafficVehicleProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	VehicleQuery.AddTagRequirement<FRammsTrafficVehicleTag>(EMassFragmentPresence::All);
	VehicleQuery.AddRequirement<FRammsTrafficVehicleFragment>(EMassFragmentAccess::ReadWrite);
	VehicleQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	VehicleQuery.AddRequirement<FRammsTrafficVisualFragment>(EMassFragmentAccess::ReadOnly);
	VehicleQuery.AddConstSharedRequirement<FRammsTrafficDrivingParams>(EMassFragmentPresence::All);

	AvatarQuery.AddTagRequirement<FRammsPlayerAvatarTag>(EMassFragmentPresence::All);
	AvatarQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FMassAvoidanceColliderFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

	CrowdQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	CrowdQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	CrowdQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void URammsTrafficVehicleProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	URammsTrafficSubsystem* Traffic = UWorld::GetSubsystem<URammsTrafficSubsystem>(World);
	const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World);
	if (World == nullptr || Traffic == nullptr || ZoneGraph == nullptr)
	{
		return;
	}
	const FRammsTrafficNetwork* Net = Traffic->GetNetwork();
	if (Net == nullptr)
	{
		return;
	}

	FTrafficFrame F;
	F.World = World;
	F.ZoneGraph = ZoneGraph;
	F.Net = Net;
	F.Now = World->GetTimeSeconds();
	// A hitch (level streaming, a breakpoint) must not fling the traffic.
	F.Dt = FMath::Min(Context.GetDeltaTimeSeconds(), 0.1f);
	F.bHasSignals = Traffic->GetSignalState(F.Signals);
	F.Occupancy.SetNum(Net->Lanes.Num());
	F.Claims.SetNum(Net->Zones.Num());
	F.Waiting.SetNum(Net->Zones.Num());
	F.Approaching.SetNum(Net->Zones.Num());
	GatherPlayers(*World, F.Players);

	// The vehicles (pointers into their chunks), each with its next lane chosen.
	float Predict = 0.0f;
	VehicleQuery.ForEachEntityChunk(Context, [&F, &Predict](FMassExecutionContext& Ctx)
	{
		const FRammsTrafficDrivingParams& P = Ctx.GetConstSharedFragment<FRammsTrafficDrivingParams>();
		Predict = FMath::Max(Predict, P.PredictSeconds);
		const TArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetMutableFragmentView<FRammsTrafficVehicleFragment>();
		const TArrayView<FTransformFragment> TransformList = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FRammsTrafficVisualFragment> VisualList = Ctx.GetFragmentView<FRammsTrafficVisualFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			FVehicleRef& Ref = F.Vehicles.AddDefaulted_GetRef();
			Ref.V = &VehicleList[It];
			Ref.Transform = &TransformList[It].GetMutableTransform();
			Ref.Visual = &VisualList[It];
			Ref.P = &P;
		}
	});
	if (F.Vehicles.IsEmpty())
	{
		return;
	}

	// The traffic as it stands: lane occupancy, intersection claims, all-way stop arrivals, approaches.
	for (int32 i = 0; i < F.Vehicles.Num(); ++i)
	{
		FRammsTrafficVehicleFragment& V = *F.Vehicles[i].V;
		if (V.NetworkVersion != Net->Version || !Net->IsValidLane(V.Lane))
		{
			continue;
		}
		if (!Net->IsValidLane(V.NextLane))
		{
			V.NextLane = ChooseNext(*Net, V.Lane, V.RandomState);
		}
		F.Occupancy[V.Lane].Add(FOccupant{ V.Distance, V.Speed, V.HalfLength, V.HalfWidth, i });
		const FRammsTrafficLane& Lane = Net->Lanes[V.Lane];
		if (Lane.Zone != INDEX_NONE)
		{
			F.Claims[Lane.Zone].Add(FClaim{ V.Lane, i, V.HalfLength, true });
		}
		else
		{
			// Rear still in the intersection it just left.
			if (Net->IsValidLane(V.PrevLane) && Net->Lanes[V.PrevLane].Zone != INDEX_NONE)
			{
				F.Claims[Net->Lanes[V.PrevLane].Zone].Add(FClaim{ V.PrevLane, i, V.HalfLength, true });
			}
			const int32 Zone = F.ApproachZone(V);
			if (Zone != INDEX_NONE)
			{
				if (V.bGranted)
				{
					F.Claims[Zone].Add(FClaim{ V.NextLane, i, V.HalfLength, false });
				}
				F.Approaching[Zone].Add(FApproach{ V.NextLane, DistanceToLine(F, V, *F.Vehicles[i].P), V.Speed, i });
				if (V.ArrivalTime >= 0.0f)
				{
					F.Waiting[Zone].Add(FWaiter{ V.NextLane, V.ArrivalTime, i });
				}
			}
		}
	}
	for (TArray<FOccupant>& Occupants : F.Occupancy)
	{
		Occupants.Sort([](const FOccupant& A, const FOccupant& B) { return A.Distance < B.Distance; });
	}

	// Robot footprints: the player avatar entities (pills or circles around the robot), now and PredictSeconds ahead.
	AvatarQuery.ForEachEntityChunk(Context, [&F, Predict](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassAvoidanceColliderFragment> ColliderList = Ctx.GetFragmentView<FMassAvoidanceColliderFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Ctx.GetFragmentView<FMassVelocityFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			const FTransform& AvatarTransform = TransformList[It].GetTransform();
			const FVector2D Center(AvatarTransform.GetLocation());
			const FVector2D Forward = FVector2D(AvatarTransform.GetRotation().GetForwardVector()).GetSafeNormal();
			const FMassAvoidanceColliderFragment& Collider = ColliderList[It];
			FObstacleCapsule Capsule;
			double HalfLength = 0.0;
			if (Collider.Type == EMassColliderType::Pill)
			{
				const FMassPillCollider Pill = Collider.GetPillCollider();
				Capsule.Radius = Pill.Radius;
				HalfLength = Pill.HalfLength;
			}
			else
			{
				Capsule.Radius = Collider.GetCircleCollider().Radius;
			}
			Capsule.A = Center - Forward * HalfLength;
			Capsule.B = Center + Forward * HalfLength;
			Capsule.Z = static_cast<float>(AvatarTransform.GetLocation().Z);
			F.Footprints.Add(Capsule);
			if (!VelocityList.IsEmpty())
			{
				const FVector2D Ahead = FVector2D(VelocityList[It].Value) * Predict;
				if (!Ahead.IsNearlyZero())
				{
					FObstacleCapsule Later = Capsule;
					Later.A += Ahead;
					Later.B += Ahead;
					F.Footprints.Add(Later);
				}
			}
		}
	});
	if (F.Footprints.IsEmpty())
	{
		// No avatar entity (a robot without the RAMMS Player Avatar trait): the player pawns' collision bounds.
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* Pawn = It->Get() != nullptr ? It->Get()->GetPawn() : nullptr;
			if (Pawn == nullptr)
			{
				continue;
			}
			FVector Origin, Extent;
			Pawn->GetActorBounds(true, Origin, Extent);
			if (Extent.IsNearlyZero())
			{
				continue;
			}
			FObstacleCapsule Capsule;
			const bool bAlongX = Extent.X >= Extent.Y;
			Capsule.Radius = static_cast<float>(FMath::Min(Extent.X, Extent.Y));
			const FVector2D Axis = bAlongX ? FVector2D(Extent.X - Extent.Y, 0.0) : FVector2D(0.0, Extent.Y - Extent.X);
			Capsule.A = FVector2D(Origin) - Axis;
			Capsule.B = FVector2D(Origin) + Axis;
			Capsule.Z = static_cast<float>(Origin.Z - Extent.Z);
			F.Footprints.Add(Capsule);
			const FVector2D Ahead = FVector2D(Pawn->GetVelocity()) * Predict;
			if (!Ahead.IsNearlyZero())
			{
				FObstacleCapsule Later = Capsule;
				Later.A += Ahead;
				Later.B += Ahead;
				F.Footprints.Add(Later);
			}
		}
	}

	// Pedestrians, now and where they will be shortly (about to step off the curb).
	CrowdQuery.ForEachEntityChunk(Context, [&F, Predict](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Ctx.GetFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Ctx.GetFragmentView<FAgentRadiusFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			const FVector Location = TransformList[It].GetTransform().GetLocation();
			const float Radius = RadiusList.IsEmpty() ? 35.0f : RadiusList[It].Radius;
			const FVector2D Velocity = VelocityList.IsEmpty() ? FVector2D::ZeroVector : FVector2D(VelocityList[It].Value);
			const int32 Steps = Velocity.IsNearlyZero(1.0) ? 0 : 2;
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				FObstaclePoint Point;
				Point.Position = FVector2D(Location) + Velocity * (Predict * Step / FMath::Max(Steps, 1));
				Point.Radius = Radius;
				Point.Z = static_cast<float>(Location.Z);
				F.PedestrianGrid.Add(F.Pedestrians.Add(Point), Point.Position);
			}
			F.MaxPedestrianRadius = FMath::Max(F.MaxPedestrianRadius, Radius);
		}
	});

	// Who may enter which intersection: per intersection, protected movements first (nearest the line first), then
	// left turns, so a left turn sees the oncoming vehicles let in this frame and the claims they took.
	for (int32 i = 0; i < F.Vehicles.Num(); ++i)
	{
		F.Vehicles[i].V->Entry = ERammsTrafficYield::None;
		F.Vehicles[i].V->EntryBlocker = INDEX_NONE;
	}
	TArray<int32> Order;
	for (int32 Zone = 0; Zone < F.Approaching.Num(); ++Zone)
	{
		Order.Reset();
		for (const FApproach& A : F.Approaching[Zone])
		{
			Order.Add(A.Vehicle);
		}
		Order.Sort([&F](const int32 A, const int32 B)
		{
			const bool bLeftA = IsLeft(*F.Net, F.Vehicles[A].V->NextLane);
			const bool bLeftB = IsLeft(*F.Net, F.Vehicles[B].V->NextLane);
			if (bLeftA != bLeftB)
			{
				return !bLeftA;
			}
			return DistanceToLine(F, *F.Vehicles[A].V, *F.Vehicles[A].P) < DistanceToLine(F, *F.Vehicles[B].V, *F.Vehicles[B].P);
		});
		for (const int32 i : Order)
		{
			FRammsTrafficVehicleFragment& V = *F.Vehicles[i].V;
			V.Entry = DecideEntry(F, i, V.EntryBlocker);
		}
	}

	// Drive.
	const int32 Debug = CVarTrafficDebug.GetValueOnGameThread();
	for (int32 i = 0; i < F.Vehicles.Num(); ++i)
	{
		UpdateVehicle(F, i);
		HoldCrossings(F, i);
		if (Debug > 0)
		{
			DrawVehicleDebug(F.World, *F.Vehicles[i].V, *F.Vehicles[i].P, *F.Vehicles[i].Transform, Debug);
		}
	}

	// ramms.Traffic.Dump (and ramms.Traffic.DumpInterval).
	const float Interval = CVarTrafficDumpInterval.GetValueOnGameThread();
	const int32 Requests = URammsTrafficSubsystem::GetDumpRequestSerial();
	if (Requests != SeenDumpRequests || (Interval > 0.0f && F.Now - LastDumpTime >= Interval))
	{
		SeenDumpRequests = Requests;
		LastDumpTime = F.Now;
		DumpTraffic(F);
	}

	Traffic->SetHeldCrossings(MoveTemp(F.HeldCrossings));
}

//----------------------------------------------------------------------//
// URammsTrafficVisualProcessor
//----------------------------------------------------------------------//

URammsTrafficVisualProcessor::URammsTrafficVisualProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	bRequiresGameThreadExecution = true;
}

void URammsTrafficVisualProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FRammsTrafficVehicleTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FRammsTrafficVehicleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRammsTrafficVisualFragment>(EMassFragmentAccess::ReadWrite);
}

void URammsTrafficVisualProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = Context.GetWorld();
	if (World == nullptr)
	{
		return;
	}
	TArray<FVector> Players;
	GatherPlayers(*World, Players);
	EntityQuery.ForEachEntityChunk(Context, [&Players](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FTransformFragment> TransformList = Ctx.GetFragmentView<FTransformFragment>();
		const TArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetMutableFragmentView<FRammsTrafficVehicleFragment>();
		const TArrayView<FRammsTrafficVisualFragment> VisualList = Ctx.GetMutableFragmentView<FRammsTrafficVisualFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			FRammsTrafficVisualFragment& Visual = VisualList[It];
			ARammsTrafficVehicleActor* Actor = Visual.Actor.Get();
			if (Actor == nullptr)
			{
				continue;
			}
			FRammsTrafficVehicleFragment& V = VehicleList[It];
			const FTransform& Transform = TransformList[It].GetTransform();
			if (Visual.CollisionRadius > 0.0f)
			{
				double Nearest = TNumericLimits<double>::Max();
				for (const FVector& Player : Players)
				{
					Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Transform.GetLocation(), Player));
				}
				// 10% hysteresis so a car at the edge does not flicker its physics state.
				const double Radius = Visual.CollisionRadius * (Visual.bCollisionOn ? 1.1 : 1.0);
				Visual.bCollisionOn = Players.IsEmpty() || Nearest < Radius * Radius;
				Actor->SetCollisionActive(Visual.bCollisionOn);
			}
			// Kinematic move: the collision box gets a kinematic target (pushes with the car's real velocity).
			Actor->SetActorLocationAndRotation(Transform.GetLocation(), Transform.GetRotation(), false, nullptr,
				V.bTeleport ? ETeleportType::TeleportPhysics : ETeleportType::None);
			Actor->SetReportedVelocity(V.Velocity);
			V.bTeleport = false;
		}
	});
}
