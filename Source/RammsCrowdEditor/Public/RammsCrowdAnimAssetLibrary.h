// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdAnimAssetLibrary.generated.h"

class UAnimBlueprint;
class UBlendSpace;
class USkeleton;

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

	/**
	 * Creates (or rebuilds) a post-process AnimBP whose graph is
	 * Input Pose -> Foot Placement -> Output Pose, for crowd foot IK over minor
	 * obstacles. Configured for the City Sample SK_Base bone naming (pelvis,
	 * ik_foot_root, foot/ik_foot/ball _l/_r) with foot locking disabled (the
	 * crowd anims carry no foot-speed curves). Assign the result as the
	 * Post Process Anim Blueprint on the crowd body meshes — see
	 * Content/Python/ramms_crowd_assign_foot_ik.py, which calls this when the
	 * AnimBP is missing. Idempotent: an existing asset is rebuilt in place.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static UAnimBlueprint* CreateFootPlacementPostProcessAnimBlueprint(const FString& PackagePath, const FString& AssetName, USkeleton* Skeleton);
};
