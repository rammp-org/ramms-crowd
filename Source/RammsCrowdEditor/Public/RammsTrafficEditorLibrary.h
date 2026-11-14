// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsTrafficEditorLibrary.generated.h"

class UMaterialParameterCollection;

/** Editor utilities for building traffic content from script (RammsWorldGen traffic.py). */
UCLASS()
class RAMMSCROWDEDITOR_API URammsTrafficEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The material parameter collection the traffic signal clock drives, at AssetPath (a long package name, e.g.
	 * "/Game/WorldGen/Traffic/MPC_RammsTrafficSignals"): created when missing, and given every scalar the clock
	 * sets (RammsTraffic::GetSignalParameterDefaults) at its resting value. Existing parameters keep their ids, so
	 * materials that read them stay valid. The caller saves it. Null when the path is not a valid package name.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Traffic")
	static UMaterialParameterCollection* EnsureSignalParameterCollection(const FString& AssetPath);

	/** The scalar parameter names the signal clock drives, in order. */
	UFUNCTION(BlueprintPure, Category = "Ramms|Traffic")
	static TArray<FName> GetSignalParameterNames();
};
