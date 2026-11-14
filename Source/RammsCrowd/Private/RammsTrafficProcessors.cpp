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
		TEXT("Draw the Mass traffic: 1 = each vehicle's speed (km/h) and what it yields to, 2 = also its corridor ahead."),
		ECVF_Default);

	struct FOccupant
	{
		float Distance = 0.0f;
		float Speed = 0.0f;
		float HalfLength = 0.0f;
		int32 Vehicle = INDEX_NONE;
	};

	/** A vehicle in an intersection, or let into it, on Lane. */
	struct FClaim
	{
		int32 Lane = INDEX_NONE;
		int32 Vehicle = INDEX_NONE;
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
			return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize));
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

		TArray<TArray<FOccupant>> Occupancy;
		TArray<TArray<FClaim>> Claims;
		TArray<TArray<FWaiter>> Waiting;
		TArray<TArray<FApproach>> Approaching;

		TArray<FObstaclePoint> Pedestrians;
		FPointGrid PedestrianGrid;
		float MaxPedestrianRadius = 0.0f;
		TArray<FObstacleCapsule> Footprints;
		TArray<FVector> Players;

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
	};

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

	/** The nearest vehicle ahead on the vehicle's lane, else on its next two lanes. */
	bool FindLeader(const FTrafficFrame& F, const FRammsTrafficVehicleFragment& V, const int32 Self, float& OutGap, float& OutSpeed)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		const float Front = V.Distance + V.HalfLength;
		for (const FOccupant& O : F.Occupancy[V.Lane])
		{
			if (O.Vehicle == Self || O.Distance <= V.Distance)
			{
				continue;
			}
			OutGap = O.Distance - O.HalfLength - Front;
			OutSpeed = O.Speed;
			return true;
		}
		float Offset = Net.Lanes[V.Lane].Length - Front;
		int32 Lane = V.NextLane;
		for (int32 Hop = 0; Hop < 2 && Net.IsValidLane(Lane) && Offset < 15000.0f; ++Hop)
		{
			for (const FOccupant& O : F.Occupancy[Lane])
			{
				if (O.Vehicle == Self)
				{
					continue;
				}
				OutGap = Offset + O.Distance - O.HalfLength;
				OutSpeed = O.Speed;
				return true;
			}
			Offset += Net.Lanes[Lane].Length;
			Lane = Net.UniqueNext(Lane);
		}
		return false;
	}

	/**
	 * Whether the vehicle may enter the intersection ahead (V.NextLane's zone). ToLine: front bumper to its
	 * stopping point. Returns why not, or None (and the vehicle holds a claim on its lane).
	 */
	ERammsTrafficYield EvaluateEntry(FTrafficFrame& F, FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, const int32 Self, const float ToLine)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
		const FRammsTrafficLane& Into = Net.Lanes[V.NextLane];
		const int32 Zone = Into.Zone;
		const bool bSignalized = Into.bSignalized && F.bHasSignals;
		// Held by other vehicles for too long (a gridlock the rules did not foresee): go anyway.
		const bool bDeadlock = V.WaitTime > P.MaxWaitSeconds;

		if (bSignalized)
		{
			if (RammsTraffic::MustStopForSignal(F.Signals.GetLight(Into.Axis), V.Speed, ToLine, P.AmberStopDeceleration, P.MaxDeceleration))
			{
				V.bGranted = false;
				return ERammsTrafficYield::Signal;
			}
		}
		else if (V.bGranted)
		{
			// All-way stop: its turn came; it goes.
			return ERammsTrafficYield::None;
		}
		else
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
					if (W.Vehicle != Self && bEarlier && Net.Conflict(W.NextLane, V.NextLane))
					{
						return ERammsTrafficYield::StopSign;
					}
				}
			}
		}

		if (!bDeadlock)
		{
			for (const FClaim& C : F.Claims[Zone])
			{
				if (C.Vehicle != Self && Net.Conflict(C.Lane, V.NextLane))
				{
					V.bGranted = false;
					return ERammsTrafficYield::Intersection;
				}
			}
			// Do not block the box: enter only when the exit has room for it beyond a queue that is standing there.
			const int32 Exit = Net.UniqueNext(V.NextLane);
			if (!V.bGranted && Net.IsValidLane(Exit))
			{
				for (const FOccupant& O : F.Occupancy[Exit])
				{
					if (O.Vehicle == Self)
					{
						continue;
					}
					if (O.Distance - O.HalfLength < 2.0f * V.HalfLength + P.MinGap && O.Speed < 150.0f)
					{
						return ERammsTrafficYield::Leader;
					}
					break; // the last car on the exit is the first one listed
				}
			}
			if (bSignalized && Into.Turn == ERammsTrafficTurn::Left)
			{
				// Permitted left: oncoming straight and right-turning traffic first.
				for (const FApproach& A : F.Approaching[Zone])
				{
					if (A.Vehicle == Self || !Net.Conflict(A.NextLane, V.NextLane) || Net.Lanes[A.NextLane].Turn == ERammsTrafficTurn::Left)
					{
						continue;
					}
					if (F.Signals.GetLight(Net.Lanes[A.NextLane].Axis) == ERammsSignalLight::Red)
					{
						continue;
					}
					const float Seconds = A.DistanceToLine / FMath::Max(A.Speed, 50.0f);
					if (A.DistanceToLine < 300.0f || Seconds < P.LeftTurnGapSeconds)
					{
						V.bGranted = false;
						return ERammsTrafficYield::Intersection;
					}
				}
			}
		}

		if (!V.bGranted)
		{
			V.bGranted = true;
			F.Claims[Zone].Add(FClaim{ V.NextLane, Self });
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
		FVector Chord = FrontPosition - RearPosition;
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
			V.NextLane = INDEX_NONE;
			V.Distance = Distance;
			V.Speed = V.DesiredSpeed * 0.5f;
			V.bGranted = false;
			V.ArrivalTime = -1.0f;
			V.StoppedAtLine = 0.0f;
			V.WaitTime = 0.0f;
			V.bTeleport = true;
			V.NetworkVersion = Net.Version;
			TArray<FOccupant>& Occupants = F.Occupancy[Lane];
			const int32 At = Algo::LowerBoundBy(Occupants, Distance, &FOccupant::Distance);
			Occupants.Insert(FOccupant{ Distance, V.Speed, V.HalfLength, Self }, At);
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
			const int32 FromZone = Net.Lanes[V.Lane].Zone;
			V.Distance -= Net.Lanes[V.Lane].Length;
			V.Lane = V.NextLane;
			V.NextLane = ChooseNext(Net, V.Lane, V.RandomState);
			const int32 ToZone = Net.Lanes[V.Lane].Zone;
			if (ToZone == INDEX_NONE)
			{
				V.bGranted = false;
			}
			else if (FromZone == INDEX_NONE)
			{
				V.StoppedAtLine = 0.0f;
				V.ArrivalTime = -1.0f;
			}
		}
	}

	void UpdateVehicle(FTrafficFrame& F, FRammsTrafficVehicleFragment& V, const FRammsTrafficVisualFragment& Visual, const FRammsTrafficDrivingParams& P, const int32 Self, FTransform& Transform)
	{
		const FRammsTrafficNetwork& Net = *F.Net;
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
		double HardStop = -1.0;
		auto Constrain = [&Accel, &Reason, &HardStop](const double A, const ERammsTrafficYield Why, const double StopAt)
		{
			if (A < Accel)
			{
				Accel = A;
				Reason = Why;
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
				Constrain(RammsTraffic::SlowToCap(V.Speed, TurnNext, ToLaneEnd), ERammsTrafficYield::Turn, -1.0);
			}
		}

		// Car following.
		float LeaderGap = 0.0f, LeaderSpeed = 0.0f;
		if (FindLeader(F, V, Self, LeaderGap, LeaderSpeed))
		{
			Constrain(RammsTraffic::IdmFollow(V.Speed, LeaderGap, LeaderSpeed, Idm), ERammsTrafficYield::Leader, -1.0);
		}

		// The intersection ahead: signal, stop sign, conflicting traffic.
		if (Lane.Zone == INDEX_NONE && Net.IsValidLane(V.NextLane) && Net.Lanes[V.NextLane].Zone != INDEX_NONE)
		{
			const FRammsTrafficLane& Into = Net.Lanes[V.NextLane];
			const float ToLine = ToLaneEnd - P.StopLineGap;
			if (ToLine < -1.0f)
			{
				// Past its stopping point already (it could not stop): it is in.
				if (!V.bGranted)
				{
					V.bGranted = true;
					F.Claims[Into.Zone].Add(FClaim{ V.NextLane, Self });
				}
			}
			else
			{
				const bool bStopSign = !(Into.bSignalized && F.bHasSignals);
				if (bStopSign && !V.bGranted && V.Speed < 30.0f && ToLine < 150.0f)
				{
					V.StoppedAtLine += F.Dt;
					if (V.ArrivalTime < 0.0f)
					{
						V.ArrivalTime = static_cast<float>(F.Now);
					}
				}
				const ERammsTrafficYield Why = EvaluateEntry(F, V, P, Self, ToLine);
				if (Why != ERammsTrafficYield::None)
				{
					Constrain(RammsTraffic::IdmFollow(V.Speed, FMath::Max(ToLine, 0.0f), 0.0, StopIdm), Why, FMath::Max(ToLine, 0.0f));
				}
			}
		}

		// Pedestrians and the robot in the way.
		ERammsTrafficYield ObstacleReason = ERammsTrafficYield::None;
		const float Obstacle = ScanCorridor(F, V, P, ObstacleReason);
		if (Obstacle < TNumericLimits<float>::Max())
		{
			const float Gap = Obstacle - P.ObstacleGap;
			Constrain(RammsTraffic::IdmFollow(V.Speed, Gap, 0.0, StopIdm), ObstacleReason, FMath::Max(Gap, 0.0f));
		}

		Accel = FMath::Clamp(Accel, -static_cast<double>(P.MaxDeceleration), static_cast<double>(P.MaxAcceleration));
		double Speed = V.Speed;
		const double Travel = RammsTraffic::Advance(Speed, Accel, F.Dt, MaxSpeed * 1.05, HardStop);
		V.Speed = static_cast<float>(Speed);
		V.Acceleration = static_cast<float>(Accel);
		V.Yield = Reason;
		// Waiting for a light, a pedestrian or the robot is not a gridlock.
		const bool bLegitimateWait = Reason == ERammsTrafficYield::Signal || Reason == ERammsTrafficYield::Pedestrian || Reason == ERammsTrafficYield::Robot;
		V.WaitTime = (V.Speed < 10.0f && !bLegitimateWait) ? V.WaitTime + F.Dt : 0.0f;
		V.Distance += static_cast<float>(Travel);
		AdvanceLanes(F, V, Self);

		const bool bTrace = Visual.GroundTraceRadius > 0.0f && F.NearestPlayerDistanceSquared(Transform.GetLocation()) < FMath::Square(static_cast<double>(Visual.GroundTraceRadius));
		ComputePose(F, V, bTrace, Transform);
	}

	/** ramms.Traffic.Debug: speed and yield reason over the car, and its corridor ahead (Level 2). */
	void DrawVehicleDebug(const UWorld* World, const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, const FTransform& Transform, const int32 Level)
	{
#if ENABLE_DRAW_DEBUG
		const TCHAR* Why = TEXT("");
		switch (V.Yield)
		{
		case ERammsTrafficYield::Leader: Why = TEXT("leader"); break;
		case ERammsTrafficYield::Signal: Why = TEXT("signal"); break;
		case ERammsTrafficYield::StopSign: Why = TEXT("stop"); break;
		case ERammsTrafficYield::Intersection: Why = TEXT("crossing traffic"); break;
		case ERammsTrafficYield::Pedestrian: Why = TEXT("pedestrian"); break;
		case ERammsTrafficYield::Robot: Why = TEXT("robot"); break;
		case ERammsTrafficYield::Turn: Why = TEXT("turn"); break;
		default: break;
		}
		const FColor Color = V.Yield == ERammsTrafficYield::None ? FColor::Green
			: (V.Yield == ERammsTrafficYield::Pedestrian || V.Yield == ERammsTrafficYield::Robot) ? FColor::Red : FColor::Yellow;
		DrawDebugString(World, Transform.GetLocation() + FVector(0.0, 0.0, 260.0), FString::Printf(TEXT("%.0f km/h %s"), V.Speed * 0.036f, Why), nullptr, Color, 0.0f, true);
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

	// The traffic as it stands: lane occupancy, intersection claims, all-way stop arrivals, approaches.
	int32 Dense = 0;
	float Predict = 0.0f;
	VehicleQuery.ForEachEntityChunk(Context, [&F, &Dense, &Predict](FMassExecutionContext& Ctx)
	{
		const FRammsTrafficDrivingParams& P = Ctx.GetConstSharedFragment<FRammsTrafficDrivingParams>();
		Predict = FMath::Max(Predict, P.PredictSeconds);
		const TConstArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetFragmentView<FRammsTrafficVehicleFragment>();
		const FRammsTrafficNetwork& Net = *F.Net;
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			const int32 Self = Dense++;
			const FRammsTrafficVehicleFragment& V = VehicleList[It];
			if (V.NetworkVersion != Net.Version || !Net.IsValidLane(V.Lane))
			{
				continue;
			}
			F.Occupancy[V.Lane].Add(FOccupant{ V.Distance, V.Speed, V.HalfLength, Self });
			const FRammsTrafficLane& Lane = Net.Lanes[V.Lane];
			if (Lane.Zone != INDEX_NONE)
			{
				F.Claims[Lane.Zone].Add(FClaim{ V.Lane, Self });
			}
			else if (Net.IsValidLane(V.NextLane) && Net.Lanes[V.NextLane].Zone != INDEX_NONE)
			{
				const int32 Zone = Net.Lanes[V.NextLane].Zone;
				if (V.bGranted)
				{
					F.Claims[Zone].Add(FClaim{ V.NextLane, Self });
				}
				F.Approaching[Zone].Add(FApproach{ V.NextLane, Lane.Length - (V.Distance + V.HalfLength) - P.StopLineGap, V.Speed, Self });
				if (V.ArrivalTime >= 0.0f)
				{
					F.Waiting[Zone].Add(FWaiter{ V.NextLane, V.ArrivalTime, Self });
				}
			}
		}
	});
	if (Dense == 0)
	{
		return;
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
			const FTransform& Transform = TransformList[It].GetTransform();
			const FVector2D Center(Transform.GetLocation());
			const FVector2D Forward = FVector2D(Transform.GetRotation().GetForwardVector()).GetSafeNormal();
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
			Capsule.Z = static_cast<float>(Transform.GetLocation().Z);
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

	// Drive.
	const int32 Debug = CVarTrafficDebug.GetValueOnGameThread();
	Dense = 0;
	VehicleQuery.ForEachEntityChunk(Context, [&F, &Dense, Debug](FMassExecutionContext& Ctx)
	{
		const FRammsTrafficDrivingParams& P = Ctx.GetConstSharedFragment<FRammsTrafficDrivingParams>();
		const TArrayView<FRammsTrafficVehicleFragment> VehicleList = Ctx.GetMutableFragmentView<FRammsTrafficVehicleFragment>();
		const TArrayView<FTransformFragment> TransformList = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TConstArrayView<FRammsTrafficVisualFragment> VisualList = Ctx.GetFragmentView<FRammsTrafficVisualFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			const int32 Self = Dense++;
			FRammsTrafficVehicleFragment& V = VehicleList[It];
			FTransform& Transform = TransformList[It].GetMutableTransform();
			UpdateVehicle(F, V, VisualList[It], P, Self, Transform);
			if (Debug > 0)
			{
				DrawVehicleDebug(F.World, V, P, Transform, Debug);
			}
		}
	});
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
