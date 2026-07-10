// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RammsCrowdContentLibrary.generated.h"

class UTexture;

/** Editor utilities for scripted content maintenance (Python / Remote Control). */
UCLASS()
class RAMMSCROWDEDITOR_API URammsCrowdContentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Downsizes a texture's SOURCE data (halving steps) until both dimensions are
	 * <= TargetSourceSize, then applies the edit. Lossy and in-place — used to bring
	 * imported marketplace textures under repository file-size limits. Supports
	 * Texture2D and TextureCube. Returns true if the texture was changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static bool DownsizeTextureSource(UTexture* Texture, int32 TargetSourceSize);
};
