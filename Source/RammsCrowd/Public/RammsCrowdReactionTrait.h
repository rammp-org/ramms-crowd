// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "RammsCrowdReactionTrait.generated.h"

class URammsCrowdReactionProfile;

/**
 * Opts an NPC kind into stimulus perception and reactions: adds FRammsStimulusFragment
 * (written by URammsCrowdStimulusProcessor) and bakes the referenced reaction profile
 * into a const-shared FRammsReactionParamsFragment. Kinds without this trait (e.g.
 * Vehicle) never match the perception query and cost nothing.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "RAMMS Crowd Reaction"))
class RAMMSCROWD_API URammsCrowdReactionTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	TObjectPtr<URammsCrowdReactionProfile> Profile;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
