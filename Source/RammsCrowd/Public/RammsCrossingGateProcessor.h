// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "ZoneGraphTypes.h"
#include "RammsCrossingGateProcessor.generated.h"

struct FRammsCrossingWaitFragment;
struct FRammsStreetCrossing;

/**
 * Pedestrians wait at the curb while their street crossing is closed.
 *
 * A crossing is a ZoneGraph lane with MassCrowd's CrowdTag and CrossingTag; its state is MassCrowd's
 * (UMassCrowdSubsystem::GetLaneState), which ARammsTrafficSignalClock opens for Walk and closes otherwise (MassCrowd
 * itself leaves the state Opened: without a clock nobody is held for a signal). A crowd member whose lane is a closed
 * crossing and who is still within HoldLimit of its start (on the sidewalk, at the lane's entry) is held: its move
 * target's desired speed is zeroed, so ZoneGraph path following stops advancing and steering brings it to a stand,
 * and the walking speed it had is given back when the crossing opens. Someone already further along keeps walking
 * and clears the road.
 *
 * Walkers are also held, whatever the signal, at the start of a crossing a traffic vehicle is over or about to
 * cross (URammsTrafficSubsystem::FindCrossingHold): a car clearing the intersection after the lights changed, a
 * turning car on its way through, a car on a mid-block crossing. Cars still stop for anyone already crossing,
 * and a car yielding to walkers holds just the ones who step up after it began to (FRammsCrossingWaitFragment
 * keeps when each came onto its lane), while whoever already waits for a car keeps waiting as long as any car
 * holds the crossing: who was first goes first. Cars stop for a held walker only where it physically stands in
 * their path, and on a street crossing nobody is held beyond the edge of the traffic's lanes (HoldLimit).
 *
 * It also warns (once) when crowd members walk on the traffic's vehicle lanes.
 *
 * Runs after the StateTree tasks (which may request a new path and speed) and before path following. Levels
 * without crossing-tagged lanes or traffic are untouched.
 */
UCLASS()
class RAMMSCROWD_API URammsCrossingGateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrossingGateProcessor();

	/** How far along a closed crossing a pedestrian may be and still be held at its start, cm. */
	static constexpr float HoldDistanceCm = 60.0f;
	/** A walker at a crossing's start is always held within this, cm (a crossing whose lanes begin at once). */
	static constexpr float MinHoldDistanceCm = 15.0f;

	/** How far along its crossing a walker of Radius may be and still be held: HoldDistanceCm, and on a street
	 *  crossing short of the traffic's first lane edge by its radius (and 10 cm), so a held walker never stands in a
	 *  lane; at least MinHoldDistanceCm. */
	static float HoldLimit(const FRammsStreetCrossing* Crossing, float Radius);

	/**
	 * One walker's gate, this frame (pure: the Ramms.Crowd.Traffic.Gate test runs it). Keeps Wait's lane and the time
	 * it came onto it (Now), and holds the walker when it is within HoldLimit of its lane's start and the lane is a
	 * closed crossing (bSignalClosed), or a car holds the crossing (CarHoldSince: null for none) for walkers who came
	 * onto it at or after that time, or for one already held for a car. Held: InOutDesiredSpeed becomes 0 (the speed
	 * asked for is kept in Wait); let go: the kept speed is given back. Returns whether it is held.
	 */
	static bool UpdateWait(FRammsCrossingWaitFragment& Wait, const FZoneGraphLaneHandle& Lane, float DistanceAlongLane, float Limit,
		bool bSignalClosed, const float* CarHoldSince, float Now, float& InOutDesiredSpeed);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
