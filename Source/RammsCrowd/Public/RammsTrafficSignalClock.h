// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RammsTrafficTypes.h"
#include "ZoneGraphTypes.h"
#include "RammsTrafficSignalClock.generated.h"

class AZoneGraphData;
class UMaterialParameterCollection;

/**
 * The clock every signalized intersection in the level shares: one global phase (FRammsSignalTiming), read by
 * the traffic (vehicles stop at red and amber stop lines), shown by the signal heads through a material
 * parameter collection (the signal materials RammsWorldGen builds read it: lamps per head axis, walk / hand on
 * the pedestrian heads), and applied to the crowd: every street-crossing lane (ZoneGraph lanes with MassCrowd's
 * CrowdTag and CrossingTag) is opened through UMassCrowdSubsystem::SetLaneState while its walkers have Walk and
 * closed otherwise, and the RAMMS crossing gate holds pedestrians at the start of a closed crossing.
 *
 * Placed by the level builder (RammsWorldGen traffic.py), which also sets SignalParameters. Without a clock,
 * signalized intersections run as all-way stops and the crossings stay as MassCrowd leaves them (closed).
 */
UCLASS(BlueprintType)
class RAMMSCROWD_API ARammsTrafficSignalClock : public AActor
{
	GENERATED_BODY()

public:
	ARammsTrafficSignalClock();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	FRammsSignalTiming Timing;

	/** Collection the signal materials read (scalars GreenX, AmberX, RedX, GreenY, AmberY, RedY, WalkX, HandX, WalkY, HandY). Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	TObjectPtr<UMaterialParameterCollection> SignalParameters = nullptr;

	/** Open and close the crowd's crossing lanes with the walk signals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic")
	bool bControlCrossings = true;

	/** Flashing Don't Walk: the hand blinks with this period, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Traffic", meta = (ClampMin = "0.1", Units = "s"))
	float HandFlashPeriod = 1.0f;

	/** The signals at a world time. */
	FRammsSignalState GetSignalStateAt(double WorldTimeSeconds) const;

	/** The signals now. */
	UFUNCTION(BlueprintPure, Category = "Ramms|Traffic")
	FRammsSignalState GetSignalState() const;

	/** Crossing lanes the clock opens and closes (found on the first ticks after the ZoneGraph and the crowd's lane data are in). */
	UFUNCTION(BlueprintPure, Category = "Ramms|Traffic")
	int32 GetControlledCrossingCount() const { return Crossings.Num(); }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FCrossing
	{
		FZoneGraphLaneHandle Lane;
		/** Axis its walkers walk along (0 X, 1 Y). */
		int32 Axis = 0;
		/** Last state sent: 0 closed, 1 open, -1 none yet. */
		int8 Applied = -1;
	};

	void PushParameters(const FRammsSignalState& State, double Time);
	void UpdateCrossings(const FRammsSignalState& State);
	bool CollectCrossings();
	void HandleZoneGraphChanged(const AZoneGraphData* ZoneGraphData);

	TArray<FCrossing> Crossings;
	bool bCrossingsCollected = false;
	TMap<FName, float> LastParameters;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
};
