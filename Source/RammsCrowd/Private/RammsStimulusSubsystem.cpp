// Copyright (c) RAMMP. All rights reserved.

#include "RammsStimulusSubsystem.h"

int32 URammsStimulusSubsystem::RegisterSource()
{
	const int32 Handle = NextHandle++;
	Sources.Add(Handle);
	return Handle;
}

void URammsStimulusSubsystem::UnregisterSource(const int32 SourceHandle)
{
	Sources.Remove(SourceHandle);
	ActiveHandles.Remove(SourceHandle);
	RebuildCache();
}

void URammsStimulusSubsystem::UpdateSource(const int32 SourceHandle, const FRammsStimulusSnapshot& Snapshot)
{
	if (FRammsStimulusSnapshot* Existing = Sources.Find(SourceHandle))
	{
		*Existing = Snapshot;
		ActiveHandles.AddUnique(SourceHandle);
		RebuildCache();
	}
}

void URammsStimulusSubsystem::ClearSource(const int32 SourceHandle)
{
	ActiveHandles.Remove(SourceHandle);
	RebuildCache();
}

void URammsStimulusSubsystem::RebuildCache()
{
	CachedSnapshots.Reset(ActiveHandles.Num());
	for (const int32 Handle : ActiveHandles)
	{
		if (const FRammsStimulusSnapshot* Snapshot = Sources.Find(Handle))
		{
			CachedSnapshots.Add(*Snapshot);
		}
	}
}
