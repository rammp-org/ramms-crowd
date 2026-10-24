// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimSet.h"

const FRammsCrowdBodyAnims* URammsCrowdAnimSet::FindBody(const FString& MeshName) const
{
	for (const FRammsCrowdBodyAnims& Body : Bodies)
	{
		// "f_tal_nrw_base" contains "f_" and "_nrw": match gender prefix and build separately
		// so the height token in between does not matter.
		FString Gender;
		FString Build;
		if (Body.BodyKey.Split(TEXT("_"), &Gender, &Build)
			&& MeshName.StartsWith(Gender + TEXT("_"), ESearchCase::IgnoreCase)
			&& MeshName.Contains(TEXT("_") + Build + TEXT("_"), ESearchCase::IgnoreCase))
		{
			return &Body;
		}
	}
	return Bodies.IsEmpty() ? nullptr : &Bodies[0];
}
