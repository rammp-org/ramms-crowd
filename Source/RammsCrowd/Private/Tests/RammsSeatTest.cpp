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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsSeatLimbRestTest, "Ramms.Crowd.Seat.LimbRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsSeatLimbRestTest::RunTest(const FString& Parameters)
{
	// A right arm hanging from a shoulder at (0, 18, 75): 30 cm upper arm, 26 cm forearm.
	const FTransform UpperStart(FQuat(FVector::RightVector, 0.2), FVector(0.0, 18.0, 75.0));
	const FTransform LowerStart(FVector(0.0, 18.0, 45.0));
	const FTransform EndStart(FVector(0.0, 18.0, 19.0));
	const FVector	 Forward = FVector::ForwardVector, Right = FVector::RightVector, Up = FVector::UpVector;
	const FVector	 Lap(25.0, 0.0, 38.0);

	{
		// An armrest line at y 28, z 47 (rest point x -4): within reach, so the elbow sits on it.
		FTransform	  Upper = UpperStart, Lower = LowerStart, End = EndStart;
		const FVector Rest(-4.0, 28.0, 47.0);
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, Rest, 12.0f, Lap, Forward, Right, Up, false);
		const FVector Elbow = Lower.GetLocation(), Hand = End.GetLocation();
		TestEqual(TEXT("upper length kept"), (Elbow - Upper.GetLocation()).Size(), 30.0, 0.1);
		TestEqual(TEXT("lower length kept"), (Hand - Elbow).Size(), 26.0, 0.1);
		TestEqual(TEXT("elbow at the rest's height"), Elbow.Z, 47.0, 0.1);
		TestEqual(TEXT("elbow on the rest's line"), Elbow.Y, 28.0, 0.1);
		TestTrue(TEXT("elbow slides no further than allowed"), FMath::Abs(Elbow.X - Rest.X) <= 12.0 + 0.1);
		TestEqual(TEXT("hand at the lap's height"), Hand.Z, Lap.Z, 0.1);
		const FVector2D Heading = FVector2D(Hand - Elbow).GetSafeNormal(), Wanted = FVector2D(Lap - Elbow).GetSafeNormal();
		TestTrue(TEXT("forearm points at the lap"), (Heading | Wanted) > 0.999);
	}
	{
		// The armrest well below the shoulder's reach: the elbow goes as near it as the arm reaches.
		FTransform	  Upper = UpperStart, Lower = LowerStart, End = EndStart;
		const FVector Rest(-4.0, 28.0, 30.0);
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, Rest, 12.0f, Lap, Forward, Right, Up, false);
		const FVector Elbow = Lower.GetLocation();
		TestEqual(TEXT("short arm: length kept"), (Elbow - Upper.GetLocation()).Size(), 30.0, 0.1);
		TestTrue(TEXT("short arm: elbow above the rest, toward it"), Elbow.Z > Rest.Z && ((Elbow - Upper.GetLocation()).GetSafeNormal() | (Rest - Upper.GetLocation()).GetSafeNormal()) > 0.999);
	}
	{
		// The shoulder 2 cm too high for the armrest: a clavicle from the neck at (0, 2, 77) swings just enough to drop it.
		const FVector Neck(0.0, 2.0, 77.0), Shoulder(0.0, 18.0, 75.0), Rest(-4.0, 28.0, 47.0);
		const float	  Swing = URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 28.0, Rest, Forward, 20.0f);
		const FVector Swung = Neck + FQuat(Forward, FMath::DegreesToRadians(Swing)).RotateVector(Shoulder - Neck);
		const FVector FromRest = Swung - Rest;
		TestTrue(TEXT("swing drops the shoulder"), Swung.Z < Shoulder.Z);
		TestEqual(TEXT("swing brings the rest just within reach"), (FromRest - Forward * (FromRest | Forward)).Size(), 27.5, 0.05);
		TestEqual(TEXT("in reach already: no swing"), URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 40.0, Rest, Forward, 20.0f), 0.0f);
		TestEqual(TEXT("out of reach even swung: the most allowed"), FMath::Abs(URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 10.0, Rest, Forward, 20.0f)), 20.0f);
	}
	{
		// Weight 0 leaves the chain as it was.
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, FVector(-4.0, 28.0, 47.0), 12.0f, Lap, Forward, Right, Up, false, 0.0f);
		TestTrue(TEXT("weight 0: elbow unmoved"), Lower.GetLocation().Equals(LowerStart.GetLocation(), 0.01));
		TestTrue(TEXT("weight 0: hand unmoved"), End.GetLocation().Equals(EndStart.GetLocation(), 0.01));
	}
	return true;
}

#endif
