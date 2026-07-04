// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsCrowdLookAtSync.generated.h"

/**
 * Pushes Mass look-at gaze data (FMassLookAtFragment, computed by the engine's
 * UMassLookAtProcessor) to URammsCrowdAnimComponent on entities that currently have a
 * high-LOD representation actor. Game-thread by necessity (touches UObjects), bounded
 * by the HighRes actor cap (~25 actors), not crowd size.
 */
UCLASS()
class RAMMSCROWD_API URammsCrowdLookAtSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrowdLookAtSyncProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
