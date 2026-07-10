// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RammsCrowdActorSync.generated.h"

/**
 * Pushes Mass entity transforms to Mass-owned high/low-LOD representation actors every
 * frame (Mass -> actor). The engine's representation processor only teleports an actor
 * once when the representation switches; continuous following is normally provided by
 * CharacterMovement-based sync traits, which BP_CrowdPerson (a plain AActor) does not
 * have. Without this, spawned crowd actors freeze at their spawn transform while the
 * entities walk on.
 */
UCLASS()
class RAMMSCROWD_API URammsCrowdActorSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URammsCrowdActorSyncProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
