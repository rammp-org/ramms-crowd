// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdReactionTrait.h"

#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"
#include "RammsCrowdLog.h"
#include "RammsCrowdReactionProfile.h"
#include "RammsCrowdStimulusTypes.h"

void URammsCrowdReactionTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	BuildContext.RequireFragment<FTransformFragment>();
	BuildContext.AddFragment<FRammsStimulusFragment>();

	const FRammsReactionParamsFragment Params = Profile ? Profile->Params : FRammsReactionParamsFragment();
	const FConstSharedStruct ParamsFragment = EntityManager.GetOrCreateConstSharedFragment(Params);
	BuildContext.AddConstSharedFragment(ParamsFragment);

	if (Profile != nullptr)
	{
		for (const FInstancedStruct& Extension : Profile->ExtensionParams)
		{
			const UScriptStruct* ScriptStruct = Extension.GetScriptStruct();
			if (ScriptStruct == nullptr || !ScriptStruct->IsChildOf(FMassConstSharedFragment::StaticStruct()))
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("%s: ExtensionParams entry is not a FMassConstSharedFragment subtype; skipping."), *GetPathNameSafe(Profile));
				continue;
			}
			const FConstSharedStruct ExtensionFragment = EntityManager.GetOrCreateConstSharedFragment(*ScriptStruct, Extension.GetMemory());
			BuildContext.AddConstSharedFragment(ExtensionFragment);
		}
	}
}

bool URammsCrowdReactionTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	if (Profile == nullptr)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("%s has no reaction profile assigned; entities will use default reaction parameters."), *GetPathName());
	}
	return true;
}
