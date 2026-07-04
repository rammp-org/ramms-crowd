// Copyright (c) RAMMP. All rights reserved.

#include "RammsStimulusEvaluator.h"

#include "MassStateTreeDependency.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FRammsStimulusEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(StimulusHandle);
	return true;
}

void FRammsStimulusEvaluator::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(StimulusHandle);
}

void FRammsStimulusEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FRammsStimulusFragment& Stimulus = Context.GetExternalData(StimulusHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	InstanceData.bHasStimulus = Stimulus.Zone != ERammsStimulusZone::None;
	InstanceData.Zone = Stimulus.Zone;
	InstanceData.Distance = Stimulus.Distance;
	InstanceData.ClosingSpeed = Stimulus.ClosingSpeed;
	InstanceData.Strength = Stimulus.Strength;
	InstanceData.StimulusEntity = Stimulus.StimulusEntity;
	InstanceData.StimulusLocation = Stimulus.StimulusLocation;
	InstanceData.AwayDirection = Stimulus.AwayDirection;
}
