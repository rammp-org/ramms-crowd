// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdZoneGraphLibrary.generated.h"

class UZoneShapeComponent;

/** Editor utilities for scripted ZoneGraph authoring (Python / Remote Control). */
UCLASS()
class RAMMSCROWDEDITOR_API URammsCrowdZoneGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Recalculates a zone shape's cached data after its Points/profile were set from script. */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static void UpdateZoneShape(UZoneShapeComponent* ShapeComponent);

	/** Triggers a full ZoneGraph rebuild (same as the editor's Build ZoneGraph action). */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static void RebuildZoneGraph();
};
