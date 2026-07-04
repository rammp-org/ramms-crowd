// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "RammsCrowdStimulusTypes.h"
#include "RammsStimulusEvaluator.generated.h"

struct FStateTreeExecutionContext;
namespace UE::MassBehavior
{
struct FStateTreeDependencyBuilder;
}

USTRUCT()
struct FRammsStimulusEvaluatorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasStimulus = false;

	UPROPERTY(EditAnywhere, Category = Output)
	ERammsStimulusZone Zone = ERammsStimulusZone::None;

	UPROPERTY(EditAnywhere, Category = Output)
	float Distance = 0.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float ClosingSpeed = 0.0f;

	UPROPERTY(EditAnywhere, Category = Output)
	float Strength = 0.0f;

	/** Mass entity of the stimulus source — bind to FMassLookAtTask.TargetEntity for gaze. */
	UPROPERTY(EditAnywhere, Category = Output)
	FMassEntityHandle StimulusEntity;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector StimulusLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector AwayDirection = FVector::ForwardVector;
};

/**
 * Exposes the entity's FRammsStimulusFragment (written by URammsCrowdStimulusProcessor)
 * to the StateTree as bindable outputs. Place one at the tree root.
 */
USTRUCT(meta = (DisplayName = "RAMMS Stimulus"))
struct RAMMSCROWD_API FRammsStimulusEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRammsStimulusEvaluatorInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;

	TStateTreeExternalDataHandle<FRammsStimulusFragment> StimulusHandle;
};
