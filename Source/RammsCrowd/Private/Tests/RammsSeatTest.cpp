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
	const FVector	 Shoulder = UpperStart.GetLocation();

	// The arm rested on a rest point with 12 cm of slide, its hand headed for Effector: the elbow and the hand.
	auto SolveOnRest = [&](const FVector& At, const FVector& Effector, FVector& OutElbow, FVector& OutHand) {
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, At, 12.0f, Effector, Forward, Right, Up, false);
		OutElbow = Lower.GetLocation();
		OutHand = End.GetLocation();
	};

	{
		// An armrest line at y 28, z 47 (rest point x -4): within reach, so the elbow sits on it -- of the two places
		// on it at the upper arm's length (x -4 and x 4), the one ahead of the shoulder.
		FVector		  Elbow, Hand;
		const FVector Rest(-4.0, 28.0, 47.0);
		SolveOnRest(Rest, Lap, Elbow, Hand);
		TestEqual(TEXT("upper length kept"), (Elbow - Shoulder).Size(), 30.0, 0.1);
		TestEqual(TEXT("lower length kept"), (Hand - Elbow).Size(), 26.0, 0.1);
		TestEqual(TEXT("elbow at the rest's height"), Elbow.Z, 47.0, 0.1);
		TestEqual(TEXT("elbow on the rest's line"), Elbow.Y, 28.0, 0.1);
		TestTrue(TEXT("elbow slides no further than allowed"), FMath::Abs(Elbow.X - Rest.X) <= 12.0 + 0.1);
		TestEqual(TEXT("elbow ahead of the shoulder"), Elbow.X, 4.0, 0.1);
		TestEqual(TEXT("hand at the lap's height"), Hand.Z, Lap.Z, 0.1);
		const FVector2D Heading = FVector2D(Hand - Elbow).GetSafeNormal(), Wanted = FVector2D(Lap - Elbow).GetSafeNormal();
		TestTrue(TEXT("forearm points at the lap"), (Heading | Wanted) > 0.999);
	}
	{
		// The armrest well below the shoulder's reach: the elbow goes toward the nearest point of the stretch it may
		// slide along (under the shoulder, 4 cm ahead of the rest point), as far as the arm reaches.
		FVector		  Elbow, Hand;
		const FVector Rest(-4.0, 28.0, 30.0), Nearest(0.0, 28.0, 30.0);
		SolveOnRest(Rest, Lap, Elbow, Hand);
		TestEqual(TEXT("short arm: length kept"), (Elbow - Shoulder).Size(), 30.0, 0.1);
		TestTrue(TEXT("short arm: elbow above the rest, toward the nearest point of its stretch"),
			Elbow.Z > Rest.Z && ((Elbow - Shoulder).GetSafeNormal() | (Nearest - Shoulder).GetSafeNormal()) > 0.9999);
	}
	{
		// The rest point 20 cm ahead of the shoulder: the arm cannot reach the stretch it may slide along (back to
		// x 8), so the elbow stops just short of that end, just above the rest.
		FVector		  Elbow, Hand;
		const FVector Rest(20.0, 28.0, 47.0), SlideEnd(8.0, 28.0, 47.0);
		SolveOnRest(Rest, Lap, Elbow, Hand);
		TestEqual(TEXT("slide end out of reach: length kept"), (Elbow - Shoulder).Size(), 30.0, 0.1);
		TestTrue(TEXT("slide end out of reach: elbow toward that end, just above the rest"),
			Elbow.Z > Rest.Z && Elbow.Z < Rest.Z + 1.0 && ((Elbow - Shoulder).GetSafeNormal() | (SlideEnd - Shoulder).GetSafeNormal()) > 0.9999);
	}
	{
		// The rest 20 cm below the shoulder: the upper arm reaches beyond the whole stretch, so the elbow stays on the
		// rest's line at its height, a little past the stretch's front end, rather than pushed down into the rest.
		FVector		  Elbow, Hand;
		const FVector Rest(-4.0, 23.0, 55.0);
		SolveOnRest(Rest, Lap, Elbow, Hand);
		TestEqual(TEXT("overreach: length kept"), (Elbow - Shoulder).Size(), 30.0, 0.1);
		TestEqual(TEXT("overreach: elbow at the rest's height"), Elbow.Z, 55.0, 0.1);
		TestEqual(TEXT("overreach: elbow on the rest's line"), Elbow.Y, 23.0, 0.1);
		TestTrue(TEXT("overreach: elbow past the stretch's front end"), Elbow.X > Rest.X + 12.0);
	}
	{
		// The shoulder right over the rest point, a hair behind it and a hair ahead: the same elbow, ahead of the
		// shoulder (choosing the place nearer the rest point flipped it 8 cm here).
		FVector ElbowBehind, ElbowAhead, Hand;
		SolveOnRest(FVector(0.01, 28.0, 47.0), Lap, ElbowBehind, Hand);
		SolveOnRest(FVector(-0.01, 28.0, 47.0), Lap, ElbowAhead, Hand);
		TestTrue(TEXT("no flip over the rest point"), ElbowBehind.Equals(ElbowAhead, 0.05));
		TestTrue(TEXT("no flip: elbow ahead of the shoulder"), ElbowBehind.X > Shoulder.X + 1.0);
	}
	{
		// The rest's line at the very edge of the upper arm's reach, a hair inside and a hair outside: nearly the same
		// elbow (it jumped about 4 cm here, from the line to straight at the rest point).
		const double EdgeZ = 75.0 - FMath::Sqrt(30.0 * 30.0 - 10.0 * 10.0);
		FVector		 ElbowInside, ElbowOutside, Hand;
		SolveOnRest(FVector(-4.0, 28.0, EdgeZ + 0.001), Lap, ElbowInside, Hand);
		SolveOnRest(FVector(-4.0, 28.0, EdgeZ - 0.001), Lap, ElbowOutside, Hand);
		TestTrue(TEXT("continuous at the edge of reach"), (ElbowInside - ElbowOutside).Size() < 0.5);
	}
	{
		// The lap a hair less and a hair more than a forearm below the elbow: nearly the same hand (it jumped from
		// under the elbow to pointing at the lap here). Further down, the forearm hangs straight down.
		FVector		  Elbow, HandInReach, HandOutOfReach;
		const FVector Rest(-4.0, 28.0, 47.0);
		SolveOnRest(Rest, FVector(25.0, 0.0, 47.0 - 26.0 + 0.001), Elbow, HandInReach);
		SolveOnRest(Rest, FVector(25.0, 0.0, 47.0 - 26.0 - 0.001), Elbow, HandOutOfReach);
		TestTrue(TEXT("forearm continuous where the lap is a forearm below"), (HandInReach - HandOutOfReach).Size() < 0.5);
		TestTrue(TEXT("lap out of reach below: forearm hangs straight down"), (HandOutOfReach - (Elbow - Up * 26.0)).Size() < 0.01);
	}
	{
		// The shoulder 2 cm too high for the armrest: a clavicle from the neck at (0, 2, 77) swings just enough to drop it.
		const FVector Neck(0.0, 2.0, 77.0), Rest(-4.0, 28.0, 47.0);
		const float	  Swing = URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 28.0, Rest, 12.0f, Forward, 20.0f);
		const FVector Swung = Neck + FQuat(Forward, FMath::DegreesToRadians(Swing)).RotateVector(Shoulder - Neck);
		const FVector FromRest = Swung - Rest;
		TestTrue(TEXT("swing drops the shoulder"), Swung.Z < Shoulder.Z);
		TestEqual(TEXT("swing brings the rest just within reach"), (FromRest - Forward * (FromRest | Forward)).Size(), 27.5, 0.05);
		TestEqual(TEXT("in reach already: no swing"), URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 40.0, Rest, 12.0f, Forward, 20.0f), 0.0f);
		TestEqual(TEXT("out of reach even swung: the most allowed"),
			FMath::Abs(URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 10.0, Rest, 12.0f, Forward, 20.0f)), 20.0f);
		// A limb only partly placed swings its parent that share of the way (no jump as its weight comes up from 0).
		TestEqual(TEXT("half weight: half the swing"),
			URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 28.0, Rest, 12.0f, Forward, 20.0f, 0.5f, 0.5f), 0.5f * Swing, 1.0e-4f);
		TestEqual(TEXT("weight 0: no swing"), URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 28.0, Rest, 12.0f, Forward, 20.0f, 0.5f, 0.0f), 0.0f);
	}
	{
		// The rest point 14 cm ahead of the shoulder with 4 cm of slide: the rest's line is in reach, the stretch the
		// elbow may slide along is not, so the clavicle swings until its near end is.
		const FVector Neck(0.0, 2.0, 77.0), Rest(14.0, 28.0, 47.0);
		const float	  Swing = URammsSeatedPoseAnimInstance::ParentSwingToRest(Neck, Shoulder, 31.0, Rest, 4.0f, Forward, 20.0f);
		const FVector Swung = Neck + FQuat(Forward, FMath::DegreesToRadians(Swing)).RotateVector(Shoulder - Neck);
		TestTrue(TEXT("stretch out of reach: the shoulder drops"), FMath::Abs(Swing) > 0.1f && Swung.Z < Shoulder.Z);
		TestEqual(TEXT("stretch out of reach: its near end just within reach"), (Swung - (Rest - Forward * 4.0)).Size(), 31.0, 0.05);
	}
	{
		// Weight 0 leaves the chain as it was; half lands between, the bone lengths kept.
		FTransform Upper = UpperStart, Lower = LowerStart, End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, FVector(-4.0, 28.0, 47.0), 12.0f, Lap, Forward, Right, Up, false, 0.0f);
		TestTrue(TEXT("weight 0: elbow unmoved"), Lower.GetLocation().Equals(LowerStart.GetLocation(), 0.01));
		TestTrue(TEXT("weight 0: hand unmoved"), End.GetLocation().Equals(EndStart.GetLocation(), 0.01));
		Upper = UpperStart;
		Lower = LowerStart;
		End = EndStart;
		URammsSeatedPoseAnimInstance::SolveLimbOnRest(Upper, Lower, End, FVector(-4.0, 28.0, 47.0), 12.0f, Lap, Forward, Right, Up, false, 0.5f);
		TestEqual(TEXT("half weight: upper length kept"), (Lower.GetLocation() - Shoulder).Size(), 30.0, 0.1);
		TestEqual(TEXT("half weight: lower length kept"), (End.GetLocation() - Lower.GetLocation()).Size(), 26.0, 0.1);
	}
	return true;
}

#endif
