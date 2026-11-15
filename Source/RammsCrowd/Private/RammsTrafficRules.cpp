// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficRules.h"

#include "Algo/BinarySearch.h"
#include "RammsCrowdLog.h"
#include "RammsTrafficVehicleActor.h"

namespace RammsTraffic
{
	float TurnSpeed(const FRammsTrafficLane& Lane, const FRammsTrafficDrivingParams& Params)
	{
		if (Lane.Zone == INDEX_NONE || Lane.Turn == ERammsTrafficTurn::Straight)
		{
			return 0.0f;
		}
		return Lane.Turn == ERammsTrafficTurn::Right ? Params.RightTurnSpeed : Params.LeftTurnSpeed;
	}

	int32 ChooseNext(const FRammsTrafficNetwork& Net, const int32 Lane, uint32& RandomState)
	{
		if (!Net.IsValidLane(Lane))
		{
			return INDEX_NONE;
		}
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
		float Pick = RandomFloat(RandomState) * Total;
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

	namespace
	{
		bool IsLeft(const FRammsTrafficNetwork& Net, const int32 Lane)
		{
			return Net.IsValidLane(Lane) && Net.Lanes[Lane].Turn == ERammsTrafficTurn::Left;
		}

		bool IsStanding(const FWalker& W)
		{
			return W.Velocity.SizeSquared() < FMath::Square(StandingSpeed);
		}
	}

	//----------------------------------------------------------------------//
	// Building the frame
	//----------------------------------------------------------------------//

	void FTrafficFrame::Begin(const FRammsTrafficNetwork& Network, const double InNow, const float InDt)
	{
		Net = &Network;
		Now = InNow;
		Dt = FMath::Max(InDt, 0.0f);
		Occupancy.SetNum(Network.Lanes.Num());
		Claims.SetNum(Network.Zones.Num());
		Waiting.SetNum(Network.Zones.Num());
		Approaching.SetNum(Network.Zones.Num());
	}

	int32 FTrafficFrame::AddVehicle(FRammsTrafficVehicleFragment& Vehicle, FTransform& Transform, const FRammsTrafficVisualFragment* Visual, const FRammsTrafficDrivingParams& Params)
	{
		Predict = FMath::Max(Predict, Params.PredictSeconds);
		return Vehicles.Add(FVehicleRef{ &Vehicle, &Transform, Visual, &Params });
	}

	void FTrafficFrame::GatherTraffic()
	{
		for (int32 i = 0; i < Vehicles.Num(); ++i)
		{
			FRammsTrafficVehicleFragment& V = *Vehicles[i].V;
			const FRammsTrafficDrivingParams& P = *Vehicles[i].P;
			if (V.NetworkVersion != Net->Version || !Net->IsValidLane(V.Lane))
			{
				continue;
			}
			if (!Net->IsValidLane(V.NextLane))
			{
				V.NextLane = ChooseNext(*Net, V.Lane, V.RandomState);
			}
			Occupancy[V.Lane].Add(FOccupant{ V.Distance, V.Speed, V.HalfLength, V.HalfWidth, i });
			const FRammsTrafficLane& Lane = Net->Lanes[V.Lane];
			if (Lane.Zone != INDEX_NONE)
			{
				Claims[Lane.Zone].Add(FClaim{ V.Lane, i, V.HalfLength, true, true, false });
			}
			// Rear still in the intersection it just left.
			if (Net->IsValidLane(V.PrevLane) && Net->Lanes[V.PrevLane].Zone != INDEX_NONE && Net->Lanes[V.PrevLane].Zone != Lane.Zone)
			{
				Claims[Net->Lanes[V.PrevLane].Zone].Add(FClaim{ V.PrevLane, i, V.HalfLength, true, true, true });
			}
			const int32 Zone = ApproachZone(V);
			if (Zone != INDEX_NONE)
			{
				const float ToLine = DistanceToLine(V, P);
				const bool bCannotStop = IsCommitted(V.Speed, ToLine, P.MaxDeceleration);
				if (V.bGranted)
				{
					Claims[Zone].Add(FClaim{ V.NextLane, i, V.HalfLength, false, bCannotStop, false });
				}
				Approaching[Zone].Add(FApproach{ V.NextLane, ToLine, V.Speed, i, bCannotStop });
				if (V.ArrivalTime >= 0.0f)
				{
					Waiting[Zone].Add(FWaiter{ V.NextLane, V.ArrivalTime, i });
				}
			}
		}
		for (TArray<FOccupant>& Occupants : Occupancy)
		{
			Occupants.Sort([](const FOccupant& A, const FOccupant& B) { return A.Distance < B.Distance; });
		}
	}

	void FTrafficFrame::AddFootprint(const FObstacleCapsule& Capsule, const FVector2D& Velocity)
	{
		Footprints.Add(Capsule);
		const FVector2D Ahead = Velocity * Predict;
		if (!Ahead.IsNearlyZero())
		{
			FObstacleCapsule Later = Capsule;
			Later.A += Ahead;
			Later.B += Ahead;
			Footprints.Add(Later);
		}
	}

	int32 FTrafficFrame::AddWalker(const FWalker& Walker)
	{
		FWalker W = Walker;
		const bool bStanding = IsStanding(W);
		W.bWaiting = IsWalkerWaiting(W.bHeld, W.Lane.IsValid(), bStanding, W.Crossing != nullptr, W.LaneDistance,
			W.Crossing != nullptr ? W.Crossing->RoadStart : 0.0f, W.Crossing != nullptr ? W.Crossing->RoadEnd : 0.0f);
		const int32 Index = Walkers.Add(W);
		MaxPedestrianRadius = FMath::Max(MaxPedestrianRadius, W.Radius);
		// Where it is, and (walking somewhere, not waiting) where it will be shortly: about to step off the kerb.
		const int32 Steps = !W.bWaiting && W.Velocity.SizeSquared() > FMath::Square(MovingSpeed) ? 2 : 0;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			FObstaclePoint Point;
			Point.Position = FVector2D(W.Location) + W.Velocity * (Predict * Step / FMath::Max(Steps, 1));
			Point.Radius = W.Radius;
			Point.Z = static_cast<float>(W.Location.Z);
			Point.Walker = Index;
			Point.Step = static_cast<uint8>(Step);
			Point.bWaiting = W.bWaiting;
			PedestrianGrid.Add(Pedestrians.Add(Point), Point.Position);
		}
		return Index;
	}

	void FTrafficFrame::DecideEntries()
	{
		for (const FVehicleRef& Ref : Vehicles)
		{
			Ref.V->Entry = ERammsTrafficYield::None;
			Ref.V->EntryBlocker = INDEX_NONE;
		}
		TArray<int32> Order;
		for (int32 Zone = 0; Zone < Approaching.Num(); ++Zone)
		{
			Order.Reset();
			for (const FApproach& A : Approaching[Zone])
			{
				Order.Add(A.Vehicle);
			}
			Order.Sort([this](const int32 A, const int32 B)
			{
				const bool bLeftA = IsLeft(*Net, Vehicles[A].V->NextLane);
				const bool bLeftB = IsLeft(*Net, Vehicles[B].V->NextLane);
				if (bLeftA != bLeftB)
				{
					return !bLeftA;
				}
				return DistanceToLine(*Vehicles[A].V, *Vehicles[A].P) < DistanceToLine(*Vehicles[B].V, *Vehicles[B].P);
			});
			for (const int32 i : Order)
			{
				FRammsTrafficVehicleFragment& V = *Vehicles[i].V;
				V.Entry = DecideEntry(i, V.EntryBlocker);
			}
		}
	}

	void FTrafficFrame::Drive()
	{
		for (int32 i = 0; i < Vehicles.Num(); ++i)
		{
			UpdateVehicle(i);
			HoldCrossings(i);
		}
	}

	//----------------------------------------------------------------------//
	// Queries
	//----------------------------------------------------------------------//

	bool FTrafficFrame::Sample(const int32 Lane, const float Distance, FVector& OutPosition, FVector& OutDirection) const
	{
		return Net->Sample(Lane, Distance, OutPosition, OutDirection);
	}

	bool FTrafficFrame::SampleAhead(const int32 Lane, const int32 Next, const float Distance, FVector& OutPosition, FVector& OutDirection, int32& OutLane) const
	{
		int32 Current = Lane;
		int32 Following = Next;
		float D = Distance;
		for (int32 Hop = 0; Hop < 3; ++Hop)
		{
			const float Length = Net->Lanes[Current].Length;
			if (D <= Length || !Net->IsValidLane(Following))
			{
				OutLane = Current;
				return Sample(Current, FMath::Min(D, Length), OutPosition, OutDirection);
			}
			D -= Length;
			Current = Following;
			Following = Net->UniqueNext(Current);
		}
		OutLane = Current;
		return Sample(Current, FMath::Min(D, Net->Lanes[Current].Length), OutPosition, OutDirection);
	}

	int32 FTrafficFrame::ApproachZone(const FRammsTrafficVehicleFragment& V) const
	{
		if (!Net->IsValidLane(V.Lane) || !Net->IsValidLane(V.NextLane))
		{
			return INDEX_NONE;
		}
		const int32 Zone = Net->Lanes[V.NextLane].Zone;
		return Zone != INDEX_NONE && Zone != Net->Lanes[V.Lane].Zone ? Zone : INDEX_NONE;
	}

	float FTrafficFrame::DistanceToLine(const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P) const
	{
		return Net->Lanes[V.Lane].Length - (V.Distance + V.HalfLength) - P.StopLineGap;
	}

	int32 FTrafficFrame::QueuedBehind(const int32 Self) const
	{
		const FRammsTrafficVehicleFragment& V = *Vehicles[Self].V;
		for (const FOccupant& O : Occupancy[V.Lane])
		{
			if (O.Vehicle == Self || O.Distance <= V.Distance)
			{
				continue;
			}
			const FRammsTrafficVehicleFragment& Ahead = *Vehicles[O.Vehicle].V;
			if (!Ahead.bGranted || Ahead.Speed < 100.0f)
			{
				return O.Vehicle;
			}
		}
		return INDEX_NONE;
	}

	double FTrafficFrame::NearestPlayerDistanceSquared(const FVector& Position) const
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector& Player : Players)
		{
			Best = FMath::Min(Best, FVector::DistSquared2D(Position, Player));
		}
		return Best;
	}

	void FTrafficFrame::SiblingLeaders(const FRammsTrafficVehicleFragment& V, const int32 Self, const int32 OnLane, const float OffsetToStart, float& InOutGap, float& InOutSpeed, int32& InOutLeader) const
	{
		// Cars on lanes diverging from intersection lane OnLane (same entry) that are still beside it: a car that turned
		// off ahead of this one keeps being its leader until the lanes have separated by their two half widths.
		// OffsetToStart: from this vehicle's front to OnLane's start (negative once it is on it).
		for (const int32 Sibling : Net->Lanes[OnLane].Siblings)
		{
			for (const FOccupant& O : Occupancy[Sibling])
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
				FVector Mine, Theirs, Direction;
				if (!Sample(OnLane, Rear, Mine, Direction) || !Sample(Sibling, Rear, Theirs, Direction))
				{
					continue;
				}
				if (FVector::Dist2D(Mine, Theirs) < V.HalfWidth + O.HalfWidth + 40.0f)
				{
					InOutGap = Gap;
					InOutSpeed = O.Speed;
					InOutLeader = O.Vehicle;
				}
			}
		}
	}

	bool FTrafficFrame::FindLeader(const FRammsTrafficVehicleFragment& V, const int32 Self, float& OutGap, float& OutSpeed, int32& OutLeader) const
	{
		const float Front = V.Distance + V.HalfLength;
		float Gap = TNumericLimits<float>::Max();
		float Speed = 0.0f;
		int32 Leader = INDEX_NONE;
		for (const FOccupant& O : Occupancy[V.Lane])
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
		if (Net->Lanes[V.Lane].Zone != INDEX_NONE)
		{
			SiblingLeaders(V, Self, V.Lane, -Front, Gap, Speed, Leader);
		}
		float Offset = Net->Lanes[V.Lane].Length - Front;
		int32 Lane = V.NextLane;
		for (int32 Hop = 0; Hop < 2 && Net->IsValidLane(Lane) && Offset < FMath::Min(Gap, 15000.0f); ++Hop)
		{
			for (const FOccupant& O : Occupancy[Lane])
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
			if (Net->Lanes[Lane].Zone != INDEX_NONE)
			{
				SiblingLeaders(V, Self, Lane, Offset, Gap, Speed, Leader);
			}
			Offset += Net->Lanes[Lane].Length;
			Lane = Net->UniqueNext(Lane);
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

	float FTrafficFrame::ScanCorridor(const FRammsTrafficVehicleFragment& V, const FRammsTrafficDrivingParams& P, ERammsTrafficYield& OutReason, int32& OutObstacle) const
	{
		OutObstacle = INDEX_NONE;
		if (Pedestrians.IsEmpty() && Footprints.IsEmpty())
		{
			return TNumericLimits<float>::Max();
		}
		const float Look = FMath::Max(P.MinLookahead, V.Speed * P.LookaheadSeconds + 800.0f);
		const float Reach = V.HalfWidth + P.CorridorMargin + MaxPedestrianRadius;
		const float Front = V.Distance + V.HalfLength;
		constexpr float Step = 150.0f;
		FVector Position, Direction;
		int32 SampledLane = INDEX_NONE;
		for (float S = 0.0f; S <= Look; S += Step)
		{
			if (!SampleAhead(V.Lane, V.NextLane, Front + S, Position, Direction, SampledLane))
			{
				break;
			}
			const FVector2D Here(Position);
			const float Z = static_cast<float>(Position.Z);
			const float LaneHalfWidth = Net->Lanes[SampledLane].HalfWidth;
			int32 Hit = INDEX_NONE;
			PedestrianGrid.ForEachNear(Here, Reach, [&](const int32 Index)
			{
				const FObstaclePoint& O = Pedestrians[Index];
				if (Hit != INDEX_NONE || FMath::Abs(O.Z - Z) >= 250.0f)
				{
					return;
				}
				// The walker deadlock breaker: it drives on past the one walker it stood for, while that walker stands.
				const FWalker& W = Walkers[O.Walker];
				if (V.PastStandingWalkers > 0.0f && W.Entity == V.PastWalker && S <= V.PastStandingWalkers && IsStanding(W))
				{
					return;
				}
				if (IsInTheWay(FVector2D::Distance(Here, O.Position), O.Radius, V.HalfWidth, LaneHalfWidth, P.CorridorMargin, O.bWaiting))
				{
					Hit = Index;
				}
			});
			if (Hit != INDEX_NONE)
			{
				OutReason = ERammsTrafficYield::Pedestrian;
				OutObstacle = Hit;
				return S;
			}
			for (int32 c = 0; c < Footprints.Num(); ++c)
			{
				const FObstacleCapsule& C = Footprints[c];
				const double Distance = FVector2D::Distance(Here, FMath::ClosestPointOnSegment2D(Here, C.A, C.B));
				// The robot stops a lane when it is on the carriageway (or physically in the car's path), not when it
				// waits on the sidewalk beside it.
				if (FMath::Abs(C.Z - Z) < 300.0f && IsInTheWay(Distance, C.Radius, V.HalfWidth, LaneHalfWidth, P.CorridorMargin, false))
				{
					OutReason = ERammsTrafficYield::Robot;
					OutObstacle = c;
					return S;
				}
			}
		}
		return TNumericLimits<float>::Max();
	}

	FString FTrafficFrame::VehicleName(const int32 Index) const
	{
		if (!Vehicles.IsValidIndex(Index))
		{
			return TEXT("-");
		}
		if (NameVehicle)
		{
			return NameVehicle(Index);
		}
		const FRammsTrafficVisualFragment* Visual = Vehicles[Index].Visual;
		const ARammsTrafficVehicleActor* Actor = Visual != nullptr ? Visual->Actor.Get() : nullptr;
		return Actor != nullptr ? Actor->GetName() : FString::Printf(TEXT("#%d"), Index);
	}

	FString FTrafficFrame::WalkerText(const int32 Index, const int32 Step) const
	{
		if (!Walkers.IsValidIndex(Index))
		{
			return TEXT("-");
		}
		const FWalker& W = Walkers[Index];
		FString LaneText = TEXT("no lane");
		if (W.Lane.IsValid())
		{
			FString Kind = W.Crossing != nullptr
				? FString::Printf(TEXT("street crossing, road %.0f-%.0f, lanes %.0f-%.0f"), W.Crossing->RoadStart, W.Crossing->RoadEnd, W.Crossing->EdgeStart, W.Crossing->EdgeEnd)
				: FString(TEXT("not a street crossing"));
			if (DescribeLaneSignal)
			{
				Kind += DescribeLaneSignal(W.Lane);
			}
			if (const float* Since = HeldCrossings.Find(W.Lane))
			{
				Kind += *Since <= TNumericLimits<float>::Lowest() ? FString(TEXT(", held for all")) : FString::Printf(TEXT(", held for those on after t=%.1f"), *Since);
			}
			LaneText = FString::Printf(TEXT("lane %s (%s) d=%.0f/%.0f on since t=%.1f"), *W.Lane.ToString(), *Kind, W.LaneDistance, W.LaneLength, W.LaneSince);
		}
		const FString Ahead = Step > 0 ? FString::Printf(TEXT(" (point %.1f s ahead of it)"), Predict * Step * 0.5f) : FString();
		const TCHAR* Action = W.Action == EMassMovementAction::Stand ? TEXT("stand") : W.Action == EMassMovementAction::Animate ? TEXT("animate") : TEXT("move");
		return FString::Printf(TEXT("E%d at (%.0f, %.0f, %.0f) %.2f m/s%s, %s, gate held %d, desired %.0f cm/s %s%s"),
			W.Entity.Index, W.Location.X, W.Location.Y, W.Location.Z, W.Velocity.Size() * 0.01f, *Ahead, *LaneText,
			W.bHeld ? 1 : 0, W.DesiredSpeed, Action, W.bWaiting ? TEXT(", waiting") : TEXT(""));
	}

	//----------------------------------------------------------------------//
	// Entry decisions
	//----------------------------------------------------------------------//

	ERammsTrafficYield FTrafficFrame::DecideEntry(const int32 Self, int32& OutBlocker)
	{
		const FRammsTrafficNetwork& N = *Net;
		FRammsTrafficVehicleFragment& V = *Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *Vehicles[Self].P;
		const FRammsTrafficLane& Into = N.Lanes[V.NextLane];
		const int32 Zone = Into.Zone;
		const float ToLine = DistanceToLine(V, P);
		const bool bSignalized = Into.bSignalized && bHasSignals;
		OutBlocker = INDEX_NONE;

		// Let in and no longer able to stop at the line (or over it already): it goes on in, and every other vehicle
		// respects its claim (IsCommitted).
		if (V.bGranted && IsCommitted(V.Speed, ToLine, P.MaxDeceleration))
		{
			return ERammsTrafficYield::None;
		}
		// All-way stop: the time it has stood at the line, and when it got there.
		if (!bSignalized && !V.bGranted && V.Speed < 30.0f && ToLine < 150.0f)
		{
			V.StoppedAtLine += Dt;
			if (V.ArrivalTime < 0.0f)
			{
				V.ArrivalTime = static_cast<float>(Now);
			}
		}

		// Far from the line only the signal matters (braking for red starts early); no claims are taken yet.
		const float DecideDistance = FMath::Max(1500.0f, V.Speed * V.Speed / (2.0f * P.ComfortDeceleration) + 600.0f);
		const ERammsSignalLight Light = bSignalized ? Signals.GetLight(Into.Axis) : ERammsSignalLight::Green;
		// A permitted left turn that waited at the line for oncoming traffic goes on the amber (and the all red that
		// follows it): oncoming traffic stops for the amber.
		const int32 OtherAxis = 1 - Into.Axis;
		const bool bAllRedAfterOurs = bSignalized && Signals.LightX == ERammsSignalLight::Red && Signals.LightY == ERammsSignalLight::Red
			&& (OtherAxis == 0 ? Signals.SecondsToChangeX : Signals.SecondsToChangeY) < (Into.Axis == 0 ? Signals.SecondsToChangeX : Signals.SecondsToChangeY);
		const bool bSneak = bSignalized && Into.Turn == ERammsTrafficTurn::Left && V.bLeftWaiting && ToLine < 300.0f
			&& (Light == ERammsSignalLight::Amber || bAllRedAfterOurs);
		if (bSignalized && !bSneak && MustStopForSignal(Light, V.Speed, FMath::Max(ToLine, 0.0f), P.AmberStopDeceleration, P.MaxDeceleration))
		{
			V.bGranted = false;
			return ERammsTrafficYield::Signal;
		}
		if (ToLine > DecideDistance)
		{
			V.bGranted = false;
			return bSignalized ? ERammsTrafficYield::None : ERammsTrafficYield::StopSign;
		}

		// Only the first car at its line takes a claim: one queued behind a car that has not been let in (or stands)
		// would hold up every conflicting movement while it cannot move itself.
		const int32 Ahead = QueuedBehind(Self);
		if (Ahead != INDEX_NONE)
		{
			V.bGranted = false;
			OutBlocker = Ahead;
			return ERammsTrafficYield::Queue;
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
				for (const FWaiter& W : Waiting[Zone])
				{
					const bool bEarlier = W.Arrival < V.ArrivalTime || (W.Arrival == V.ArrivalTime && W.Vehicle < Self);
					if (W.Vehicle != Self && bEarlier && N.Conflict(W.NextLane, V.NextLane) && !Vehicles[W.Vehicle].V->bGranted)
					{
						OutBlocker = W.Vehicle;
						return ERammsTrafficYield::StopSign;
					}
				}
			}
		}

		// Conflicting vehicles in the intersection, or let in and unable to stop, always count; ones only let in count
		// unless this one is deadlocked (they can still stop for it).
		for (const FClaim& C : Claims[Zone])
		{
			if (C.Vehicle != Self && N.Conflict(C.Lane, V.NextLane) && (C.bInside || C.bCommitted || !bDeadlock))
			{
				V.bGranted = false;
				// A permitted left held at its line on green by oncoming traffic already let in waits for it like for
				// traffic still coming: it may go on the amber.
				if (bSignalized && Into.Turn == ERammsTrafficTurn::Left && Light == ERammsSignalLight::Green && ToLine < 300.0f)
				{
					V.bLeftWaiting = true;
				}
				OutBlocker = C.Vehicle;
				return ERammsTrafficYield::Claim;
			}
		}

		if (!V.bGranted)
		{
			// Do not block the box: room on the exit beyond a slow queue there, less what is already let in to it. A
			// deadlocked car (which no longer waits for cars only let in) goes once the exit has room for it counting just
			// the cars already in the intersection: never into a full exit, so it never stops in the box.
			const int32 Exit = N.UniqueNext(V.NextLane);
			if (N.IsValidLane(Exit))
			{
				float FirstRear = TNumericLimits<float>::Max();
				int32 Last = INDEX_NONE;
				for (const FOccupant& O : Occupancy[Exit])
				{
					if (O.Vehicle != Self)
					{
						if (O.Speed < 300.0f)
						{
							FirstRear = O.Distance - O.HalfLength;
							Last = O.Vehicle;
						}
						break; // the car nearest the exit's start is listed first
					}
				}
				if (FirstRear < TNumericLimits<float>::Max())
				{
					float Room = FirstRear;
					float RoomInside = FirstRear;
					for (const FClaim& C : Claims[Zone])
					{
						// (A rear claim's car is on the exit already: it is counted there.)
						if (C.Vehicle != Self && !C.bRear && N.UniqueNext(C.Lane) == Exit)
						{
							const float Need = 2.0f * C.HalfLength + P.MinGap;
							Room -= Need;
							RoomInside -= C.bInside ? Need : 0.0f;
						}
					}
					const float Mine = 2.0f * V.HalfLength + P.MinGap;
					if (Room < Mine)
					{
						const bool bOverride = bDeadlock && RoomInside >= Mine;
						if (!bOverride)
						{
							OutBlocker = Last;
							return ERammsTrafficYield::Box;
						}
					}
				}
			}
		}

		if (bSignalized && Into.Turn == ERammsTrafficTurn::Left && !V.bGranted)
		{
			// Permitted left: oncoming straight and right-turning traffic goes first -- the ones just let in, and the
			// ones on their way within the gap. Not one standing at its line held by something else, not another left
			// turn (two opposing lefts sort themselves out through their claims), and on the amber only those
			// committed to going through. Deadlocked, it still waits for any that can no longer stop. Once let in, the
			// left turn keeps its claim: oncoming traffic waits for it.
			for (const FApproach& A : Approaching[Zone])
			{
				if (A.Vehicle == Self || IsLeft(N, A.NextLane) || !N.Conflict(A.NextLane, V.NextLane))
				{
					continue;
				}
				if (Signals.GetLight(N.Lanes[A.NextLane].Axis) == ERammsSignalLight::Red && !A.bCannotStop)
				{
					continue;
				}
				const bool bLetIn = Vehicles[A.Vehicle].V->bGranted;
				const float TimeToLine = A.Speed > 100.0f ? A.DistanceToLine / A.Speed : TNumericLimits<float>::Max();
				const bool bComing = TimeToLine < P.LeftTurnGapSeconds;
				const bool bWait = (A.bCannotStop && (bComing || bLetIn)) || (!bDeadlock && (bLetIn || (!bSneak && bComing)));
				if (bWait)
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
			Claims[Zone].Add(FClaim{ V.NextLane, Self, V.HalfLength, false, IsCommitted(V.Speed, ToLine, P.MaxDeceleration), false });
		}
		return ERammsTrafficYield::None;
	}

	//----------------------------------------------------------------------//
	// Driving
	//----------------------------------------------------------------------//

	void FTrafficFrame::ComputePose(FRammsTrafficVehicleFragment& V, const bool bTraceGround, FTransform& OutTransform) const
	{
		// Base centre on the road, yaw along the axle chord, pitch from the axles' heights.
		const float Axle = V.HalfLength * 0.75f;
		FVector Center, CenterDirection, FrontPosition, FrontDirection, RearPosition, RearDirection;
		int32 SampledLane = INDEX_NONE;
		if (!SampleAhead(V.Lane, V.NextLane, V.Distance, Center, CenterDirection, SampledLane))
		{
			return;
		}
		if (!SampleAhead(V.Lane, V.NextLane, V.Distance + Axle, FrontPosition, FrontDirection, SampledLane))
		{
			FrontPosition = Center + CenterDirection * Axle;
		}
		const bool bRearOnLane = V.Distance - Axle >= 0.0f && Sample(V.Lane, V.Distance - Axle, RearPosition, RearDirection);
		if (!bRearOnLane)
		{
			if (Sample(V.Lane, 0.0f, RearPosition, RearDirection))
			{
				// Just onto the lane: carry its start back along its direction.
				RearPosition -= RearDirection * (Axle - V.Distance);
			}
			else
			{
				RearPosition = Center - CenterDirection * Axle;
			}
		}
		if (bTraceGround && GroundHeight)
		{
			FrontPosition.Z = GroundHeight(FrontPosition);
			RearPosition.Z = GroundHeight(RearPosition);
		}
		const FVector Chord = FrontPosition - RearPosition;
		const double Horizontal = FVector2D(Chord).Size();
		const FVector Flat = Horizontal > 1.0 ? FVector(Chord.X, Chord.Y, 0.0) / Horizontal : FVector(CenterDirection.X, CenterDirection.Y, 0.0).GetSafeNormal();
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		const double Pitch = Horizontal > 1.0 ? FMath::RadiansToDegrees(FMath::Atan2(Chord.Z, Horizontal)) : 0.0;
		const FVector Base(Center.X, Center.Y, (FrontPosition.Z + RearPosition.Z) * 0.5);
		OutTransform = FTransform(FRotator(Pitch, Yaw, 0.0), Base);
		V.Velocity = OutTransform.GetRotation().GetForwardVector() * V.Speed;
	}

	bool FTrafficFrame::Respawn(FRammsTrafficVehicleFragment& V, const int32 Self)
	{
		// Somewhere free on a road lane, away from the players.
		const FRammsTrafficNetwork& N = *Net;
		for (int32 Try = 0; Try < 24 && N.Lanes.Num() > 0; ++Try)
		{
			const int32 Lane = FMath::Min(static_cast<int32>(RandomFloat(V.RandomState) * N.Lanes.Num()), N.Lanes.Num() - 1);
			const FRammsTrafficLane& L = N.Lanes[Lane];
			const float Margin = V.HalfLength + 300.0f;
			if (L.Zone != INDEX_NONE || L.Next.Num() == 0 || L.Length < 2.0f * Margin + 200.0f)
			{
				continue;
			}
			const float Distance = Margin + RandomFloat(V.RandomState) * (L.Length - 2.0f * Margin);
			FVector Position, Direction;
			if (!Sample(Lane, Distance, Position, Direction) || NearestPlayerDistanceSquared(Position) < FMath::Square(DeadEndRespawnDistance))
			{
				continue;
			}
			bool bFree = true;
			for (const FOccupant& O : Occupancy[Lane])
			{
				bFree &= FMath::Abs(O.Distance - Distance) > 1500.0f;
			}
			if (!bFree)
			{
				continue;
			}
			V.Lane = Lane;
			V.NextLane = ChooseNext(N, Lane, V.RandomState);
			V.PrevLane = INDEX_NONE;
			V.PrevLaneEnd = 0.0f;
			V.Distance = Distance;
			V.Speed = V.DesiredSpeed * 0.5f;
			V.bGranted = false;
			V.bLeftWaiting = false;
			V.ArrivalTime = -1.0f;
			V.StoppedAtLine = 0.0f;
			V.BlockedTime = 0.0f;
			V.WalkersSince = -1.0f;
			V.WalkersLast = -1.0f;
			V.WalkersWait = 0.0f;
			V.StuckWalkerWait = 0.0f;
			V.PastWalker = FMassEntityHandle();
			V.PastStandingWalkers = 0.0f;
			V.DeadEndWait = 0.0f;
			V.bTeleport = true;
			V.NetworkVersion = N.Version;
			TArray<FOccupant>& Occupants = Occupancy[Lane];
			const int32 At = Algo::LowerBoundBy(Occupants, Distance, &FOccupant::Distance);
			Occupants.Insert(FOccupant{ Distance, V.Speed, V.HalfLength, V.HalfWidth, Self }, At);
			return true;
		}
		return false;
	}

	void FTrafficFrame::AdvanceLanes(FRammsTrafficVehicleFragment& V, const int32 Self)
	{
		const FRammsTrafficNetwork& N = *Net;
		for (int32 Hop = 0; Hop < 4 && V.Distance > N.Lanes[V.Lane].Length; ++Hop)
		{
			if (!N.IsValidLane(V.NextLane))
			{
				// A dead end (it stops short of it; this only catches a long frame): wait at the end.
				V.Distance = N.Lanes[V.Lane].Length;
				V.Speed = 0.0f;
				return;
			}
			const int32 FromLane = V.Lane;
			const int32 FromZone = N.Lanes[FromLane].Zone;
			V.Distance -= N.Lanes[FromLane].Length;
			V.PrevLaneEnd -= N.Lanes[FromLane].Length;
			V.Lane = V.NextLane;
			V.NextLane = ChooseNext(N, V.Lane, V.RandomState);
			const int32 ToZone = N.Lanes[V.Lane].Zone;
			if (ToZone != FromZone)
			{
				// Out of an intersection (its rear stays in it a while), into one (its leave is used up: the lane's claim
				// counts now), or straight from one into the next (it decides on that one at the boundary).
				if (FromZone != INDEX_NONE)
				{
					V.PrevLane = FromLane;
					V.PrevLaneEnd = 0.0f;
				}
				V.bGranted = false;
				if (ToZone != INDEX_NONE)
				{
					V.StoppedAtLine = 0.0f;
					V.ArrivalTime = -1.0f;
					V.BlockedTime = 0.0f;
					V.bLeftWaiting = false;
				}
			}
		}
		if (V.PrevLane != INDEX_NONE && V.Distance - V.HalfLength > V.PrevLaneEnd)
		{
			V.PrevLane = INDEX_NONE; // its rear is out of the intersection too
		}
	}

	void FTrafficFrame::UpdateVehicle(const int32 Self)
	{
		const FRammsTrafficNetwork& N = *Net;
		FRammsTrafficVehicleFragment& V = *Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *Vehicles[Self].P;
		const FRammsTrafficVisualFragment* Visual = Vehicles[Self].Visual;
		FTransform& Transform = *Vehicles[Self].Transform;
		V.BlockerObstacle = INDEX_NONE;
		if (V.NetworkVersion != N.Version || !N.IsValidLane(V.Lane))
		{
			// Placed on another network (the ZoneGraph changed): find a new place.
			if (!Respawn(V, Self))
			{
				V.Speed = 0.0f;
				return;
			}
		}
		if (!N.IsValidLane(V.NextLane))
		{
			V.NextLane = ChooseNext(N, V.Lane, V.RandomState);
		}

		const int32 LaneIndex = V.Lane;
		const FRammsTrafficLane& Lane = N.Lanes[LaneIndex];
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

		double Accel = IdmFree(V.Speed, Idm);
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

		// Slow for the turn ahead, once it needs to.
		const bool bDeadEnd = !N.IsValidLane(V.NextLane);
		if (!bDeadEnd)
		{
			const float TurnNext = TurnSpeed(N.Lanes[V.NextLane], P);
			if (TurnNext > 0.0f)
			{
				Constrain(TurnSlowdown(V.Speed, TurnNext, ToLaneEnd, P.ComfortDeceleration), ERammsTrafficYield::Turn, -1.0, INDEX_NONE, ToLaneEnd);
			}
		}
		else
		{
			// A lane that leads nowhere: stop at its end.
			const float ToEnd = ToLaneEnd - P.StopLineGap;
			Constrain(IdmFollow(V.Speed, FMath::Max(ToEnd, 0.0f), 0.0, StopIdm), ERammsTrafficYield::DeadEnd, FMath::Max(ToEnd, 0.0f), INDEX_NONE, ToEnd);
		}

		// Car following.
		float LeaderGap = 0.0f, LeaderSpeed = 0.0f;
		int32 Leader = INDEX_NONE;
		if (FindLeader(V, Self, LeaderGap, LeaderSpeed, Leader))
		{
			Constrain(IdmFollow(V.Speed, LeaderGap, LeaderSpeed, Idm), ERammsTrafficYield::Leader, -1.0, Leader, LeaderGap);
		}

		// The intersection ahead (decided in the entry pass); a queued car just follows the car ahead of it.
		if (ApproachZone(V) != INDEX_NONE && V.Entry != ERammsTrafficYield::None && V.Entry != ERammsTrafficYield::Queue)
		{
			const float ToLine = ToLaneEnd - P.StopLineGap;
			// Past the line without leave (it overshot): stand where it is.
			Constrain(IdmFollow(V.Speed, FMath::Max(ToLine, 0.0f), 0.0, StopIdm), V.Entry, FMath::Max(ToLine, 0.0f), V.EntryBlocker, ToLine);
		}

		// Pedestrians and the robot in the way.
		const double AccelBeforeObstacles = Accel;
		ERammsTrafficYield ObstacleReason = ERammsTrafficYield::None;
		int32 Obstacle = INDEX_NONE;
		const float ObstacleDistance = ScanCorridor(V, P, ObstacleReason, Obstacle);
		if (ObstacleDistance < TNumericLimits<float>::Max())
		{
			const float Gap = ObstacleDistance - P.ObstacleGap;
			Constrain(IdmFollow(V.Speed, Gap, 0.0, StopIdm), ObstacleReason, FMath::Max(Gap, 0.0f), INDEX_NONE, Gap);
		}
		const bool bObstacle = Reason == ERammsTrafficYield::Pedestrian || Reason == ERammsTrafficYield::Robot;
		V.BlockerObstacle = bObstacle ? Obstacle : INDEX_NONE;

		// Stopping or stopped for walkers and for nothing else (it would drive on but for them): since when, so that
		// it holds those who come after it (HoldCrossings); a light, a queue or other traffic is not walkers' doing.
		const bool bForWalkersOnly = Reason == ERammsTrafficYield::Pedestrian && AccelBeforeObstacles > 0.1 * P.MaxAcceleration;
		if (bForWalkersOnly)
		{
			if (V.WalkersSince < 0.0f)
			{
				V.WalkersSince = static_cast<float>(Now);
			}
			V.WalkersLast = static_cast<float>(Now);
			if (V.Speed < 10.0f)
			{
				V.WalkersWait += Dt;
			}
		}
		else if (V.WalkersSince >= 0.0f && (V.Speed > 100.0f || Now - V.WalkersLast > 2.0))
		{
			V.WalkersSince = -1.0f;
			V.WalkersWait = 0.0f;
		}
		// The walker it stops for stands (held up by others, not crossing): after StuckWalkerReleaseSeconds it stops
		// holding newcomers at the crossing (HoldCrossings), so that the crowd waiting at the kerb can make way.
		const int32 StoodFor = bForWalkersOnly && Pedestrians.IsValidIndex(Obstacle) ? Pedestrians[Obstacle].Walker : INDEX_NONE;
		const bool bWalkerStands = Walkers.IsValidIndex(StoodFor) && IsStanding(Walkers[StoodFor]);
		if (bWalkerStands && V.Speed < 10.0f)
		{
			V.StuckWalkerWait += Dt;
		}
		else if (!bWalkerStands)
		{
			V.StuckWalkerWait = 0.0f;
		}
		// Walker deadlock breaker: stood MaxWaitSeconds for nothing but a walker who does not move. Drive on past that
		// walker (and only that one) until this car is well past where they stand.
		if (bWalkerStands && V.WalkersWait > P.MaxWaitSeconds)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("Traffic: %s stood %.0f s for a walker who does not move; it drives on past them. Walker %s"),
				*VehicleName(Self), V.WalkersWait, *WalkerText(StoodFor, Pedestrians[Obstacle].Step));
			V.PastWalker = Walkers[StoodFor].Entity;
			V.PastStandingWalkers = ObstacleDistance + 2.0f * V.HalfLength + 300.0f;
			V.WalkersWait = 0.0f;
		}

		Accel = FMath::Clamp(Accel, -static_cast<double>(P.MaxDeceleration), static_cast<double>(P.MaxAcceleration));
		double Speed = V.Speed;
		const double Travel = Advance(Speed, Accel, Dt, MaxSpeed * 1.05, HardStop);
		V.Speed = static_cast<float>(Speed);
		V.Acceleration = static_cast<float>(Accel);
		V.Yield = Reason;
		V.Blocker = Blocker;
		V.LimitGap = LimitGap;
		// The deadlock breaker counts only time stood at the line held by other vehicles' rules; time at a red light,
		// in a queue or at the box (it waits for the cars ahead on its exit to move, not for this intersection) neither
		// counts nor resets it, driving on resets it.
		const bool bHeldByVehicles = V.Entry == ERammsTrafficYield::Claim || V.Entry == ERammsTrafficYield::OncomingLeft
			|| (V.Entry == ERammsTrafficYield::StopSign && V.EntryBlocker != INDEX_NONE);
		if (V.Speed < 10.0f && bHeldByVehicles)
		{
			V.BlockedTime += Dt;
		}
		else if (V.Speed > 100.0f)
		{
			V.BlockedTime = 0.0f;
		}
		V.Distance += static_cast<float>(Travel);
		V.PastStandingWalkers = FMath::Max(V.PastStandingWalkers - static_cast<float>(Travel), 0.0f);
		if (V.PastStandingWalkers <= 0.0f)
		{
			V.PastWalker = FMassEntityHandle();
		}
		AdvanceLanes(V, Self);

		// At a dead end (its front within 1.5 m of where it stops): start again elsewhere at once when nobody is near
		// enough to see it go; watched, once it has stood there MaxWaitSeconds. (IDM creeps the last metre into a stop
		// for several seconds, so "there" is close and slow, not standing.)
		if (bDeadEnd && V.Lane == LaneIndex && !N.IsValidLane(V.NextLane)
			&& N.Lanes[V.Lane].Length - (V.Distance + V.HalfLength) < P.StopLineGap + 150.0f)
		{
			if (V.Speed < 50.0f)
			{
				V.DeadEndWait += Dt;
			}
			if (NearestPlayerDistanceSquared(Transform.GetLocation()) > FMath::Square(DeadEndRespawnDistance) || V.DeadEndWait > P.MaxWaitSeconds)
			{
				Respawn(V, Self);
			}
		}

		const bool bTrace = GroundHeight && Visual != nullptr && Visual->GroundTraceRadius > 0.0f
			&& NearestPlayerDistanceSquared(Transform.GetLocation()) < FMath::Square(static_cast<double>(Visual->GroundTraceRadius));
		ComputePose(V, bTrace, Transform);
	}

	void FTrafficFrame::Hold(const FZoneGraphLaneHandle& Lane, const float Since)
	{
		float& Held = HeldCrossings.FindOrAdd(Lane, Since);
		Held = FMath::Min(Held, Since);
	}

	void FTrafficFrame::HoldCrossings(const int32 Self)
	{
		// The street crossings walkers wait at for this vehicle (see CrossingHoldSeconds): those under it, for everyone;
		// those ahead, for everyone while it drives on, for those who come after it began to yield while it yields to
		// walkers (who was first goes first: the ones already crossing or already at the kerb go before it, and the gate
		// keeps waiting anyone already waiting for a car), and for nobody while it stands for anything else (a light, a
		// queue, other traffic) or for a walker who stands (held up by the crowd it would otherwise keep waiting).
		const FRammsTrafficNetwork& N = *Net;
		const FRammsTrafficVehicleFragment& V = *Vehicles[Self].V;
		const FRammsTrafficDrivingParams& P = *Vehicles[Self].P;
		if (!N.IsValidLane(V.Lane))
		{
			return;
		}
		const FRammsTrafficLane& Lane = N.Lanes[V.Lane];
		const float Front = V.Distance + V.HalfLength;
		const float Back = V.Distance - V.HalfLength;
		const float Everyone = TNumericLimits<float>::Lowest();
		auto HoldAlong = [this, &N](const int32 LaneIndex, const float From, const float To, const float Since)
		{
			for (const FRammsTrafficCrossingPoint& X : N.Lanes[LaneIndex].Crossings)
			{
				if (X.Distance > To)
				{
					break;
				}
				if (X.Distance >= From)
				{
					Hold(X.PedestrianLane, Since);
				}
			}
		};
		// On into the next lane: always along a road or out of an intersection, into one only when let in.
		const bool bApproaching = ApproachZone(V) != INDEX_NONE;
		const bool bOnInto = N.IsValidLane(V.NextLane) && (!bApproaching || V.bGranted);

		// Nobody walks through a car, whatever it is doing.
		HoldAlong(V.Lane, Back - 50.0f, Front + 50.0f, Everyone);
		if (N.IsValidLane(V.PrevLane) && Back < V.PrevLaneEnd)
		{
			// Its rear on the lane it left (PrevLaneEnd: where that lane ended, along this one).
			const float PrevLength = N.Lanes[V.PrevLane].Length;
			HoldAlong(V.PrevLane, PrevLength + (Back - V.PrevLaneEnd) - 50.0f, PrevLength, Everyone);
		}
		if (bOnInto && Front + 50.0f > Lane.Length)
		{
			HoldAlong(V.NextLane, 0.0f, Front + 50.0f - Lane.Length, Everyone);
		}

		float Since = Everyone;
		if (V.WalkersSince >= 0.0f)
		{
			if (V.StuckWalkerWait > StuckWalkerReleaseSeconds)
			{
				return;
			}
			Since = V.WalkersSince;
		}
		else if (V.Speed <= 50.0f)
		{
			return;
		}
		const float Stopping = V.Speed * V.Speed / (2.0f * FMath::Max(P.ComfortDeceleration, 10.0f));
		const float Reach = Front + FMath::Clamp(FMath::Max(V.Speed * P.CrossingHoldSeconds, Stopping), 300.0f, 2500.0f);
		HoldAlong(V.Lane, Back - 50.0f, Reach, Since);
		if (bOnInto && Reach > Lane.Length)
		{
			HoldAlong(V.NextLane, 0.0f, Reach - Lane.Length, Since);
		}
	}
}
