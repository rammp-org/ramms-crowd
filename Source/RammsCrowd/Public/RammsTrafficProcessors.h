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
 *     the crowd's pedestrians (now and, walking ones, PredictSeconds ahead, on a hash grid; not those waiting at
 *     the kerb: held at a crossing's start, standing at the start of a street crossing or on any other lane) and
 *     the robot footprints (the player avatar entities URammsRobotClearanceProcessor uses, or the player pawns'
 *     bounds when there is no avatar entity).
 *  2. Moves each vehicle along its lanes: acceleration is the lowest of the IDM free-road term, IDM behind the
 *     leader (on its lane, then its next two lanes), the turn speed, the stop line when it may not enter the
 *     intersection ahead, and IDM behind the nearest pedestrian or robot in its corridor (its path ahead, half
 *     width + CorridorMargin either side). It never moves past a stop it has to make.
 *
 * Intersection rules (see FRammsTrafficDrivingParams):
 *  - signalized (lanes tagged Signalized, a signal clock in the world): green goes, amber goes unless it can
 *    stop comfortably, red stops unless it can no longer stop. Straight and right on green are protected; a left
 *    turn is permitted on green but waits for oncoming straight and right-turning traffic within
 *    LeftTurnGapSeconds of its line (or let in already), never for another left turn or for one standing at its
 *    line held by something else. No turns on red.
 *  - otherwise (all-way stop): stop at the line for AllWayStopSeconds, then go in arrival order among vehicles
 *    whose lanes conflict.
 *  - always: nobody enters while a vehicle on a conflicting lane (crossing, merging or passing too close) is in
 *    the intersection or has been let in, nor when its exit has no room for it beyond a slow queue there (less the
 *    cars already let in to that exit). A vehicle held at its line by other vehicles' rules for MaxWaitSeconds in
 *    all (time at a red light does not count) enters once no conflicting vehicle is physically in the
 *    intersection.
 *
 * Height and pitch come from the lanes (traced onto the generated road when they were built), refined by two
 * ground traces (front and rear axle) near the player, so cars follow grades, crowns and humps.
 *
 * Entry decisions are made per intersection in one pass before anyone moves: protected movements first, then left
 * turns, so a left turn sees the oncoming vehicles let in this frame. A permitted left that waited at the line for
 * oncoming traffic goes on the amber (and the all red after it). Claims are only taken within a decision distance of
 * the line. A car that has just turned off a shared entry stays the leader of the car behind it until their lanes
 * have separated. Walkers are held at the start of any crossing a car is over or about to cross
 * (URammsTrafficSubsystem::SetHeldCrossings), so a car clearing the intersection is not trapped by the next Walk.
 *
 * Cars and walkers at a crossing: who was first goes first. A car driving on holds everyone at the kerb (within
 * CrossingHoldSeconds, and at least its comfortable stopping distance); walkers already crossing are stopped for.
 * A car yielding to nothing but walkers holds those who step up after it began to (and the gate keeps waiting
 * anyone already waiting for a car), so it goes once the ones already crossing are across; a car stopped for
 * anything else holds nobody but those it stands over. Cars never wait for a walker held at the kerb. A car that has stood MaxWaitSeconds for a walker who does not move drives on past
 * standing walkers (a warning names the walker).
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

	/** ramms.Traffic.Dump requests served so far, and when the last dump was (ramms.Traffic.DumpInterval). */
	int32 SeenDumpRequests = 0;
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
