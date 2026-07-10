// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimAssetLibrary.h"

#include "Animation/BlendSpace.h"

void URammsCrowdAnimAssetLibrary::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
	if (BlendSpace == nullptr)
	{
		return;
	}
#if WITH_EDITOR
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->MarkPackageDirty();
#endif
}
