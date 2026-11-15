// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsTrafficProcessors.generated.h"

/**
 * Drives the traffic, once per frame on the game thread (tens of vehicles: a few hundred microseconds). The rules
 * themselves are RammsTraffic::FTrafficFrame (RammsTrafficRules.h), which the Ramms.Crowd.Traffic tests run over
 * hand-made networks; this processor fills a frame from Mass and the world and runs it:
 *
 *  1. Gathers the frame's state: a per-lane occupancy index (vehicles sorted by distance), each controlled
 *     intersection's claims (vehicles in it, let in, or let in and no longer able to stop: committed), arrivals at
 *     all-way stops and vehicles approaching it; the crowd's pedestrians (now and, walking ones, PredictSeconds
 *     ahead, on a hash grid; walkers waiting at the kerb -- held at a crossing's start, standing off the road part
 *     of a street crossing or on any other lane -- count only where they physically are) and the robot footprints
 *     (the player avatar entities URammsRobotClearanceProcessor uses, or the player pawns' bounds when there is no
 *     avatar entity).
 *  2. Moves each vehicle along its lanes: acceleration is the lowest of the IDM free-road term, IDM behind the
 *     leader (on its lane, then its next two lanes), the turn speed (once braking for it is due), the stop line when
 *     it may not enter the intersection ahead or its lane leads nowhere, and IDM behind the nearest pedestrian or
 *     robot in its way (RammsTraffic::IsInTheWay: in its path, or on the carriageway within half width +
 *     CorridorMargin of it). It never moves past a stop it has to make.
 *
 * Intersection rules (see FRammsTrafficDrivingParams):
 *  - signalized (lanes tagged Signalized, a signal clock in the world): green goes, amber goes unless it can
 *    stop comfortably, red stops unless it can no longer stop. Straight and right on green are protected; a left
 *    turn is permitted on green but waits for oncoming straight and right-turning traffic within
 *    LeftTurnGapSeconds of its line (or let in already), never for another left turn or for one standing at its
 *    line held by something else. No turns on red.
 *  - otherwise (all-way stop): stop at the line for AllWayStopSeconds, then go in arrival order among vehicles
 *    whose lanes conflict.
 *  - always: only the first car at its line is let in (one queued behind a car not let in, or standing, takes no
 *    claim). Nobody enters while a vehicle on a conflicting lane (crossing, merging or passing too close) is in the
 *    intersection or has been let in, nor when its exit has no room for it beyond a slow queue there (less the cars
 *    already let in to that exit). A vehicle let in that can no longer stop keeps going.
 *  - deadlock breaker: a vehicle held at its line by other vehicles' rules for MaxWaitSeconds in all (time at a red
 *    light, in a queue or at the box does not count) stops waiting for vehicles that are only let in; it still waits
 *    for any in the intersection, any let in that can no longer stop, and (a left turn) any oncoming one that can
 *    no longer stop. One held at the box for MaxWaitSeconds enters once its exit has room counting only the cars
 *    already in the intersection; it never stops in the box.
 *
 * Height and pitch come from the lanes (traced onto the generated road when they were built), refined by two
 * ground traces (front and rear axle) near the player, so cars follow grades, crowns and humps.
 *
 * Entry decisions are made per intersection in one pass before anyone moves: protected movements first, then left
 * turns, so a left turn sees the oncoming vehicles let in this frame. A permitted left that waited at the line for
 * oncoming traffic goes on the amber (and the all red after it). Claims are only taken within a decision distance of
 * the line. A car that has just turned off a shared entry stays the leader of the car behind it until their lanes
 * have separated. Walkers are held at the start of any crossing a car is over or about to cross
 * (URammsTrafficSubsystem::SetHeldCrossings, published every frame, empty when there is nothing to drive), so a car
 * clearing the intersection is not trapped by the next Walk. A lane that leads nowhere ends in a stop; a car
 * standing there starts again elsewhere once no player is near (or after MaxWaitSeconds).
 *
 * Cars and walkers at a crossing: who was first goes first. A car driving on holds everyone at the kerb (within
 * CrossingHoldSeconds, and at least its comfortable stopping distance); walkers already crossing are stopped for.
 * A car yielding to nothing but walkers holds those who step up after it began to (and the gate keeps waiting
 * anyone already waiting for a car), so it goes once the ones already crossing are across; a car stopped for
 * anything else holds nobody but those it stands over. One stopped for a walker who stands (held up by the crowd
 * at the far kerb) stops holding newcomers after RammsTraffic::StuckWalkerReleaseSeconds, so the crowd can make
 * way. A car that has stood MaxWaitSeconds for a walker who does not move drives on past that walker only (a
 * warning names the walker).
 *
 * ramms.Traffic.Debug 1 draws each vehicle's speed and what limits it; ramms.Traffic.Dump logs every vehicle (lane,
 * distance, speed, next lane and turn, the limiting term and the vehicle or walker behind it, the entry decision and
 * who it waits for, claim, stop and walker-wait state), every intersection's claims and approaches, the held
 * crossings, and the walkers on street crossings that are held or stand still (entity, position, speed, lane and
 * distance along it, signal and hold state, desired speed and movement action); ramms.Traffic.DumpInterval N
 * repeats the dump every N s.
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

	/** ramms.Traffic.Dump requests served so far (INDEX_NONE until the first frame takes the global count), and when
	 *  the last dump was (ramms.Traffic.DumpInterval). */
	int32 SeenDumpRequests = INDEX_NONE;
	double LastDumpTime = 0.0;
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
