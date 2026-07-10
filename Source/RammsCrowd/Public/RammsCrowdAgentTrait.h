// Copyright (c) RAMMP. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTypes.h"
#include "RammsCrowdTypes.h"
#include "RammsCrowdAgentTrait.generated.h"

/** Marks entities spawned as RAMMS crowd members; filter tag for project processors. */
USTRUCT()
struct FRammsCrowdMemberTag : public FMassTag
{
	GENERATED_BODY()
};

/** Per-archetype crowd profile data shared by all entities of one kind. */
USTRUCT()
struct RAMMSCROWD_API FRammsCrowdProfileParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	ERammsCrowdActorKind ActorKind = ERammsCrowdActorKind::Person;

	/** Scalar 0..1 the animation layer can use for stride/cadence variation. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GaitVariation = 0.25f;

	/**
	 * Extra yaw applied when syncing the entity transform to a spawned representation
	 * actor. Use when the character mesh IS the actor's root component and so cannot
	 * carry its own rotation offset (e.g. City Sample crowd characters, authored
	 * facing +Y, need -90).
	 */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float RepresentationActorYawOffsetDegrees = 0.0f;
};

/**
 * Base identity trait for RAMMS crowd agents: sets the agent radius, adds
 * FRammsCrowdMemberTag, and shares FRammsCrowdProfileParameters (ActorKind, gait).
 * The reaction framework and project processors key off these.
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "RAMMS Crowd Agent"))
class RAMMSCROWD_API URammsCrowdAgentTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** Avoidance/navigation footprint radius, cm. */
	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd", meta = (ClampMin = "1.0"))
	float AgentRadiusCm = 35.0f;

	UPROPERTY(EditAnywhere, Category = "Ramms|Crowd")
	FRammsCrowdProfileParameters ProfileParameters;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
