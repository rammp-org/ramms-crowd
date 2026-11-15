// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficVehicleTrait.h"

#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"

void URammsTrafficVehicleTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FRammsTrafficVehicleFragment>();
	BuildContext.AddFragment<FRammsTrafficVisualFragment>();
	BuildContext.AddTag<FRammsTrafficVehicleTag>();

	// Inside the editor's ranges whatever set the values (Python, Blueprint): the rules divide by some of them.
	const FRammsTrafficDrivingParams Params = Driving.Sanitized();
	const FConstSharedStruct DrivingFragment = EntityManager.GetOrCreateConstSharedFragment(Params);
	BuildContext.AddConstSharedFragment(DrivingFragment);
}
