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
 * A moving robot pushes agents out of its way the same way (they yield). Each push
 * that starts a new contact is counted in the agent's FRammsRobotContactFragment, for
 * the animation layer's bump reaction.
 */
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

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery AvatarQuery;
	FMassEntityQuery CrowdQuery;
};
