// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "MassExternalSubsystemTraits.h"
#include "Subsystems/WorldSubsystem.h"
#include "RammsStimulusSubsystem.generated.h"

/** POD snapshot of one stimulus source, pushed each frame by URammsStimulusSourceComponent. */
struct FRammsStimulusSnapshot
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Radius = 50.0f;
	float StrengthScale = 1.0f;
	/** Mass entity of the source (player avatar puppet), if any. */
	FMassEntityHandle Entity;
};

/**
 * Registry of stimulus sources (player robot, van, other robots, virtual experiment
 * stimuli) the crowd perception processor evaluates against. Sources push POD
 * snapshots on the game thread; URammsCrowdStimulusProcessor reads them batch-wise.
 */
UCLASS()
class RAMMSCROWD_API URammsStimulusSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Registers a source and returns its handle. */
	int32 RegisterSource();

	void UnregisterSource(int32 SourceHandle);

	/** Updates the snapshot for a registered source. Call every frame the source is active. */
	void UpdateSource(int32 SourceHandle, const FRammsStimulusSnapshot& Snapshot);

	/** Marks a source inactive without unregistering (e.g. bEnabled=false). */
	void ClearSource(int32 SourceHandle);

	/** All currently active source snapshots. Game-thread access (see TMassExternalSubsystemTraits). */
	TConstArrayView<FRammsStimulusSnapshot> GetSnapshots() const { return CachedSnapshots; }

private:
	void RebuildCache();

	TMap<int32, FRammsStimulusSnapshot> Sources;
	TArray<int32> ActiveHandles;
	TArray<FRammsStimulusSnapshot> CachedSnapshots;
	int32 NextHandle = 0;
};

template <>
struct TMassExternalSubsystemTraits<URammsStimulusSubsystem> final
{
	enum
	{
		// Perception runs on the game thread for v1; revisit if the crowd grows past ~1000.
		GameThreadOnly = true,
		ThreadSafeWrite = false,
	};
};
