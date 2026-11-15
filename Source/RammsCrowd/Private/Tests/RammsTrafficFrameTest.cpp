// Copyright (c) RAMMP. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RammsCrossingGateProcessor.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsTrafficRules.h"
#include "RammsTrafficSubsystem.h"
#include "RammsTrafficTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The traffic's per-frame rules (RammsTraffic::FTrafficFrame, the code URammsTrafficVehicleProcessor runs) over
 * hand-made networks: vehicles are plain fragments, no world or Mass needed.
 */
namespace
{
	using RammsTraffic::FTrafficFrame;

	const FZoneGraphDataHandle TestData(1, 1);

	/** A hand-made network: lanes as straight polylines (world cm, z = 0). */
	struct FTestNetwork
	{
		FRammsTrafficNetwork Net;

		int32 AddLane(const TArray<FVector2D>& Points, const int32 Zone = INDEX_NONE, const uint16 StartEntry = 0, const uint16 EndEntry = 0, const bool bSignalized = false, const float HalfWidth = 160.0f)
		{
			const int32 Index = Net.Lanes.AddDefaulted();
			FRammsTrafficLane& Lane = Net.Lanes[Index];
			float Along = 0.0f;
			for (int32 i = 0; i < Points.Num(); ++i)
			{
				const FVector Point(Points[i].X, Points[i].Y, 0.0);
				if (i > 0)
				{
					Along += static_cast<float>(FVector::Dist(Lane.Points.Last(), Point));
				}
				Lane.Points.Add(Point);
				Lane.Progress.Add(Along);
			}
			Lane.Length = Along;
			Lane.HalfWidth = HalfWidth;
			Lane.Zone = Zone;
			Lane.StartEntry = StartEntry;
			Lane.EndEntry = EndEntry;
			Lane.bSignalized = bSignalized;
			const FVector2D StartDirection(Lane.Points[1] - Lane.Points[0]);
			const FVector2D EndDirection(Lane.Points.Last() - Lane.Points[Lane.Points.Num() - 2]);
			Lane.Axis = static_cast<uint8>(RammsTraffic::TravelAxis(StartDirection));
			Lane.Turn = RammsTraffic::ClassifyTurn(StartDirection, EndDirection);
			Lane.Handle = FZoneGraphLaneHandle(Index, TestData);
			Net.LaneByHandle.Add(Lane.Handle, Index);
			return Index;
		}

		void Link(const int32 From, const int32 To)
		{
			Net.Lanes[From].Next.AddUnique(To);
		}

		FRammsTrafficNetworkStats Finish()
		{
			Net.Version = 1;
			return RammsTraffic::FinalizeNetwork(Net, RammsTraffic::ConflictDistance(190.0));
		}
	};

	/**
	 * A four-way intersection (zone 0) of two two-way roads, right-hand traffic (+Y is right of +X), its box 20 m across.
	 * Entries: 0 west, 1 south (-Y), 2 east, 3 north (+Y). A drives +X, B +Y, C -X, D -Y; each has a straight lane
	 * through, A and B a left turn.
	 */
	struct FCrossroads : FTestNetwork
	{
		int32 AIn, AStraight, ALeft, AOut;
		int32 BIn, BStraight, BLeft, BOut;
		int32 CIn, CStraight, COut;
		int32 DIn, DStraight, DOut;

		explicit FCrossroads(const bool bSignalized)
		{
			AIn = AddLane({ FVector2D(-6000.0, 160.0), FVector2D(-1000.0, 160.0) });
			AStraight = AddLane({ FVector2D(-1000.0, 160.0), FVector2D(1000.0, 160.0) }, 0, 0, 2, bSignalized);
			ALeft = AddLane({ FVector2D(-1000.0, 160.0), FVector2D(160.0, 160.0), FVector2D(160.0, -1000.0) }, 0, 0, 1, bSignalized);
			AOut = AddLane({ FVector2D(1000.0, 160.0), FVector2D(6000.0, 160.0) });
			BIn = AddLane({ FVector2D(-160.0, -6000.0), FVector2D(-160.0, -1000.0) });
			BStraight = AddLane({ FVector2D(-160.0, -1000.0), FVector2D(-160.0, 1000.0) }, 0, 1, 3, bSignalized);
			BLeft = AddLane({ FVector2D(-160.0, -1000.0), FVector2D(-160.0, 160.0), FVector2D(1000.0, 160.0) }, 0, 1, 2, bSignalized);
			BOut = AddLane({ FVector2D(-160.0, 1000.0), FVector2D(-160.0, 6000.0) });
			CIn = AddLane({ FVector2D(6000.0, -160.0), FVector2D(1000.0, -160.0) });
			CStraight = AddLane({ FVector2D(1000.0, -160.0), FVector2D(-1000.0, -160.0) }, 0, 2, 0, bSignalized);
			COut = AddLane({ FVector2D(-1000.0, -160.0), FVector2D(-6000.0, -160.0) });
			DIn = AddLane({ FVector2D(160.0, 6000.0), FVector2D(160.0, 1000.0) });
			DStraight = AddLane({ FVector2D(160.0, 1000.0), FVector2D(160.0, -1000.0) }, 0, 3, 1, bSignalized);
			DOut = AddLane({ FVector2D(160.0, -1000.0), FVector2D(160.0, -6000.0) });
			Link(AIn, AStraight);
			Link(AIn, ALeft);
			Link(AStraight, AOut);
			Link(ALeft, DOut);
			Link(BIn, BStraight);
			Link(BIn, BLeft);
			Link(BStraight, BOut);
			Link(BLeft, AOut);
			Link(CIn, CStraight);
			Link(CStraight, COut);
			Link(DIn, DStraight);
			Link(DStraight, DOut);
			Finish();
		}
	};

	/** The vehicles of a test: plain fragments, and one frame over them at a time. */
	struct FTestTraffic
	{
		const FRammsTrafficNetwork& Net;
		FRammsTrafficDrivingParams Params;
		TArray<FRammsTrafficVehicleFragment> Vehicles;
		TArray<FTransform> Transforms;
		bool bHasSignals = false;
		FRammsSignalState Signals;
		TArray<FVector> Players;
		double Now = 100.0;

		explicit FTestTraffic(const FRammsTrafficNetwork& InNet)
			: Net(InNet)
		{
			Vehicles.Reserve(16);
			Transforms.Reserve(16);
		}

		/** A 4.5 m car on Lane, its front bumper FrontToLaneEnd short of the lane's end (StopLineGap more than its distance to the line). */
		int32 Add(const int32 Lane, const int32 NextLane, const float FrontToLaneEnd, const float Speed)
		{
			FRammsTrafficVehicleFragment& V = Vehicles.AddDefaulted_GetRef();
			V.Lane = Lane;
			V.NextLane = NextLane;
			V.NetworkVersion = Net.Version;
			V.HalfLength = 225.0f;
			V.HalfWidth = 95.0f;
			V.Distance = Net.Lanes[Lane].Length - FrontToLaneEnd - V.HalfLength;
			V.Speed = Speed;
			V.DesiredSpeed = 1000.0f;
			V.RandomState = 7u + static_cast<uint32>(Vehicles.Num());
			V.bTeleport = false;
			Transforms.AddDefaulted();
			return Vehicles.Num() - 1;
		}

		/** A frame over the vehicles as they stand (gathered), ready for its entry decisions. */
		void Begin(FTrafficFrame& F, const float Dt = 1.0f / 30.0f)
		{
			F.Begin(Net, Now, Dt);
			F.bHasSignals = bHasSignals;
			F.Signals = Signals;
			F.Players = Players;
			for (int32 i = 0; i < Vehicles.Num(); ++i)
			{
				F.AddVehicle(Vehicles[i], Transforms[i], nullptr, Params);
			}
			F.GatherTraffic();
		}

		/** One whole frame: gather, entry decisions, drive. */
		void Step(const float Dt = 1.0f / 30.0f)
		{
			FTrafficFrame F;
			Begin(F, Dt);
			F.DecideEntries();
			F.Drive();
			Now += Dt;
		}
	};

	FRammsSignalState GreenForX()
	{
		FRammsSignalState S;
		S.LightX = ERammsSignalLight::Green;
		S.LightY = ERammsSignalLight::Red;
		S.SecondsToChangeX = 10.0f;
		S.SecondsToChangeY = 15.0f;
		return S;
	}

	bool HasClaimBy(const FTrafficFrame& F, const int32 Zone, const int32 Vehicle)
	{
		return F.Claims[Zone].ContainsByPredicate([Vehicle](const RammsTraffic::FClaim& C) { return C.Vehicle == Vehicle; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficNetworkTest, "Ramms.Crowd.Traffic.Network",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficNetworkTest::RunTest(const FString& Parameters)
{
	const FCrossroads X(false);
	const FRammsTrafficNetwork& N = X.Net;
	TestTrue(TEXT("crossing straight lanes conflict"), N.Conflict(X.AStraight, X.BStraight) && N.Conflict(X.BStraight, X.AStraight));
	TestFalse(TEXT("opposite straight lanes 3.2 m apart do not"), N.Conflict(X.AStraight, X.CStraight));
	TestTrue(TEXT("a left turn conflicts with oncoming straight traffic"), N.Conflict(X.ALeft, X.CStraight));
	TestTrue(TEXT("lanes into one exit merge"), N.Conflict(X.BLeft, X.AStraight));
	TestFalse(TEXT("lanes from one entry diverge: no conflict"), N.Conflict(X.AStraight, X.ALeft));
	TestTrue(TEXT("lanes from one entry are siblings"), N.Lanes[X.AStraight].Siblings.Contains(X.ALeft));
	TestTrue(TEXT("road lanes into the box are approaches"), N.Lanes[X.AIn].bApproach && N.Zones[0].Approaches.Contains(X.AIn));
	TestEqual(TEXT("left turn classified"), N.Lanes[X.ALeft].Turn, ERammsTrafficTurn::Left);

	// Sampling the lane polylines: halfway along A's left turn's first leg, and clamped past its end.
	FVector Position, Direction;
	TestTrue(TEXT("sample: on the lane"), N.Sample(X.ALeft, 580.0f, Position, Direction));
	TestTrue(TEXT("sample: position"), Position.Equals(FVector(-420.0, 160.0, 0.0), 0.1));
	TestTrue(TEXT("sample: direction"), Direction.Equals(FVector(1.0, 0.0, 0.0), 0.001));
	TestTrue(TEXT("sample: clamped to the end"), N.Sample(X.ALeft, 1.0e6f, Position, Direction) && Position.Equals(FVector(160.0, -1000.0, 0.0), 0.1));

	// An intersection lane straight into another intersection's lane (no road between): counted, and an approach.
	FTestNetwork T;
	const int32 Road = T.AddLane({ FVector2D(0.0, 0.0), FVector2D(3000.0, 0.0) });
	const int32 First = T.AddLane({ FVector2D(3000.0, 0.0), FVector2D(4000.0, 0.0) }, 0, 0, 1);
	const int32 Second = T.AddLane({ FVector2D(4000.0, 0.0), FVector2D(5000.0, 0.0) }, 1, 0, 1);
	const int32 Away = T.AddLane({ FVector2D(5000.0, 0.0), FVector2D(9000.0, 0.0) });
	T.Link(Road, First);
	T.Link(First, Second);
	T.Link(Second, Away);
	const FRammsTrafficNetworkStats Stats = T.Finish();
	TestEqual(TEXT("zone to zone: counted"), Stats.ZoneToZone, 1);
	TestEqual(TEXT("dead ends: the last road"), Stats.DeadEnds, 1);
	TestTrue(TEXT("zone to zone: the first zone's lane approaches the second"), T.Net.Lanes[First].bApproach && T.Net.Zones[1].Approaches.Contains(First));

	FTestTraffic Traffic(T.Net);
	const int32 Car = Traffic.Add(First, Second, 500.0f, 300.0f);
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		TestEqual(TEXT("zone to zone: a car on the first decides on the second"), F.ApproachZone(Traffic.Vehicles[Car]), 1);
		TestEqual(TEXT("zone to zone: it is approaching it"), F.Approaching[1].Num(), 1);
	}
	TestEqual(TEXT("conflict distance: the widest car and 30 cm"), RammsTraffic::ConflictDistance(190.0), 220.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficCommittedClaimTest, "Ramms.Crowd.Traffic.DeadlockBreaker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficCommittedClaimTest::RunTest(const FString& Parameters)
{
	// All-way stop: X has stood at its line far past MaxWaitSeconds; Y, on the crossing road, has been let in.
	{
		const FCrossroads Roads(false);
		FTestTraffic Traffic(Roads.Net);
		const int32 X = Traffic.Add(Roads.AIn, Roads.AStraight, 40.0f, 0.0f);
		Traffic.Vehicles[X].StoppedAtLine = 2.0f;
		Traffic.Vehicles[X].ArrivalTime = 50.0f;
		Traffic.Vehicles[X].BlockedTime = 25.0f;
		// Y's front is a metre over its line: it cannot stop any more.
		const int32 Y = Traffic.Add(Roads.BIn, Roads.BStraight, -60.0f, 300.0f);
		Traffic.Vehicles[Y].bGranted = true;
		FTrafficFrame F;
		Traffic.Begin(F);
		TestTrue(TEXT("committed: its claim is committed"), F.Claims[0].ContainsByPredicate([Y](const RammsTraffic::FClaim& C) { return C.Vehicle == Y && C.bCommitted && !C.bInside; }));
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("committed: the deadlocked car still waits"), F.DecideEntry(X, Blocker), ERammsTrafficYield::Claim);
		TestEqual(TEXT("committed: for it"), Blocker, Y);
		TestFalse(TEXT("committed: not let in"), Traffic.Vehicles[X].bGranted);
	}
	{
		const FCrossroads Roads(false);
		FTestTraffic Traffic(Roads.Net);
		const int32 X = Traffic.Add(Roads.AIn, Roads.AStraight, 40.0f, 0.0f);
		Traffic.Vehicles[X].StoppedAtLine = 2.0f;
		Traffic.Vehicles[X].ArrivalTime = 50.0f;
		Traffic.Vehicles[X].BlockedTime = 25.0f;
		// Y let in 8 m short of its line at 2 m/s: it can still stop.
		const int32 Y = Traffic.Add(Roads.BIn, Roads.BStraight, 840.0f, 200.0f);
		Traffic.Vehicles[Y].bGranted = true;
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("only let in: the deadlocked car goes"), F.DecideEntry(X, Blocker), ERammsTrafficYield::None);
		TestTrue(TEXT("only let in: let in"), Traffic.Vehicles[X].bGranted);
		// Not deadlocked, it waits for the car let in before it.
		Traffic.Vehicles[X].bGranted = false;
		Traffic.Vehicles[X].BlockedTime = 0.0f;
		FTrafficFrame G;
		Traffic.Begin(G);
		TestEqual(TEXT("only let in, not deadlocked: waits"), G.DecideEntry(X, Blocker), ERammsTrafficYield::Claim);
	}
	// Signalized, X green: a deadlocked permitted left and an oncoming car that can no longer stop.
	{
		const FCrossroads Roads(true);
		FTestTraffic Traffic(Roads.Net);
		Traffic.bHasSignals = true;
		Traffic.Signals = GreenForX();
		const int32 L = Traffic.Add(Roads.AIn, Roads.ALeft, 50.0f, 0.0f);
		Traffic.Vehicles[L].BlockedTime = 25.0f;
		const int32 O = Traffic.Add(Roads.CIn, Roads.CStraight, 540.0f, 1000.0f);
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("left, deadlocked: still waits for an oncoming car that cannot stop"), F.DecideEntry(L, Blocker), ERammsTrafficYield::OncomingLeft);
		TestEqual(TEXT("left, deadlocked: for it"), Blocker, O);
	}
	{
		const FCrossroads Roads(true);
		FTestTraffic Traffic(Roads.Net);
		Traffic.bHasSignals = true;
		Traffic.Signals = GreenForX();
		const int32 L = Traffic.Add(Roads.AIn, Roads.ALeft, 50.0f, 0.0f);
		Traffic.Vehicles[L].BlockedTime = 25.0f;
		// 30 m out at 10 m/s: within the gap, but it can stop.
		Traffic.Add(Roads.CIn, Roads.CStraight, 3040.0f, 1000.0f);
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("left, deadlocked: goes before an oncoming car that can stop"), F.DecideEntry(L, Blocker), ERammsTrafficYield::None);
		Traffic.Vehicles[L].bGranted = false;
		Traffic.Vehicles[L].BlockedTime = 0.0f;
		FTrafficFrame G;
		Traffic.Begin(G);
		TestEqual(TEXT("left, not deadlocked: waits for it"), G.DecideEntry(L, Blocker), ERammsTrafficYield::OncomingLeft);
	}
	// A car let in that can no longer stop keeps its leave whatever turns up.
	{
		const FCrossroads Roads(false);
		FTestTraffic Traffic(Roads.Net);
		const int32 Y = Traffic.Add(Roads.BIn, Roads.BStraight, 300.0f, 1000.0f);
		Traffic.Vehicles[Y].bGranted = true;
		const int32 Z = Traffic.Add(Roads.AStraight, Roads.AOut, 1000.0f, 0.0f); // in the box, on a crossing lane
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("committed: keeps going"), F.DecideEntry(Y, Blocker), ERammsTrafficYield::None);
		TestTrue(TEXT("committed: still let in"), Traffic.Vehicles[Y].bGranted && Z != INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficQueueTest, "Ramms.Crowd.Traffic.QueuedCarTakesNoClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficQueueTest::RunTest(const FString& Parameters)
{
	const FCrossroads Roads(true);
	FTestTraffic Traffic(Roads.Net);
	Traffic.bHasSignals = true;
	Traffic.Signals = GreenForX();
	// Q1 waits at its line to turn left; oncoming traffic is coming. Q2, behind it, goes straight.
	const int32 Q1 = Traffic.Add(Roads.AIn, Roads.ALeft, 50.0f, 0.0f);
	const int32 Q2 = Traffic.Add(Roads.AIn, Roads.AStraight, 750.0f, 0.0f);
	Traffic.Add(Roads.CIn, Roads.CStraight, 2040.0f, 1000.0f);
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
		TestFalse(TEXT("the left turn waits"), Traffic.Vehicles[Q1].bGranted);
		TestEqual(TEXT("the car behind it queues"), Traffic.Vehicles[Q2].Entry, ERammsTrafficYield::Queue);
		TestEqual(TEXT("behind it"), Traffic.Vehicles[Q2].EntryBlocker, Q1);
		TestFalse(TEXT("queued: not let in"), Traffic.Vehicles[Q2].bGranted);
		TestFalse(TEXT("queued: no claim"), HasClaimBy(F, 0, Q2));
		TestTrue(TEXT("the left turn waits at its line for the oncoming car: it may go on the amber"), Traffic.Vehicles[Q1].bLeftWaiting);
	}
	// Once the car ahead has been let in and moves, the one behind may be let in too.
	Traffic.Vehicles[Q1].bGranted = true;
	Traffic.Vehicles[Q1].Speed = 300.0f;
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
		TestTrue(TEXT("behind a car let in and moving: let in"), Traffic.Vehicles[Q2].bGranted);
		TestTrue(TEXT("and holds a claim"), HasClaimBy(F, 0, Q2));
	}
	// A queued car follows the car ahead: no stop line of its own (it does not brake short of the line behind it).
	{
		const FCrossroads Platoon(true);
		FTestTraffic Moving(Platoon.Net);
		Moving.bHasSignals = true;
		Moving.Signals = GreenForX();
		const int32 Lead = Moving.Add(Platoon.AIn, Platoon.ALeft, 50.0f, 0.0f);
		const int32 Behind = Moving.Add(Platoon.AIn, Platoon.AStraight, 1200.0f, 600.0f);
		Moving.Add(Platoon.CIn, Platoon.CStraight, 2040.0f, 1000.0f);
		Moving.Step();
		TestEqual(TEXT("queued while still rolling"), Moving.Vehicles[Behind].Entry, ERammsTrafficYield::Queue);
		TestEqual(TEXT("queued: limited by the car ahead"), Moving.Vehicles[Behind].Yield, ERammsTrafficYield::Leader);
		TestEqual(TEXT("queued: behind the car ahead"), Moving.Vehicles[Behind].Blocker, Lead);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficBoxTest, "Ramms.Crowd.Traffic.Box",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficBoxTest::RunTest(const FString& Parameters)
{
	// X waits at its line on green, its exit blocked by a stopped car just past the box.
	for (const bool bDeadlocked : { false, true })
	{
		const FCrossroads Roads(true);
		FTestTraffic Traffic(Roads.Net);
		Traffic.bHasSignals = true;
		Traffic.Signals = GreenForX();
		const int32 X = Traffic.Add(Roads.AIn, Roads.AStraight, 40.0f, 0.0f);
		Traffic.Vehicles[X].BlockedTime = bDeadlocked ? 25.0f : 0.0f;
		const int32 S = Traffic.Add(Roads.AOut, INDEX_NONE, Roads.Net.Lanes[Roads.AOut].Length - 525.0f, 0.0f); // its rear 75 cm into the exit
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(bDeadlocked ? TEXT("full exit, deadlocked: still the box") : TEXT("full exit: the box"), F.DecideEntry(X, Blocker), ERammsTrafficYield::Box);
		TestEqual(TEXT("full exit: for the car on it"), Blocker, S);
	}
	// Room for one car on the exit, but another car has been let in to it (and can still stop): a deadlocked car
	// that no longer waits for it goes, as the exit has room counting the cars already in the box.
	{
		const FCrossroads Roads(true);
		FTestTraffic Traffic(Roads.Net);
		Traffic.bHasSignals = true;
		Traffic.Signals = GreenForX();
		const int32 X = Traffic.Add(Roads.AIn, Roads.AStraight, 40.0f, 0.0f);
		Traffic.Vehicles[X].BlockedTime = 25.0f;
		Traffic.Add(Roads.AOut, INDEX_NONE, Roads.Net.Lanes[Roads.AOut].Length - 1425.0f, 0.0f); // rear 975 cm in
		const int32 K = Traffic.Add(Roads.BIn, Roads.BLeft, 840.0f, 100.0f);
		Traffic.Vehicles[K].bGranted = true;
		FTrafficFrame F;
		Traffic.Begin(F);
		int32 Blocker = INDEX_NONE;
		TestEqual(TEXT("room once the cars only let in are left out: goes"), F.DecideEntry(X, Blocker), ERammsTrafficYield::None);
		// The same car not deadlocked waits for the car let in.
		Traffic.Vehicles[X].bGranted = false;
		Traffic.Vehicles[X].BlockedTime = 0.0f;
		FTrafficFrame G;
		Traffic.Begin(G);
		TestEqual(TEXT("not deadlocked: waits for the car let in to its exit"), G.DecideEntry(X, Blocker), ERammsTrafficYield::Claim);
		TestEqual(TEXT("for it"), Blocker, K);
	}
	// Time at the box does not count toward the deadlock breaker.
	{
		const FCrossroads Roads(true);
		FTestTraffic Traffic(Roads.Net);
		Traffic.bHasSignals = true;
		Traffic.Signals = GreenForX();
		const int32 X = Traffic.Add(Roads.AIn, Roads.AStraight, 40.0f, 0.0f);
		const int32 S = Traffic.Add(Roads.AOut, INDEX_NONE, Roads.Net.Lanes[Roads.AOut].Length - 525.0f, 0.0f);
		const float Parked = Traffic.Vehicles[S].Distance;
		for (int32 i = 0; i < 60; ++i)
		{
			// The car on the exit does not move (it stands there for the whole test).
			Traffic.Vehicles[S].Distance = Parked;
			Traffic.Vehicles[S].Speed = 0.0f;
			Traffic.Step(0.5f);
		}
		TestEqual(TEXT("box: still held"), Traffic.Vehicles[X].Entry, ERammsTrafficYield::Box);
		TestEqual(TEXT("box: not counted as a deadlock"), Traffic.Vehicles[X].BlockedTime, 0.0f, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficAllWayStopTest, "Ramms.Crowd.Traffic.AllWayStopOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficAllWayStopTest::RunTest(const FString& Parameters)
{
	const FCrossroads Roads(false);
	FTestTraffic Traffic(Roads.Net);
	// Four cars standing at their lines, arrived B, A, D, C (a centimetre apart from their lines in that order, so the
	// order the decisions are made in is fixed: nearest the line first).
	const int32 A = Traffic.Add(Roads.AIn, Roads.AStraight, 41.0f, 0.0f);
	const int32 B = Traffic.Add(Roads.BIn, Roads.BStraight, 40.0f, 0.0f);
	const int32 C = Traffic.Add(Roads.CIn, Roads.CStraight, 43.0f, 0.0f);
	const int32 D = Traffic.Add(Roads.DIn, Roads.DStraight, 42.0f, 0.0f);
	const float Arrivals[] = { 11.0f, 10.0f, 13.0f, 12.0f };
	for (int32 i = 0; i < 4; ++i)
	{
		Traffic.Vehicles[i].StoppedAtLine = 2.0f;
		Traffic.Vehicles[i].ArrivalTime = Arrivals[i];
	}
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
		TestTrue(TEXT("first to arrive goes first"), Traffic.Vehicles[B].bGranted);
		TestFalse(TEXT("A waits"), Traffic.Vehicles[A].bGranted);
		TestFalse(TEXT("C waits"), Traffic.Vehicles[C].bGranted);
		TestFalse(TEXT("D waits"), Traffic.Vehicles[D].bGranted);
		TestEqual(TEXT("A waits for B"), Traffic.Vehicles[A].EntryBlocker, B);
		TestEqual(TEXT("D waits for A (it arrived first, and they cross)"), Traffic.Vehicles[D].EntryBlocker, A);
		TestEqual(TEXT("C waits for D (it arrived first, and they cross)"), Traffic.Vehicles[C].EntryBlocker, D);
	}
	// B has crossed and gone: A next; D still waits for A, C for D (A and C do not cross).
	FRammsTrafficVehicleFragment& Gone = Traffic.Vehicles[B];
	Gone.Lane = Roads.BOut;
	Gone.NextLane = INDEX_NONE;
	Gone.Distance = 2000.0f;
	Gone.Speed = 800.0f;
	Gone.bGranted = false;
	Gone.ArrivalTime = -1.0f;
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
		TestTrue(TEXT("second to arrive goes next"), Traffic.Vehicles[A].bGranted);
		TestFalse(TEXT("D waits"), Traffic.Vehicles[D].bGranted);
		TestEqual(TEXT("D waits for A"), Traffic.Vehicles[D].EntryBlocker, A);
		TestFalse(TEXT("C waits"), Traffic.Vehicles[C].bGranted);
		TestEqual(TEXT("C waits for D, which arrived before it"), Traffic.Vehicles[C].EntryBlocker, D);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficPermittedLeftTest, "Ramms.Crowd.Traffic.PermittedLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficPermittedLeftTest::RunTest(const FString& Parameters)
{
	const FCrossroads Roads(true);
	FTestTraffic Traffic(Roads.Net);
	Traffic.bHasSignals = true;
	Traffic.Signals = GreenForX();
	const int32 L = Traffic.Add(Roads.AIn, Roads.ALeft, 50.0f, 0.0f);
	const int32 O = Traffic.Add(Roads.CIn, Roads.CStraight, 2040.0f, 1000.0f);
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
		TestTrue(TEXT("green: the oncoming car is let in"), Traffic.Vehicles[O].bGranted);
		TestFalse(TEXT("green: the left turn yields"), Traffic.Vehicles[L].bGranted);
		TestTrue(TEXT("green: it waited at its line"), Traffic.Vehicles[L].bLeftWaiting);
	}
	// Amber: the oncoming car can stop comfortably and does; the left turn that waited goes.
	Traffic.Signals.LightX = ERammsSignalLight::Amber;
	Traffic.Signals.SecondsToChangeX = 3.0f;
	// (The first amber frame still sees the oncoming car's claim from the green; the next one does not.)
	for (int32 Frame = 0; Frame < 2; ++Frame)
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.DecideEntries();
	}
	TestEqual(TEXT("amber: the oncoming car stops"), Traffic.Vehicles[O].Entry, ERammsTrafficYield::Signal);
	TestTrue(TEXT("amber: the left turn goes"), Traffic.Vehicles[L].bGranted);
	// A left turn that did not wait at the line stops for the amber like everyone.
	{
		const FCrossroads Late(true);
		FTestTraffic Other(Late.Net);
		Other.bHasSignals = true;
		Other.Signals = GreenForX();
		Other.Signals.LightX = ERammsSignalLight::Amber;
		const int32 M = Other.Add(Late.AIn, Late.ALeft, 1500.0f, 500.0f);
		FTrafficFrame F;
		Other.Begin(F);
		F.DecideEntries();
		TestEqual(TEXT("amber, did not wait: stops"), Other.Vehicles[M].Entry, ERammsTrafficYield::Signal);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficWalkersTest, "Ramms.Crowd.Traffic.Walkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficWalkersTest::RunTest(const FString& Parameters)
{
	// A straight road lane along X; a car on it, front at x = 1225, 5 m/s.
	FTestNetwork T;
	const int32 Road = T.AddLane({ FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0) });
	T.Link(Road, Road);
	T.Finish();
	const FRammsStreetCrossing Crossing{ 200.0f, 520.0f, 40.0f, 680.0f };
	const FZoneGraphLaneHandle CrossingLane(42, TestData);
	const FZoneGraphLaneHandle Sidewalk(43, TestData);

	struct FCase
	{
		const TCHAR* What;
		FVector2D Position;
		FVector2D Velocity;
		bool bOnCrossing;
		bool bOnSidewalk;
		float LaneDistance;
		bool bInTheWay;
	};
	const FCase Cases[] = {
		{ TEXT("standing on a sidewalk beside the lane: not in the way"), FVector2D(2000.0, 140.0), FVector2D::ZeroVector, false, true, 0.0f, false },
		{ TEXT("walking at the same spot (on the carriageway): in the way"), FVector2D(2000.0, 140.0), FVector2D(140.0, 0.0), false, true, 0.0f, true },
		{ TEXT("standing past its crossing's last lane (held up at the far kerb): only where it stands"), FVector2D(2000.0, 140.0), FVector2D::ZeroVector, true, false, 560.0f, false },
		{ TEXT("standing short of its crossing's first lane but in the car's path: in the way"), FVector2D(2000.0, 100.0), FVector2D::ZeroVector, true, false, 50.0f, true },
		{ TEXT("standing in the middle of its crossing: in the way"), FVector2D(2000.0, 140.0), FVector2D::ZeroVector, true, false, 300.0f, true },
		{ TEXT("no lane, standing: in the way"), FVector2D(2000.0, 140.0), FVector2D::ZeroVector, false, false, 0.0f, true },
	};
	for (const FCase& Case : Cases)
	{
		FTestTraffic Traffic(T.Net);
		const int32 Car = Traffic.Add(Road, Road, 10000.0f - 1225.0f, 500.0f);
		FTrafficFrame F;
		Traffic.Begin(F);
		RammsTraffic::FWalker W;
		W.Entity = FMassEntityHandle(5, 1);
		W.Location = FVector(Case.Position.X, Case.Position.Y, 0.0);
		W.Velocity = Case.Velocity;
		W.Lane = Case.bOnCrossing ? CrossingLane : Case.bOnSidewalk ? Sidewalk : FZoneGraphLaneHandle();
		W.Crossing = Case.bOnCrossing ? &Crossing : nullptr;
		W.LaneDistance = Case.LaneDistance;
		F.AddWalker(W);
		ERammsTrafficYield Reason = ERammsTrafficYield::None;
		int32 Obstacle = INDEX_NONE;
		const float Distance = F.ScanCorridor(Traffic.Vehicles[Car], Traffic.Params, Reason, Obstacle);
		TestEqual(Case.What, Distance < TNumericLimits<float>::Max(), Case.bInTheWay);
	}

	// The walker deadlock breaker drives past the one walker it stood for, not past the next one.
	{
		FTestTraffic Traffic(T.Net);
		const int32 Car = Traffic.Add(Road, Road, 10000.0f - 1225.0f, 0.0f);
		FTrafficFrame F;
		Traffic.Begin(F);
		RammsTraffic::FWalker First;
		First.Entity = FMassEntityHandle(11, 1);
		First.Location = FVector(1700.0, 0.0, 0.0);
		RammsTraffic::FWalker Second = First;
		Second.Entity = FMassEntityHandle(12, 1);
		Second.Location = FVector(2100.0, 0.0, 0.0);
		F.AddWalker(First);
		const int32 SecondIndex = F.AddWalker(Second);
		ERammsTrafficYield Reason = ERammsTrafficYield::None;
		int32 Obstacle = INDEX_NONE;
		TestTrue(TEXT("breaker off: stops for the first walker"), F.ScanCorridor(Traffic.Vehicles[Car], Traffic.Params, Reason, Obstacle) < 600.0f);
		Traffic.Vehicles[Car].PastWalker = First.Entity;
		Traffic.Vehicles[Car].PastStandingWalkers = 1200.0f;
		const float Distance = F.ScanCorridor(Traffic.Vehicles[Car], Traffic.Params, Reason, Obstacle);
		TestTrue(TEXT("breaker on: still stops for the second walker"), Distance < TNumericLimits<float>::Max() && F.Pedestrians.IsValidIndex(Obstacle) && F.Pedestrians[Obstacle].Walker == SecondIndex);
	}

	// A car yielding to walkers holds newcomers at the crossing ahead, unless the walker it waits for stands (held up by
	// the crowd at the kerb): then it lets them move.
	{
		FTestNetwork Held;
		const int32 Lane = Held.AddLane({ FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0) });
		Held.Link(Lane, Lane);
		Held.Net.Lanes[Lane].Crossings.Add(FRammsTrafficCrossingPoint{ CrossingLane, 1400.0f });
		Held.Finish();
		for (const bool bStuck : { false, true })
		{
			FTestTraffic Traffic(Held.Net);
			const int32 Car = Traffic.Add(Lane, Lane, 10000.0f - 1225.0f, 0.0f);
			Traffic.Vehicles[Car].WalkersSince = 90.0f;
			Traffic.Vehicles[Car].StuckWalkerWait = bStuck ? 3.0f : 0.0f;
			FTrafficFrame F;
			Traffic.Begin(F);
			F.HoldCrossings(Car);
			const float* Since = F.HeldCrossings.Find(CrossingLane);
			if (bStuck)
			{
				TestNull(TEXT("its walker stands: newcomers are not held"), Since);
			}
			else
			{
				TestTrue(TEXT("yielding to walkers: newcomers since it began are held"), Since != nullptr && FMath::IsNearlyEqual(*Since, 90.0f));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficRobotTest, "Ramms.Crowd.Traffic.Robot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficRobotTest::RunTest(const FString& Parameters)
{
	// A narrow lane (2.5 m); the robot a 60 cm circle beside it.
	FTestNetwork T;
	const int32 Road = T.AddLane({ FVector2D(0.0, 0.0), FVector2D(10000.0, 0.0) }, INDEX_NONE, 0, 0, false, 125.0f);
	T.Link(Road, Road);
	T.Finish();
	for (const double Offset : { 200.0, 170.0 })
	{
		FTestTraffic Traffic(T.Net);
		const int32 Car = Traffic.Add(Road, Road, 10000.0f - 1225.0f, 500.0f);
		FTrafficFrame F;
		Traffic.Begin(F);
		RammsTraffic::FObstacleCapsule Robot;
		Robot.A = Robot.B = FVector2D(2000.0, Offset);
		Robot.Radius = 60.0f;
		F.AddFootprint(Robot, FVector2D::ZeroVector);
		ERammsTrafficYield Reason = ERammsTrafficYield::None;
		int32 Obstacle = INDEX_NONE;
		const bool bStops = F.ScanCorridor(Traffic.Vehicles[Car], Traffic.Params, Reason, Obstacle) < TNumericLimits<float>::Max();
		if (Offset > 180.0)
		{
			TestFalse(TEXT("robot waiting on the sidewalk beside the lane: the car passes"), bStops);
		}
		else
		{
			TestTrue(TEXT("robot reaching onto the carriageway: the car stops"), bStops && Reason == ERammsTrafficYield::Robot);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficLaneEndsTest, "Ramms.Crowd.Traffic.LaneEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficLaneEndsTest::RunTest(const FString& Parameters)
{
	// A dead end, and a loop far away to start again on.
	FTestNetwork T;
	const int32 DeadEnd = T.AddLane({ FVector2D(0.0, 0.0), FVector2D(3000.0, 0.0) });
	const int32 Loop = T.AddLane({ FVector2D(0.0, 6000.0), FVector2D(6000.0, 6000.0) });
	T.Link(Loop, Loop);
	T.Finish();
	for (const bool bWatched : { false, true })
	{
		FTestTraffic Traffic(T.Net);
		const int32 Car = Traffic.Add(DeadEnd, INDEX_NONE, 1500.0f, 1000.0f);
		if (bWatched)
		{
			Traffic.Players.Add(FVector(2500.0, 0.0, 0.0));
		}
		bool bOvershot = false;
		bool bStoppedForIt = false;
		for (int32 i = 0; i < 150 && Traffic.Vehicles[Car].Lane == DeadEnd; ++i)
		{
			Traffic.Step();
			const FRammsTrafficVehicleFragment& V = Traffic.Vehicles[Car];
			if (V.Lane == DeadEnd)
			{
				bOvershot |= V.Distance + V.HalfLength > T.Net.Lanes[DeadEnd].Length - Traffic.Params.StopLineGap + 1.0f;
				bStoppedForIt |= V.Yield == ERammsTrafficYield::DeadEnd;
			}
		}
		TestFalse(TEXT("dead end: never past its end"), bOvershot);
		TestTrue(TEXT("dead end: it stops for it"), bStoppedForIt || !bWatched);
		if (bWatched)
		{
			TestEqual(TEXT("dead end, a player near: it waits there"), Traffic.Vehicles[Car].Lane, DeadEnd);
			TestTrue(TEXT("dead end, a player near: standing"), Traffic.Vehicles[Car].Speed < 1.0f);
		}
		else
		{
			TestEqual(TEXT("dead end, nobody near: it starts again elsewhere"), Traffic.Vehicles[Car].Lane, Loop);
		}
	}

	// Out of one intersection over a road shorter than the car into the next: its rear keeps the first one's claim.
	FTestNetwork U;
	const int32 First = U.AddLane({ FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0) }, 0, 0, 1);
	const int32 ShortRoad = U.AddLane({ FVector2D(1000.0, 0.0), FVector2D(1100.0, 0.0) });
	const int32 Second = U.AddLane({ FVector2D(1100.0, 0.0), FVector2D(2100.0, 0.0) }, 1, 0, 1);
	const int32 Away = U.AddLane({ FVector2D(2100.0, 0.0), FVector2D(8000.0, 0.0) });
	U.Link(First, ShortRoad);
	U.Link(ShortRoad, Second);
	U.Link(Second, Away);
	U.Link(Away, Away);
	U.Finish();
	FTestTraffic Traffic(U.Net);
	const int32 Car = Traffic.Add(ShortRoad, Second, 100.0f - 225.0f - 80.0f, 300.0f); // centre 80 cm along the short road
	FRammsTrafficVehicleFragment& V = Traffic.Vehicles[Car];
	V.PrevLane = First;
	V.PrevLaneEnd = 0.0f;
	V.bGranted = true;
	for (int32 i = 0; i < 10 && V.Lane == ShortRoad; ++i)
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		F.UpdateVehicle(Car);
	}
	TestEqual(TEXT("short link: onto the next intersection"), V.Lane, Second);
	TestFalse(TEXT("short link: its leave was used up entering it"), V.bGranted);
	TestEqual(TEXT("short link: its rear is still in the first"), V.PrevLane, First);
	{
		FTrafficFrame F;
		Traffic.Begin(F);
		TestTrue(TEXT("short link: so the first one's claim stays"), F.Claims[0].ContainsByPredicate([Car](const RammsTraffic::FClaim& C) { return C.Vehicle == Car && C.bRear; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficGateTest, "Ramms.Crowd.Traffic.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficGateTest::RunTest(const FString& Parameters)
{
	const FZoneGraphLaneHandle Lane(3, TestData);
	const float Limit = URammsCrossingGateProcessor::HoldDistanceCm;

	// The signal: held at the start while closed, its walking speed given back when it opens.
	{
		FRammsCrossingWaitFragment Wait;
		float Speed = 140.0f;
		TestTrue(TEXT("signal closed: held"), URammsCrossingGateProcessor::UpdateWait(Wait, Lane, 10.0f, Limit, true, nullptr, 5.0f, Speed));
		TestEqual(TEXT("signal closed: standing"), Speed, 0.0f);
		TestFalse(TEXT("signal open: let go"), URammsCrossingGateProcessor::UpdateWait(Wait, Lane, 10.0f, Limit, false, nullptr, 6.0f, Speed));
		TestEqual(TEXT("signal open: its walking speed back"), Speed, 140.0f);
		float Further = 140.0f;
		FRammsCrossingWaitFragment Along;
		TestFalse(TEXT("signal closed, already past the start: walks on"), URammsCrossingGateProcessor::UpdateWait(Along, Lane, Limit + 20.0f, Limit, true, nullptr, 5.0f, Further));
	}
	// Who was first: a car yielding since t = 5 holds those who came onto the crossing at or after it.
	{
		FRammsCrossingWaitFragment Early;
		float Speed = 140.0f;
		URammsCrossingGateProcessor::UpdateWait(Early, Lane, 0.0f, Limit, false, nullptr, 3.0f, Speed); // came at t = 3
		const float Since = 5.0f;
		TestFalse(TEXT("there before the car: goes"), URammsCrossingGateProcessor::UpdateWait(Early, Lane, 10.0f, Limit, false, &Since, 6.0f, Speed));
		FRammsCrossingWaitFragment Late;
		float LateSpeed = 140.0f;
		URammsCrossingGateProcessor::UpdateWait(Late, Lane, 0.0f, Limit, false, nullptr, 6.0f, LateSpeed); // came at t = 6
		TestTrue(TEXT("came after the car: held"), URammsCrossingGateProcessor::UpdateWait(Late, Lane, 5.0f, Limit, false, &Since, 6.5f, LateSpeed));
		const float LaterSince = 9.0f;
		TestTrue(TEXT("waits for a car: keeps waiting while any car holds the crossing"), URammsCrossingGateProcessor::UpdateWait(Late, Lane, 5.0f, Limit, false, &LaterSince, 9.5f, LateSpeed));
		TestFalse(TEXT("no car holds it: goes"), URammsCrossingGateProcessor::UpdateWait(Late, Lane, 5.0f, Limit, false, nullptr, 10.0f, LateSpeed));
		TestEqual(TEXT("no car holds it: walking again"), LateSpeed, 140.0f);
	}
	// Waiting for the signal is not waiting for a car: on Walk it goes, unless a car driving on holds everyone.
	{
		FRammsCrossingWaitFragment Wait;
		float Speed = 140.0f;
		URammsCrossingGateProcessor::UpdateWait(Wait, Lane, 0.0f, Limit, true, nullptr, 3.0f, Speed);
		const float YieldingSince = 5.0f;
		TestFalse(TEXT("Walk, a car yielding since after it came: goes"), URammsCrossingGateProcessor::UpdateWait(Wait, Lane, 5.0f, Limit, false, &YieldingSince, 6.0f, Speed));
		FRammsCrossingWaitFragment Other;
		float OtherSpeed = 140.0f;
		URammsCrossingGateProcessor::UpdateWait(Other, Lane, 0.0f, Limit, true, nullptr, 3.0f, OtherSpeed);
		const float Everyone = TNumericLimits<float>::Lowest();
		TestTrue(TEXT("Walk, a car driving on: held"), URammsCrossingGateProcessor::UpdateWait(Other, Lane, 5.0f, Limit, false, &Everyone, 6.0f, OtherSpeed));
	}
	// Never held inside the traffic's lanes.
	const FRammsStreetCrossing Near{ 150.0f, 470.0f, 50.0f, 630.0f };
	const FRammsStreetCrossing Far{ 400.0f, 720.0f, 250.0f, 880.0f };
	TestEqual(TEXT("hold limit: not a street crossing"), URammsCrossingGateProcessor::HoldLimit(nullptr, 25.0f), URammsCrossingGateProcessor::HoldDistanceCm);
	TestEqual(TEXT("hold limit: lanes far from the start"), URammsCrossingGateProcessor::HoldLimit(&Far, 25.0f), URammsCrossingGateProcessor::HoldDistanceCm);
	TestEqual(TEXT("hold limit: lanes near the start (short of them by its radius)"), URammsCrossingGateProcessor::HoldLimit(&Near, 25.0f), 15.0f);

	// Holds go stale with the traffic frame that set them.
	FRammsHeldCrossings Holds;
	TMap<FZoneGraphLaneHandle, float> HeldNow;
	HeldNow.Add(Lane, 4.0f);
	Holds.Set(MoveTemp(HeldNow), 10.0);
	TestNotNull(TEXT("holds: fresh"), Holds.Find(Lane, 10.05));
	TestNull(TEXT("holds: a traffic frame that stopped running holds nobody"), Holds.Find(Lane, 10.0 + FRammsHeldCrossings::MaxAgeSeconds + 0.5));
	TestEqual(TEXT("holds: stale ones are not counted"), Holds.Num(10.0 + FRammsHeldCrossings::MaxAgeSeconds + 0.5), 0);
	Holds.Set(TMap<FZoneGraphLaneHandle, float>(), 11.0);
	TestNull(TEXT("holds: an empty frame clears them"), Holds.Find(Lane, 11.0));
	{
		// A frame with nothing to drive publishes no holds.
		FTestNetwork T;
		T.AddLane({ FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0) });
		T.Finish();
		FTrafficFrame F;
		F.Begin(T.Net, 10.0, 0.03f);
		F.GatherTraffic();
		F.DecideEntries();
		F.Drive();
		TestEqual(TEXT("holds: none without vehicles"), F.HeldCrossings.Num(), 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
