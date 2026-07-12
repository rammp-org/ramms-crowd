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

	/**
	 * Shows an editor toast notification (bottom-right slide-in), optionally with a
	 * clickable hyperlink. Used by the content bootstrap so setup problems are
	 * visible without watching the output log. No-op outside the editor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static void ShowSetupNotification(const FString& Message, const FString& HyperlinkText, const FString& HyperlinkURL, float DurationSeconds = 12.0f, bool bWarning = false);

	/**
	 * Converts virtual-textured Texture2Ds back to regular streaming textures,
	 * INCLUDING fixing the sampler types of every referencing material (same
	 * machinery as the editor's right-click "Convert to Regular" action). Needed
	 * after downsizing VT textures: a VT whose size/layout changed can hit the
	 * HLSLMaterialTranslator VTStacks assertion when its materials recompile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static int32 ConvertTexturesToNonVirtual(const TArray<UTexture2D*>& Textures);

	/**
	 * Fixes up every ObjectRedirector under FolderPath: loads all referencers
	 * (hard AND soft references), rewrites them to the redirect targets, resaves,
	 * and deletes the redirectors. Returns the number of redirectors processed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ramms|Crowd")
	static int32 FixUpRedirectorsInFolder(const FString& FolderPath);
};
