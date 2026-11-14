// Copyright (c) RAMMP. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RammsTrafficTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FRammsIdm TestIdm()
	{
		FRammsIdm Idm;
		Idm.DesiredSpeed = 1000.0;
		Idm.MaxAcceleration = 150.0;
		Idm.ComfortDeceleration = 250.0;
		Idm.TimeHeadway = 1.4;
		Idm.MinGap = 250.0;
		return Idm;
	}

	/** Drives a vehicle at Speed toward a stationary obstacle Gap cm ahead of its front (a stop line: MinGap 0),
	 *  the way URammsTrafficVehicleProcessor does: IDM, clamped to MaxDecel, never past the stop. */
	void ApproachStop(const FRammsIdm& Idm, double& Speed, double& Gap, const double MaxDecel, double& OutPeakDecel, const int32 Steps = 3000, const double Dt = 1.0 / 60.0)
	{
		OutPeakDecel = 0.0;
		for (int32 i = 0; i < Steps; ++i)
		{
			const double Accel = FMath::Clamp(RammsTraffic::IdmFollow(Speed, FMath::Max(Gap, 0.0), 0.0, Idm), -MaxDecel, Idm.MaxAcceleration);
			OutPeakDecel = FMath::Max(OutPeakDecel, -Accel);
			Gap -= RammsTraffic::Advance(Speed, Accel, Dt, Idm.DesiredSpeed, FMath::Max(Gap, 0.0));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficIdmTest, "Ramms.Crowd.Traffic.Idm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficIdmTest::RunTest(const FString& Parameters)
{
	const FRammsIdm Idm = TestIdm();
	TestEqual(TEXT("free road, standing: full acceleration"), RammsTraffic::IdmFree(0.0, Idm), 150.0, 0.01);
	TestEqual(TEXT("free road, at the desired speed: none"), RammsTraffic::IdmFree(1000.0, Idm), 0.0, 0.01);
	TestTrue(TEXT("free road, too fast: brakes"), RammsTraffic::IdmFree(1200.0, Idm) < 0.0);
	TestEqual(TEXT("a leader far ahead is a free road"), RammsTraffic::IdmFollow(800.0, 1.0e7, 800.0, Idm), RammsTraffic::IdmFree(800.0, Idm), 0.01);
	TestTrue(TEXT("closing fast on a near leader: brakes"), RammsTraffic::IdmFollow(1000.0, 1500.0, 0.0, Idm) < -Idm.ComfortDeceleration);
	TestTrue(TEXT("touching: brakes as hard as it can"), RammsTraffic::IdmFollow(100.0, 0.0, 0.0, Idm) < -1000.0);
	TestTrue(TEXT("standing at the minimum gap behind a stopped leader: stays"), RammsTraffic::IdmFollow(0.0, Idm.MinGap * 0.9, 0.0, Idm) < 0.0);

	// Stopping at a stop line from 10 m/s, 60 m out: stops short of it, never past it, within the braking limit.
	{
		FRammsIdm Stop = Idm;
		Stop.MinGap = 0.0;
		double Speed = 1000.0;
		double Gap = 6000.0;
		double Peak = 0.0;
		ApproachStop(Stop, Speed, Gap, 800.0, Peak);
		TestTrue(TEXT("stop line: never past it"), Gap >= -0.01);
		TestTrue(TEXT("stop line: stopped close to it"), Gap < 150.0);
		TestEqual(TEXT("stop line: standing"), Speed, 0.0, 1.0);
		TestTrue(TEXT("stop line: gentle from 60 m (about the comfortable deceleration)"), Peak <= Idm.ComfortDeceleration * 2.0);
	}
	// A late stop (15 m out at 10 m/s) brakes hard but still stops, clamped to the maximum.
	{
		FRammsIdm Stop = Idm;
		Stop.MinGap = 0.0;
		double Speed = 1000.0;
		double Gap = 1500.0;
		double Peak = 0.0;
		ApproachStop(Stop, Speed, Gap, 800.0, Peak);
		TestTrue(TEXT("late stop: never past the line"), Gap >= -0.01);
		TestEqual(TEXT("late stop: standing"), Speed, 0.0, 1.0);
		TestTrue(TEXT("late stop: within the maximum deceleration"), Peak <= 800.0 + 0.01);
	}
	// Following a stopped car: it stops about the minimum gap behind it.
	{
		double Speed = 900.0;
		double Gap = 5000.0;
		double Peak = 0.0;
		ApproachStop(Idm, Speed, Gap, 800.0, Peak);
		TestTrue(TEXT("queue: stops behind the leader, not in it"), Gap > 0.0);
		TestTrue(TEXT("queue: about the minimum gap"), Gap > Idm.MinGap * 0.8 && Gap < Idm.MinGap * 1.5);
	}
	// Advance: the hard stop.
	{
		double Speed = 1000.0;
		const double Travel = RammsTraffic::Advance(Speed, 0.0, 0.1, 1000.0, 30.0);
		TestEqual(TEXT("advance: travel clamped to the stop"), Travel, 30.0, 0.01);
		TestTrue(TEXT("advance: slowed to arrive there"), Speed <= 300.0 + 0.01);
		double Standing = 50.0;
		TestEqual(TEXT("advance: at the stop, no travel"), RammsTraffic::Advance(Standing, 100.0, 0.1, 1000.0, 0.0), 0.0, 0.001);
		TestEqual(TEXT("advance: at the stop, standing"), Standing, 0.0, 0.001);
		double Reverse = 10.0;
		RammsTraffic::Advance(Reverse, -1000.0, 0.1, 1000.0, -1.0);
		TestEqual(TEXT("advance: braking never reverses"), Reverse, 0.0, 0.001);
	}
	// Turn speed.
	TestTrue(TEXT("slow to cap: already slow, no constraint"), RammsTraffic::SlowToCap(400.0, 450.0, 1000.0) > 1.0e10);
	TestEqual(TEXT("slow to cap: v^2 - cap^2 over 2d"), RammsTraffic::SlowToCap(1000.0, 400.0, 2100.0), (400.0 * 400.0 - 1000.0 * 1000.0) / 4200.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficStopLineTest, "Ramms.Crowd.Traffic.StopLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficStopLineTest::RunTest(const FString& Parameters)
{
	using RammsTraffic::MustStopForSignal;
	const double Comfort = 350.0, Max = 800.0;
	TestFalse(TEXT("green: go"), MustStopForSignal(ERammsSignalLight::Green, 1000.0, 3000.0, Comfort, Max));
	TestTrue(TEXT("red, far: stop"), MustStopForSignal(ERammsSignalLight::Red, 1000.0, 3000.0, Comfort, Max));
	TestTrue(TEXT("red, standing at the line: stay"), MustStopForSignal(ERammsSignalLight::Red, 0.0, 5.0, Comfort, Max));
	// 10 m/s needs 1000^2 / 1600 = 625 cm to stop at 800 cm/s^2.
	TestFalse(TEXT("red, too close to stop even braking hard: committed"), MustStopForSignal(ERammsSignalLight::Red, 1000.0, 400.0, Comfort, Max));
	TestTrue(TEXT("red, just able to stop braking hard: stop"), MustStopForSignal(ERammsSignalLight::Red, 1000.0, 700.0, Comfort, Max));
	// Amber: 10 m/s stops comfortably in 1000^2 / 700 = 1429 cm.
	TestTrue(TEXT("amber, far: stop"), MustStopForSignal(ERammsSignalLight::Amber, 1000.0, 2000.0, Comfort, Max));
	TestFalse(TEXT("amber, close (would need hard braking): go through"), MustStopForSignal(ERammsSignalLight::Amber, 1000.0, 1000.0, Comfort, Max));
	TestTrue(TEXT("amber, slow and close: stop"), MustStopForSignal(ERammsSignalLight::Amber, 200.0, 300.0, Comfort, Max));
	TestFalse(TEXT("past the stopping point: never stops"), MustStopForSignal(ERammsSignalLight::Red, 0.0, -5.0, Comfort, Max));

	// Turns (Unreal: +Y is to the right of +X) and travel axes.
	TestEqual(TEXT("turn: +X to +Y is right"), RammsTraffic::ClassifyTurn(FVector2D(1.0, 0.0), FVector2D(0.0, 1.0)), ERammsTrafficTurn::Right);
	TestEqual(TEXT("turn: +X to -Y is left"), RammsTraffic::ClassifyTurn(FVector2D(1.0, 0.0), FVector2D(0.0, -1.0)), ERammsTrafficTurn::Left);
	TestEqual(TEXT("turn: -Y to +X is right"), RammsTraffic::ClassifyTurn(FVector2D(0.0, -1.0), FVector2D(1.0, 0.0)), ERammsTrafficTurn::Right);
	TestEqual(TEXT("turn: -Y to -X is left"), RammsTraffic::ClassifyTurn(FVector2D(0.0, -1.0), FVector2D(-1.0, 0.0)), ERammsTrafficTurn::Left);
	TestEqual(TEXT("turn: a slight bend is straight"), RammsTraffic::ClassifyTurn(FVector2D(1.0, 0.0), FVector2D(1.0, 0.2)), ERammsTrafficTurn::Straight);
	TestEqual(TEXT("axis: mostly X"), RammsTraffic::TravelAxis(FVector2D(-0.9, 0.4)), 0);
	TestEqual(TEXT("axis: mostly Y"), RammsTraffic::TravelAxis(FVector2D(0.3, -0.95)), 1);

	// Deterministic route randomness.
	uint32 A = 12345u, B = 12345u;
	bool bSame = true, bInRange = true;
	for (int32 i = 0; i < 100; ++i)
	{
		const float Ra = RammsTraffic::RandomFloat(A);
		bSame &= Ra == RammsTraffic::RandomFloat(B);
		bInRange &= Ra >= 0.0f && Ra < 1.0f;
	}
	TestTrue(TEXT("random: same seed, same stream"), bSame);
	TestTrue(TEXT("random: in [0, 1)"), bInRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsTrafficSignalCycleTest, "Ramms.Crowd.Traffic.SignalCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsTrafficSignalCycleTest::RunTest(const FString& Parameters)
{
	FRammsSignalTiming Timing;
	Timing.GreenSecondsX = 20.0f;
	Timing.GreenSecondsY = 15.0f;
	Timing.AmberSeconds = 3.0f;
	Timing.AllRedSeconds = 2.0f;
	Timing.PedestrianClearanceSeconds = 6.0f;
	TestEqual(TEXT("cycle length"), Timing.GetCycleSeconds(), 20.0 + 15.0 + 2.0 * (3.0 + 2.0), 0.001);

	auto At = [&Timing](const double T) { return FRammsSignalCycle::Evaluate(Timing, T); };
	TestEqual(TEXT("t=1: X green"), At(1.0).LightX, ERammsSignalLight::Green);
	TestEqual(TEXT("t=1: Y red"), At(1.0).LightY, ERammsSignalLight::Red);
	TestEqual(TEXT("t=1: walking along X: walk"), At(1.0).WalkAlongX, ERammsWalkSignal::Walk);
	TestEqual(TEXT("t=1: walking along Y: don't walk"), At(1.0).WalkAlongY, ERammsWalkSignal::DontWalk);
	TestEqual(TEXT("t=1: X changes in 19 s"), At(1.0).SecondsToChangeX, 19.0f, 0.001f);
	TestEqual(TEXT("t=16: X walkers clear (flashing)"), At(16.0).WalkAlongX, ERammsWalkSignal::FlashingDontWalk);
	TestEqual(TEXT("t=21: X amber"), At(21.0).LightX, ERammsSignalLight::Amber);
	TestEqual(TEXT("t=21: X walkers stopped"), At(21.0).WalkAlongX, ERammsWalkSignal::DontWalk);
	TestEqual(TEXT("t=24: all red (X)"), At(24.0).LightX, ERammsSignalLight::Red);
	TestEqual(TEXT("t=24: all red (Y)"), At(24.0).LightY, ERammsSignalLight::Red);
	TestEqual(TEXT("t=26: Y green"), At(26.0).LightY, ERammsSignalLight::Green);
	TestEqual(TEXT("t=26: walking along Y: walk"), At(26.0).WalkAlongY, ERammsWalkSignal::Walk);
	TestEqual(TEXT("t=41: Y amber"), At(41.0).LightY, ERammsSignalLight::Amber);
	TestEqual(TEXT("t=46: wrapped round: X green again"), At(46.0).LightX, ERammsSignalLight::Green);
	TestEqual(TEXT("negative time wraps"), At(-44.0).LightX, ERammsSignalLight::Green);

	// Over two cycles: never both green (or one green, one amber), amber only right after green, walk only on green.
	bool bConflict = false, bWalkOffGreen = false, bAmberAfterGreen = true;
	ERammsSignalLight PrevX = At(0.0).LightX;
	for (double T = 0.0; T < 2.0 * Timing.GetCycleSeconds(); T += 0.05)
	{
		const FRammsSignalState S = At(T);
		bConflict |= S.LightX != ERammsSignalLight::Red && S.LightY != ERammsSignalLight::Red;
		bWalkOffGreen |= (S.WalkAlongX != ERammsWalkSignal::DontWalk && S.LightX != ERammsSignalLight::Green)
			|| (S.WalkAlongY != ERammsWalkSignal::DontWalk && S.LightY != ERammsSignalLight::Green);
		if (S.LightX == ERammsSignalLight::Amber && PrevX != ERammsSignalLight::Amber)
		{
			bAmberAfterGreen &= PrevX == ERammsSignalLight::Green;
		}
		PrevX = S.LightX;
	}
	TestFalse(TEXT("the two axes never move at once"), bConflict);
	TestFalse(TEXT("pedestrians only cross with their parallel green"), bWalkOffGreen);
	TestTrue(TEXT("amber follows green"), bAmberAfterGreen);

	TestEqual(TEXT("offset shifts the cycle"), FRammsSignalCycle::Evaluate([&Timing]() { FRammsSignalTiming T = Timing; T.OffsetSeconds = 25.0f; return T; }(), 1.0).LightY, ERammsSignalLight::Green);

	// The resting material values are the old static look: X traffic green, Y red; the pedestrian heads opposite.
	TMap<FName, float> Defaults;
	for (const TPair<FName, float>& Pair : RammsTraffic::GetSignalParameterDefaults())
	{
		Defaults.Add(Pair.Key, Pair.Value);
	}
	TestEqual(TEXT("defaults: ten parameters"), Defaults.Num(), 10);
	TestEqual(TEXT("defaults: GreenX on"), Defaults.FindRef(FName(TEXT("GreenX"))), 1.0f);
	TestEqual(TEXT("defaults: RedY on"), Defaults.FindRef(FName(TEXT("RedY"))), 1.0f);
	TestEqual(TEXT("defaults: HandX on"), Defaults.FindRef(FName(TEXT("HandX"))), 1.0f);
	TestEqual(TEXT("defaults: WalkY on"), Defaults.FindRef(FName(TEXT("WalkY"))), 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
