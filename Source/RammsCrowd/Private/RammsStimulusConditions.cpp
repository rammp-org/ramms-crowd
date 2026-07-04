// Copyright (c) RAMMP. All rights reserved.

#include "RammsStimulusConditions.h"

#include "MassStateTreeDependency.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FRammsStimulusZoneCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(StimulusHandle);
	return true;
}

void FRammsStimulusZoneCondition::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(StimulusHandle);
}

bool FRammsStimulusZoneCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FRammsStimulusFragment& Stimulus = Context.GetExternalData(StimulusHandle);
	const bool bResult = Stimulus.Zone >= MinZone;
	return bResult != bInvert;
}

bool FRammsClosingSpeedCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(StimulusHandle);
	return true;
}

void FRammsClosingSpeedCondition::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(StimulusHandle);
}

bool FRammsClosingSpeedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FRammsStimulusFragment& Stimulus = Context.GetExternalData(StimulusHandle);
	const bool bResult = Stimulus.Zone != ERammsStimulusZone::None && Stimulus.ClosingSpeed > MinClosingSpeedCmS;
	return bResult != bInvert;
}
