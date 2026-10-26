// Copyright (c) RAMMP. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RammsSeatedPoseAnimInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Peak lean (degrees) and the lean after settling, for a 0.5 s push then rest. */
	void RunSway(const float Acceleration, const float TrunkControl, float& OutPeak, float& OutSettled)
	{
		float Angle = 0.0f;
		float Rate = 0.0f;
		OutPeak = 0.0f;
		const float Dt = 1.0f / 60.0f;
		for (int32 Frame = 0; Frame < 60 * 5; ++Frame)
		{
			const float Accel = Frame < 30 ? Acceleration : 0.0f;
			URammsSeatedPoseAnimInstance::StepSway(Angle, Rate, Accel, TrunkControl, Dt);
			const float Degrees = FMath::RadiansToDegrees(Angle);
			if (FMath::Abs(Degrees) > FMath::Abs(OutPeak))
			{
				OutPeak = Degrees;
			}
		}
		OutSettled = FMath::RadiansToDegrees(Angle);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsSeatSwayTest, "Ramms.Crowd.Seat.PassiveSway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsSeatSwayTest::RunTest(const FString& Parameters)
{
	float Peak = 0.0f;
	float Settled = 0.0f;

	RunSway(300.0f, 0.6f, Peak, Settled);
	TestTrue(TEXT("speeding up leans the trunk back"), Peak < -1.0f);
	TestTrue(TEXT("the lean stays modest"), Peak > -15.0f);
	TestEqual(TEXT("the trunk comes back upright"), Settled, 0.0f, 0.05f);

	RunSway(-300.0f, 0.6f, Peak, Settled);
	TestTrue(TEXT("braking leans the trunk forward"), Peak > 1.0f);

	float WeakPeak = 0.0f;
	float StrongPeak = 0.0f;
	RunSway(300.0f, 0.1f, WeakPeak, Settled);
	RunSway(300.0f, 1.0f, StrongPeak, Settled);
	TestTrue(TEXT("less trunk control sways more"), FMath::Abs(WeakPeak) > FMath::Abs(StrongPeak));

	RunSway(0.0f, 0.6f, Peak, Settled);
	TestEqual(TEXT("no push, no sway"), Peak, 0.0f);

	// A violent jolt is capped.
	RunSway(-50000.0f, 0.0f, Peak, Settled);
	TestTrue(TEXT("lean is capped"), Peak <= 25.0f + 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsSeatLimbTest, "Ramms.Crowd.Seat.LimbSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsSeatLimbTest::RunTest(const FString& Parameters)
{
	// A straight 30 + 30 cm limb along +X, bending toward +Z.
	const FTransform UpperStart(FVector::ZeroVector);
	const FTransform LowerStart(FVector(30.0, 0.0, 0.0));
	const FTransform EndStart(FQuat(FVector::UpVector, 0.3), FVector(60.0, 0.0, 0.0));
	const FVector Pole(30.0, 0.0, 50.0);

	{
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		const FVector Effector(40.0, 0.0, 0.0);
		URammsSeatedPoseAnimInstance::SolveLimb(Upper, Lower, End, Effector, Pole, false);
		TestEqual(TEXT("reaches the target"), (End.GetLocation() - Effector).Size(), 0.0, 0.1);
		TestEqual(TEXT("upper length kept"), (Lower.GetLocation() - Upper.GetLocation()).Size(), 30.0, 0.1);
		TestEqual(TEXT("lower length kept"), (End.GetLocation() - Lower.GetLocation()).Size(), 30.0, 0.1);
		TestTrue(TEXT("bends toward the pole"), Lower.GetLocation().Z > 1.0);
	}
	{
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimb(Upper, Lower, End, FVector(40.0, 0.0, 0.0), Pole, true);
		TestTrue(TEXT("keep-pose leaves the end's rotation"), End.GetRotation().Equals(EndStart.GetRotation(), 1.0e-4));
	}
	{
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimb(Upper, Lower, End, FVector(200.0, 0.0, 0.0), Pole, false);
		TestEqual(TEXT("out of reach: limb straightens, no stretch"), (End.GetLocation() - Upper.GetLocation()).Size(), 60.0, 0.1);
	}
	return true;
}

#endif
