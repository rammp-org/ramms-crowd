// Copyright (c) RAMMP. All rights reserved.

#include "Misc/AutomationTest.h"
#include "RammsCrowdAnimInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsCrowdAnimDirectionTest, "Ramms.Crowd.Anim.DirectionBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsCrowdAnimDirectionTest::RunTest(const FString& Parameters)
{
	// Forward first, then the directionals as the anim set lists them.
	const TArray<float> Directions = { 0.0f, -45.0f, 45.0f, -90.0f, 90.0f, -135.0f, 135.0f, 180.0f };
	int32 A = 0;
	int32 B = 0;
	float AlphaB = 0.0f;

	TestTrue(TEXT("empty set"), !RammsCrowdAnim::BracketDirection({}, 10.0f, A, B, AlphaB));

	RammsCrowdAnim::BracketDirection(Directions, 0.0f, A, B, AlphaB);
	TestEqual(TEXT("straight ahead is the forward walk"), Directions[A == B ? A : (AlphaB < 0.5f ? A : B)], 0.0f);
	TestTrue(TEXT("straight ahead is all forward"), (A == 0 && AlphaB < 1.0e-4f) || (B == 0 && AlphaB > 1.0f - 1.0e-4f));

	RammsCrowdAnim::BracketDirection(Directions, 20.0f, A, B, AlphaB);
	TestEqual(TEXT("20 deg: between forward"), Directions[A], 0.0f);
	TestEqual(TEXT("20 deg: and forward-right"), Directions[B], 45.0f);
	TestEqual(TEXT("20 deg: 4/9 of the way"), AlphaB, 20.0f / 45.0f, 1.0e-4f);

	RammsCrowdAnim::BracketDirection(Directions, -100.0f, A, B, AlphaB);
	TestEqual(TEXT("-100 deg: from back-left"), Directions[A], -135.0f);
	TestEqual(TEXT("-100 deg: to left"), Directions[B], -90.0f);

	RammsCrowdAnim::BracketDirection(Directions, 170.0f, A, B, AlphaB);
	TestEqual(TEXT("170 deg: from back-right"), Directions[A], 135.0f);
	TestEqual(TEXT("170 deg: to back"), Directions[B], 180.0f);
	TestEqual(TEXT("170 deg: weight"), AlphaB, 35.0f / 45.0f, 1.0e-4f);

	RammsCrowdAnim::BracketDirection(Directions, -170.0f, A, B, AlphaB);
	TestEqual(TEXT("-170 deg wraps: from back"), Directions[A], 180.0f);
	TestEqual(TEXT("-170 deg wraps: to back-left"), Directions[B], -135.0f);
	TestEqual(TEXT("-170 deg wraps: weight"), AlphaB, 10.0f / 45.0f, 1.0e-4f);

	const TArray<float> ForwardOnly = { 0.0f };
	RammsCrowdAnim::BracketDirection(ForwardOnly, 120.0f, A, B, AlphaB);
	TestTrue(TEXT("one clip covers everything"), A == 0 && B == 0 && AlphaB == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRammsCrowdAnimGaitTest, "Ramms.Crowd.Anim.GaitPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRammsCrowdAnimGaitTest::RunTest(const FString& Parameters)
{
	// Left foot down at 0.8, 1.9 and 3.0 s in a 3.3 s loop: three cycles, the last wrapping.
	RammsCrowdAnim::FGaitTrack Track;
	const float Markers[] = { 1.9f, 0.8f, 3.0f, 3.3f };
	Track.InitFromMarkers(Markers, 3.3f);
	TestEqual(TEXT("markers at or past the end are dropped"), Track.CycleStarts.Num(), 3);
	TestEqual(TEXT("first cycle starts at the first marker"), Track.TimeAt(0.0f), 0.8f, 1.0e-4f);
	TestEqual(TEXT("first cycle length"), Track.CycleDuration(), 1.1f, 1.0e-4f);
	TestEqual(TEXT("half way through the first cycle"), Track.TimeAt(0.5f), 1.35f, 1.0e-4f);
	Track.NextCycle();
	Track.NextCycle();
	TestEqual(TEXT("last cycle wraps through the loop point"), Track.CycleDuration(), 1.1f, 1.0e-4f);
	TestEqual(TEXT("time past the loop point wraps"), Track.TimeAt(0.5f), 0.25f, 1.0e-4f);
	Track.NextCycle();
	TestEqual(TEXT("cycles come round again"), Track.Cycle, 0);

	RammsCrowdAnim::FGaitTrack NoMarkers;
	NoMarkers.InitFromMarkers({}, 2.0f);
	TestEqual(TEXT("no markers: one cycle"), NoMarkers.CycleDuration(), 2.0f, 1.0e-4f);
	TestEqual(TEXT("no markers: phase is normalized time"), NoMarkers.TimeAt(0.25f), 0.5f, 1.0e-4f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
