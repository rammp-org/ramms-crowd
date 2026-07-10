// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdStateTreeLibrary.generated.h"

class UStateTree;

/**
 * Editor scaffolding for RammsCrowd StateTree assets. Builds runnable starting-point
 * trees programmatically (also callable from Python / Remote Control), which the
 * StateTree editor UI can then extend.
 */
UCLASS()
class RAMMSCROWDEDITOR_API URammsCrowdStateTreeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or rebuilds from scratch, if it exists) a minimal Mass pedestrian wander
	 * StateTree and compiles it: root state "Wander" running ZG Find Wander Target ->
	 * ZG Path Follow (target bound to the wander output), looping on completion.
	 * Uses the Mass Behavior schema so it is valid for UMassStateTreeTrait.
	 * Returns the asset (dirty, not saved) or nullptr on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static UStateTree* CreatePedestrianWanderStateTree(const FString& PackagePath = TEXT("/Game/NPCs"), const FString& AssetName = TEXT("ST_Pedestrian"));
};
