// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "RammsTrafficTypes.h"
#include "RammsTrafficVehicleTrait.generated.h"

/**
 * A kinematic traffic vehicle on the ZoneGraph vehicle lanes: transform, lane state
 * (FRammsTrafficVehicleFragment), representation link (FRammsTrafficVisualFragment), the
 * FRammsTrafficVehicleTag and the shared driving parameters. URammsTrafficVehicleProcessor drives it,
 * URammsTrafficVisualProcessor moves its actor. ARammsTrafficSpawner builds its config from this trait.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "RAMMS Traffic Vehicle"))
class RAMMSCROWD_API URammsTrafficVehicleTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Ramms|Traffic")
	FRammsTrafficDrivingParams Driving;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
