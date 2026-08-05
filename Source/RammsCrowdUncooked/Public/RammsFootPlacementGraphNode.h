// Copyright (c) RAMMP. All rights reserved.
// Editor graph node for the vendored FAnimNode_RammsFootPlacement (see
// RammsFootPlacement.h for why the stock engine node cannot be used on
// Mass crowd puppets).

#pragma once
#include "AnimGraphNode_SkeletalControlBase.h"
#include "RammsFootPlacement.h"
#include "RammsFootPlacementGraphNode.generated.h"

namespace ENodeTitleType
{
	enum Type : int;
}

UCLASS(MinimalAPI, Experimental)
class UAnimGraphNode_RammsFootPlacement : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_RammsFootPlacement Node;

public:
	// UEdGraphNode interface
	RAMMSCROWDUNCOOKED_API virtual FText		  GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	RAMMSCROWDUNCOOKED_API virtual FText		  GetTooltipText() const override;
	RAMMSCROWDUNCOOKED_API virtual FLinearColor GetNodeTitleColor() const override;
	// End of UEdGraphNode interface

protected:
	// UAnimGraphNode_SkeletalControlBase interface
	RAMMSCROWDUNCOOKED_API virtual FText			 GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface
};
