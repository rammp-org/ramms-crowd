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

	/**
	 * Sets a zone shape's own tags (its lanes inherit them on top of their lane profile's) by ZoneGraph tag name
	 * (Project Settings > Zone Graph). bAdd keeps the tags the shape already has; otherwise TagNames replace them.
	 * Names the settings do not define are skipped with a warning. Returns how many named tags were found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static int32 SetZoneShapeTags(UZoneShapeComponent* ShapeComponent, const TArray<FName>& TagNames, bool bAdd = false);

	/** Removes the named tags from a zone shape's own tags. Returns how many named tags were found. */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static int32 RemoveZoneShapeTags(UZoneShapeComponent* ShapeComponent, const TArray<FName>& TagNames);

	/** The names of a zone shape's own tags. */
	UFUNCTION(BlueprintPure, Category = "Ramms|Crowd")
	static TArray<FName> GetZoneShapeTagNames(const UZoneShapeComponent* ShapeComponent);
};
