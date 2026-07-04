// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsCrowdStimulusProcessor.generated.h"

/**
 * Crowd perception: for every entity with FRammsStimulusFragment, evaluates the nearest
 * stimulus source (distance, closing speed) against its FRammsReactionParamsFragment
 * proxemics and classifies the zone (None/Aware/React/Startle) with hysteresis, a
 * closing-speed startle rule, and a startle cooldown.
 *
 * Signals are raised only on zone transitions: NewStateTreeTaskRequired (which is on
 * UMassStateTreeProcessor's fixed wake list — custom names alone would NOT tick
 * StateTrees) plus RammsStimulusZoneChanged/RammsStartled for project listeners.
 * Steady state raises nothing.
 *
 * Debug: ramms.crowd.DebugStimulus 1 draws proxemics rings and zone-colored markers.
 */
UCLASS()
class RAMMSCROWD_API URammsCrowdStimulusProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrowdStimulusProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
