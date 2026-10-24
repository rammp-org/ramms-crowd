// Copyright (c) RAMMP. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RammsRobotClearanceProcessor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsRobotClearanceTest, "Ramms.Crowd.RobotClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsRobotClearanceTest::RunTest(const FString& Parameters)
{
	// A wheelchair-like pill: axis 60 cm long along X, clearance 100 cm, approach band 80 cm.
	const FVector2D A(-30.0, 0.0);
	const FVector2D B(30.0, 0.0);
	const double Clearance = 100.0;
	const double Band = 80.0;

	{
		FVector2D Position(0.0, 250.0);
		FVector2D Velocity(0.0, -140.0);
		TestFalse(TEXT("beyond the band: untouched"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
		TestTrue(TEXT("beyond the band: velocity unchanged"), Velocity.Equals(FVector2D(0.0, -140.0), 0.01));
	}
	{
		FVector2D Position(10.0, 40.0);
		FVector2D Velocity(0.0, -140.0);
		TestTrue(TEXT("inside clearance: resolved"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
		TestEqual(TEXT("inside clearance: moved to the boundary"), Position.Y, Clearance, 0.01);
		TestEqual(TEXT("inside clearance: stays beside the same point of the axis"), Position.X, 10.0, 0.01);
		TestTrue(TEXT("inside clearance: no velocity into the robot"), Velocity.Y >= -0.01);
		TestEqual(TEXT("inside clearance: walking speed kept"), Velocity.Size(), 140.0, 0.01);
	}
	{
		FVector2D Position(80.0, 0.0);
		FVector2D Velocity(-140.0, 10.0);
		TestTrue(TEXT("past the end cap: resolved"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
		TestEqual(TEXT("past the end cap: pushed out from the cap"), FVector2D::Distance(Position, B), Clearance, 0.01);
	}
	{
		FVector2D Position(0.0, 150.0);
		FVector2D Velocity(20.0, -140.0);
		const FVector2D Before = Velocity;
		TestTrue(TEXT("in the band, approaching: redirected"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
		TestTrue(TEXT("in the band, approaching: less inward than before"), Velocity.Y > Before.Y);
		TestTrue(TEXT("in the band, approaching: turned the way it was already drifting"), Velocity.X > Before.X);
		TestTrue(TEXT("in the band: position unchanged"), Position.Equals(FVector2D(0.0, 150.0), 0.01));
	}
	{
		FVector2D Position(0.0, 150.0);
		FVector2D Velocity(0.0, 140.0);
		TestFalse(TEXT("in the band, leaving: untouched"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
	}
	{
		FVector2D Position(0.0, 0.0);
		FVector2D Velocity(0.0, 0.0);
		TestTrue(TEXT("on the axis: resolved"), URammsRobotClearanceProcessor::ResolveAgainstFootprint(A, B, Clearance, Band, Position, Velocity));
		TestEqual(TEXT("on the axis: pushed sideways to the boundary"), Position.Size(), Clearance, 0.01);
	}
	{
		// A circle footprint (both ends equal).
		FVector2D Position(30.0, 40.0);
		FVector2D Velocity(0.0, 0.0);
		URammsRobotClearanceProcessor::ResolveAgainstFootprint(FVector2D::ZeroVector, FVector2D::ZeroVector, Clearance, Band, Position, Velocity);
		TestTrue(TEXT("circle: pushed out radially"), Position.Equals(FVector2D(60.0, 80.0), 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsRobotPassPlanningTest, "Ramms.Crowd.PassPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsRobotPassPlanningTest::RunTest(const FString& Parameters)
{
	// Parked pill along X, clearance 100 cm; planned passes keep 135 cm (head-on within 47 cm). Agents walk +X at 140 cm/s,
	// so their right is +Y.
	const FVector2D A(-30.0, 0.0);
	const FVector2D B(30.0, 0.0);
	const FVector2D Parked(0.0, 0.0);
	const double Clearance = 100.0;
	const FRammsPassingSettings Settings;
	const double Walk = 140.0;

	auto Plan = [&](const FVector2D& Position, FVector2D& Velocity, int8& Side, const FVector2D& RobotVelocity = FVector2D::ZeroVector)
	{
		return URammsRobotClearanceProcessor::PlanPass(A, B, RobotVelocity, Clearance, Settings, Walk, 1.0 / 60.0, Position, Velocity, Side);
	};

	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		TestTrue(TEXT("head-on: plans a pass"), Plan(FVector2D(-400.0, 0.0), Velocity, Side));
		TestEqual(TEXT("head-on: keeps right"), static_cast<int32>(Side), 1);
		TestTrue(TEXT("head-on: turns right"), Velocity.Y > 1.0);
		TestTrue(TEXT("head-on: does not speed up"), Velocity.Size() <= Walk + 1.0e-3);
		const double TurnDegrees = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
		TestTrue(TEXT("head-on: turns no faster than the limit"), TurnDegrees <= Settings.MaxTurnDegreesPerSecond / 60.0 + 1.0e-6);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		Plan(FVector2D(-400.0, -100.0), Velocity, Side);
		TestEqual(TEXT("already well to the left: stays left"), static_cast<int32>(Side), -1);
		TestTrue(TEXT("already well to the left: opens to the left"), Velocity.Y < -1.0);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		Plan(FVector2D(-150.0, -20.0), Velocity, Side);
		TestEqual(TEXT("head-on but too late to cross: stays on its side"), static_cast<int32>(Side), -1);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = -1;
		Plan(FVector2D(-400.0, 0.0), Velocity, Side);
		TestEqual(TEXT("a committed side is kept"), static_cast<int32>(Side), -1);
		TestTrue(TEXT("a committed side is kept: turns left"), Velocity.Y < -1.0);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 1;
		TestFalse(TEXT("moving away: untouched"), Plan(FVector2D(400.0, 0.0), Velocity, Side));
		TestEqual(TEXT("moving away: pass over"), static_cast<int32>(Side), 0);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		TestFalse(TEXT("beyond the lookahead: untouched"), Plan(FVector2D(-1000.0, 0.0), Velocity, Side));
		TestEqual(TEXT("beyond the lookahead: nothing planned"), static_cast<int32>(Side), 0);
	}
	{
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		TestFalse(TEXT("wide miss: untouched"), Plan(FVector2D(-400.0, -300.0), Velocity, Side));
		TestEqual(TEXT("wide miss: nothing planned"), static_cast<int32>(Side), 0);
	}
	{
		// 50 cm outside the clearance and closing: speed eases to 70% + 30% * 50/120 of walking speed.
		FVector2D Velocity(Walk, 0.0);
		int8 Side = 0;
		Plan(FVector2D(-20.0, -150.0), Velocity, Side);
		TestEqual(TEXT("alongside: slows down"), Velocity.Size(), Walk * (0.7 + 0.3 * 50.0 / 120.0), 1.0e-3);
		FVector2D Again = Velocity;
		Plan(FVector2D(-20.0, -150.0), Again, Side);
		TestTrue(TEXT("slowdown does not compound"), Again.Size() >= Velocity.Size() - 1.0e-3);
	}
	{
		// A robot driving at a standing-still agent: the pass is planned in the robot's frame.
		FVector2D Velocity(10.0, 0.0);
		int8 Side = 0;
		Plan(FVector2D(-400.0, 0.0), Velocity, Side, FVector2D(-150.0, 0.0));
		TestEqual(TEXT("robot approaching head-on: agent keeps right"), static_cast<int32>(Side), 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
