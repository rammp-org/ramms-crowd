// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdMotionMatchingLibrary.generated.h"

class UPoseSearchDatabase;
class USkeleton;

/** Editor utilities for building the crowd's motion matching content (Python / Remote Control). */
UCLASS()
class RAMMSCROWDEDITOR_API URammsCrowdMotionMatchingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Copies pose search databases (and their schemas) authored for another skeleton
	 * onto TargetSkeleton, keeping their tuning: each database animation is swapped
	 * for its retargeted twin, found as <AnimationPath>/<SourceName><AnimationSuffix>
	 * (as the IK retarget batch names them). Assets without a twin are dropped and
	 * logged. Mirroring is turned off (the target has no mirror table). Copies go to
	 * DestinationPath with "_<NameSuffix>" appended; existing copies are rebuilt in
	 * place. The search indexes are built before returning, and the packages saved.
	 *
	 * Typical use: the Game Animation Sample's relaxed walk databases onto the City
	 * Sample crowd skeleton - see Content/Python/ramms_crowd_build_motion_matching.py.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static TArray<UPoseSearchDatabase*> AdaptPoseSearchDatabases(const TArray<UPoseSearchDatabase*>& SourceDatabases, USkeleton* TargetSkeleton,
		const FString& AnimationPath, const FString& AnimationSuffix, const FString& DestinationPath, const FString& NameSuffix);
};
