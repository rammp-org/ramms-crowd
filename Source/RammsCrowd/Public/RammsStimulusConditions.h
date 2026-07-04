// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "RammsCrowdStimulusTypes.h"
#include "RammsStimulusConditions.generated.h"

struct FStateTreeExecutionContext;
namespace UE::MassBehavior
{
struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FRammsStimulusZoneConditionInstanceData
{
	GENERATED_BODY()
};

/** True when the entity's stimulus zone is at least MinZone (hysteresis/cooldowns already applied). */
USTRUCT(meta = (DisplayName = "RAMMS Stimulus Zone"))
struct RAMMSCROWD_API FRammsStimulusZoneCondition : public FMassStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsStimulusZoneConditionInstanceData;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	UPROPERTY(EditAnywhere, Category = Condition)
	ERammsStimulusZone MinZone = ERammsStimulusZone::Aware;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;

protected:
	TStateTreeExternalDataHandle<FRammsStimulusFragment> StimulusHandle;
};

USTRUCT()
struct FRammsClosingSpeedConditionInstanceData
{
	GENERATED_BODY()
};

/** True when the nearest stimulus is approaching faster than MinClosingSpeedCmS. */
USTRUCT(meta = (DisplayName = "RAMMS Closing Speed"))
struct RAMMSCROWD_API FRammsClosingSpeedCondition : public FMassStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsClosingSpeedConditionInstanceData;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (ClampMin = "0.0"))
	float MinClosingSpeedCmS = 150.0f;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;

protected:
	TStateTreeExternalDataHandle<FRammsStimulusFragment> StimulusHandle;
};
