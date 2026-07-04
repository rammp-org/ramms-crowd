// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RammsStimulusSourceComponent.generated.h"

class UMassAgentComponent;
class URammsStimulusSubsystem;

/**
 * Marks the owning actor (player robot, van, ...) as a stimulus source the crowd
 * perceives and reacts to. Pushes a per-frame POD snapshot (position, velocity,
 * radius) into URammsStimulusSubsystem. If the owner also has a UMassAgentComponent
 * (player avatar puppet entity), the puppet handle rides along in the snapshot so
 * StateTree look-at tasks can target it.
 */
UCLASS(ClassGroup = (Ramms), meta = (BlueprintSpawnableComponent))
class RAMMSCROWD_API URammsStimulusSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URammsStimulusSourceComponent();

	/** Physical radius of the source used for surface-distance computation, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd", meta = (ClampMin = "0.0"))
	float RadiusCm = 50.0f;

	/** Scales perceived stimulus strength; >1 makes NPCs react as if the source were closer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd", meta = (ClampMin = "0.0"))
	float StrengthScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd")
	bool bEnabled = true;

	/** When moving faster than DisturbanceSpeedThresholdCmS, periodically trigger a ZoneGraph
	 *  lane disturbance so lane-following crowds re-route away (flee via escape lanes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd|Disturbance")
	bool bTriggerLaneDisturbance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd|Disturbance", meta = (ClampMin = "0.0", EditCondition = "bTriggerLaneDisturbance"))
	float DisturbanceSpeedThresholdCmS = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd|Disturbance", meta = (ClampMin = "0.0", EditCondition = "bTriggerLaneDisturbance"))
	float DisturbanceRadiusCm = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramms|Crowd|Disturbance", meta = (ClampMin = "0.0", EditCondition = "bTriggerLaneDisturbance"))
	float DisturbanceDurationSeconds = 3.0f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector ResolveVelocity(const FVector& CurrentLocation, float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<UMassAgentComponent> CachedAgentComponent;

	int32 SourceHandle = INDEX_NONE;
	FVector PreviousLocation = FVector::ZeroVector;
	bool bHasPreviousLocation = false;
	double LastDisturbanceTime = -1.0;
};
