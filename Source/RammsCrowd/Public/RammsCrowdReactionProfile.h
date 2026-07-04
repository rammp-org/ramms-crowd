// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RammsCrowdStimulusTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "RammsCrowdReactionProfile.generated.h"

/**
 * Data-driven reaction tuning for one NPC kind (Person, Robot, Pet, ...). Referenced by
 * URammsCrowdReactionTrait in the kind's entity config; baked into a const-shared
 * FRammsReactionParamsFragment at template build.
 *
 * Extending reactions for a new NPC kind = duplicate a DA_Reaction_* asset, tune the
 * numbers, and compose the kind's reaction StateTree states — no core C++ edits.
 */
UCLASS(BlueprintType)
class RAMMSCROWD_API URammsCrowdReactionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Proxemics radii, startle rule, cooldowns, and response tuning. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ShowOnlyInnerProperties))
	FRammsReactionParamsFragment Params;

	/**
	 * Extension point for user-defined reaction parameters: each entry must derive from
	 * FMassConstSharedFragment and is added verbatim to the entity template, readable by
	 * custom StateTree tasks/conditions without touching RammsCrowd code.
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (BaseStruct = "/Script/MassEntity.MassConstSharedFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExtensionParams;
};
