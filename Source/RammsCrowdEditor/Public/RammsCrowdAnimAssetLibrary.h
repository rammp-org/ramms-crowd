// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdAnimAssetLibrary.generated.h"

class UBlendSpace;

/** Editor utilities for scripted animation-asset authoring (Python / Remote Control). */
UCLASS()
class RAMMSCROWDEDITOR_API URammsCrowdAnimAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Validates and resamples a blend space's internal triangulation. Required after
	 * setting axes/SampleData from script: without it the runtime blend data stays
	 * empty and the blend space evaluates to the reference pose with frozen time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static void RebuildBlendSpace(UBlendSpace* BlendSpace);
};
