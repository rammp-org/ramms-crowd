// Copyright (c) RAMMP. All rights reserved.

#include "RammsFootPlacementGraphNode.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_RammsFootPlacement"

UAnimGraphNode_RammsFootPlacement::UAnimGraphNode_RammsFootPlacement(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_RammsFootPlacement::GetControllerDescription() const
{
	return LOCTEXT("RammsFootPlacement", "Ramms Foot Placement");
}

FText UAnimGraphNode_RammsFootPlacement::GetTooltipText() const
{
	return LOCTEXT("RammsFootPlacementTooltip", "Foot Placement that also works on actors without a CharacterMovementComponent (Mass crowd puppets).");
}

FLinearColor UAnimGraphNode_RammsFootPlacement::GetNodeTitleColor() const
{
	return FLinearColor(FColor(153, 40, 0));
}

FText UAnimGraphNode_RammsFootPlacement::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

#undef LOCTEXT_NAMESPACE
