// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsTrafficProcessors.generated.h"

/**
 * Drives the traffic, once per frame on the game thread (tens of vehicles: a few hundred microseconds):
 *
 *  1. Gathers the frame's state: a per-lane occupancy index (vehicles sorted by distance), each controlled
 *     intersection's claims (vehicles in it, or let in), arrivals at all-way stops and vehicles approaching it;
 *     the crowd's pedestrians (now and PredictSeconds ahead, on a hash grid) and the robot footprints (the
 *     player avatar entities URammsRobotClearanceProcessor uses, or the player pawns' bounds when there is no
 *     avatar entity).
 *  2. Moves each vehicle along its lanes: acceleration is the lowest of the IDM free-road term, IDM behind the
 *     leader (on its lane, then its next two lanes), the turn speed, the stop line when it may not enter the
 *     intersection ahead, and IDM behind the nearest pedestrian or robot in its corridor (its path ahead, half
 *     width + CorridorMargin either side). It never moves past a stop it has to make.
 *
 * Intersection rules (see FRammsTrafficDrivingParams):
 *  - signalized (lanes tagged Signalized, a signal clock in the world): green goes, amber goes unless it can
 *    stop comfortably, red stops unless it can no longer stop. Straight and right on green are protected; a left
 *    turn is permitted on green but waits for oncoming straight and right-turning traffic within
 *    LeftTurnGapSeconds of its line. No turns on red.
 *  - otherwise (all-way stop): stop at the line for AllWayStopSeconds, then go in arrival order among vehicles
 *    whose lanes conflict.
 *  - always: nobody enters while a vehicle on a conflicting lane (crossing, merging or passing too close) is in
 *    the intersection or has been let in. A vehicle held only by other vehicles for MaxWaitSeconds goes anyway.
 *
 * Height and pitch come from the lanes (traced onto the generated road when they were built), refined by two
 * ground traces (front and rear axle) near the player, so cars follow grades, crowns and humps.
 *
 * ramms.Traffic.Debug 1 draws each vehicle's speed and what it is yielding to.
 */
UCLASS()
class RAMMSCROWD_API URammsTrafficVehicleProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsTrafficVehicleProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery VehicleQuery;
	FMassEntityQuery AvatarQuery;
	FMassEntityQuery CrowdQuery;
};

/**
 * Moves each vehicle's representation actor (ARammsTrafficVehicleActor) to its Mass transform, kinematically,
 * and switches its collision on near the player and off far from it.
 */
UCLASS()
class RAMMSCROWD_API URammsTrafficVisualProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsTrafficVisualProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
