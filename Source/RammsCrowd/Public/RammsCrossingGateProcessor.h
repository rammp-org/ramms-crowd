// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsCrossingGateProcessor.generated.h"

/**
 * Pedestrians wait at the curb while their street crossing is closed.
 *
 * A crossing is a ZoneGraph lane with MassCrowd's CrowdTag and CrossingTag; its state is MassCrowd's
 * (UMassCrowdSubsystem::GetLaneState), which ARammsTrafficSignalClock opens for Walk and closes otherwise. A
 * crowd member whose lane is a closed crossing and who is still within HoldDistanceCm of its start (on the
 * sidewalk, at the lane's entry) is held: its move target's desired speed is zeroed, so ZoneGraph path
 * following stops advancing and steering brings it to a stand, and the walking speed it had is given back when
 * the crossing opens. Someone already further along keeps walking and clears the road.
 *
 * Walkers are also held, whatever the signal, at the start of a crossing a traffic vehicle is over or about to
 * cross (URammsTrafficSubsystem::IsCrossingHeld): a car clearing the intersection after the lights changed, a
 * turning car on its way through, a car on a mid-block crossing. Cars still stop for anyone already crossing.
 *
 * Runs after the StateTree tasks (which may request a new path and speed) and before path following. Levels
 * without crossing-tagged lanes are untouched.
 */
UCLASS()
class RAMMSCROWD_API URammsCrossingGateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrossingGateProcessor();

	/** How far along a closed crossing a pedestrian may be and still be held at its start, cm. */
	static constexpr float HoldDistanceCm = 60.0f;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
