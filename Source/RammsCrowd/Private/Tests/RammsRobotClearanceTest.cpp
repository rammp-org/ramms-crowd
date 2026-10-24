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

#endif // WITH_DEV_AUTOMATION_TESTS
