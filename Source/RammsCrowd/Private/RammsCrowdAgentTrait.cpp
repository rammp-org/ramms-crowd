// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAgentTrait.h"

#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"

void URammsCrowdAgentTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	BuildContext.AddFragment_GetRef<FAgentRadiusFragment>().Radius = AgentRadiusCm;
	BuildContext.AddTag<FRammsCrowdMemberTag>();

	const FConstSharedStruct ProfileFragment = EntityManager.GetOrCreateConstSharedFragment(ProfileParameters);
	BuildContext.AddConstSharedFragment(ProfileFragment);
}
