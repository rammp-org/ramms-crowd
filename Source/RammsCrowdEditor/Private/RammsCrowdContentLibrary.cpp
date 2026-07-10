// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdContentLibrary.h"

#include "Engine/Texture.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "TextureSourceDataUtils.h"

bool URammsCrowdContentLibrary::DownsizeTextureSource(UTexture* Texture, const int32 TargetSourceSize)
{
	if (Texture == nullptr || TargetSourceSize < 4)
	{
		return false;
	}
#if WITH_EDITOR
	const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	Texture->PreEditChange(nullptr);
	const bool bChanged = UE::TextureUtilitiesCommon::Experimental::DownsizeTextureSourceData(Texture, TargetSourceSize, RunningPlatform);
	// DownsizeTextureSourceData does not notify by itself.
	Texture->PostEditChange();
	if (bChanged)
	{
		Texture->MarkPackageDirty();
	}
	return bChanged;
#else
	return false;
#endif
}
