// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsRobotClearanceProcessor.generated.h"

/**
 * Keeps crowd members out of robot footprints, after Mass movement has moved them
 * and before their representation actors are synced.
 *
 * Mass avoidance only steers: a pedestrian whose lane runs through a parked
 * wheelchair walks into it, and its representation actor, teleported every frame
 * with animation-driven kinematic bodies, then shoves the robot with unbounded force.
 * This processor makes the robot footprint (the player avatar's pill or circle
 * collider, see URammsPlayerAvatarTrait) a hard boundary for crowd agents:
 *
 *  - inside clearance (footprint + agent radius + margin): the agent is moved out to
 *    the boundary and the part of its velocity that points into the robot is turned
 *    along the boundary, so it slides around the robot rather than stopping dead;
 *  - inside the approach band beyond that: the inward part of its velocity is turned
 *    along the boundary progressively, so it starts curving around early.
 *
 * Before any of that, agents on course to pass close by plan the pass (PlanPass):
 * they pick a side seconds ahead (keeping right when head-on), drift over gradually,
 * and slow down alongside the robot.
 *
 * Velocity changes are written to both the velocity and the desired velocity: Mass
 * movement re-derives velocity from the desired velocity every frame, so a change to
 * the velocity alone would last one frame.
 *
 * A moving robot pushes agents out of its way the same way (they yield). Each push
 * that starts a new contact is counted in the agent's FRammsRobotContactFragment, for
 * the animation layer's bump reaction.
 */
/** How agents plan a pass of a robot footprint (from URammsPlayerAvatarTrait). */
struct FRammsPassingSettings
{
	/** Seconds of closing ahead at which planning starts. */
	double LookaheadSeconds = 3.0;
	/** Room beyond the clearance a planned pass leaves, cm. */
	double ComfortCm = 35.0;
	/** Keep right when head-on. */
	bool bPreferRight = true;
	/** A predicted miss closer to the robot's axis than this share of the planned distance counts as head-on. */
	double HeadOnShare = 0.35;
	/** Closer than this to closest approach, an undecided agent keeps the side it is on rather than crossing over to the right. */
	double MinHeadOnDecisionSeconds = 1.2;
	/** Largest sideways correction, as a share of the agent's speed. */
	double MaxSidewaysShare = 0.6;
	/** Fastest the plan turns an agent, degrees per second (people turn gradually; swerves read as robotic). */
	double MaxTurnDegreesPerSecond = 150.0;
	/** Speed share kept at the clearance while approaching or passing, easing to 1 over SlowdownBandCm. */
	double SlowdownFraction = 0.7;
	double SlowdownBandCm = 120.0;
};

UCLASS()
class RAMMSCROWD_API URammsRobotClearanceProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsRobotClearanceProcessor();

	/**
	 * Resolves one agent (2D position and velocity, cm and cm/s) against one footprint:
	 * the capsule of radius Clearance around segment [SegA, SegB] (equal ends for a
	 * circle). Returns true if the position or velocity changed; bOutContact is set when
	 * the agent was inside the clearance and had to be pushed out (a bump).
	 */
	static bool ResolveAgainstFootprint(const FVector2D& SegA, const FVector2D& SegB, double Clearance, double ApproachBand,
		FVector2D& InOutPosition, FVector2D& InOutVelocity, bool* bOutContact = nullptr);

	/**
	 * Plans an agent's pass of one footprint (the capsule of radius Clearance around
	 * [SegA, SegB], moving at RobotVelocity), the way pedestrians do it: if, at the
	 * current relative velocity, it will come closer than Clearance + Comfort within
	 * the lookahead, it commits to a side (InOutSide: +1 its right, -1 its left; the
	 * right when head-on, else the side it is already on) and turns gradually so the
	 * predicted miss opens to the planned distance by closest approach. While
	 * approaching or alongside it also eases its speed down toward SlowdownFraction
	 * of DesiredSpeed. Turns are limited to MaxTurnDegreesPerSecond over DeltaSeconds.
	 * InOutSide resets to 0 once the agent is clear or moving away. Returns true if the
	 * velocity changed.
	 */
	static bool PlanPass(const FVector2D& SegA, const FVector2D& SegB, const FVector2D& RobotVelocity, double Clearance,
		const FRammsPassingSettings& Settings, double DesiredSpeed, double DeltaSeconds, const FVector2D& Position,
		FVector2D& InOutVelocity, int8& InOutSide);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery AvatarQuery;
	FMassEntityQuery CrowdQuery;
};
