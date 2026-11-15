// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySpawnDataGeneratorBase.h"
#include "RammsCrowdBoxSpawnPointsGenerator.generated.h"

UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "RAMMS Box SpawnPoints Generator"))
class RAMMSCROWD_API URammsCrowdBoxSpawnPointsGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:
	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator")
	FVector BoxExtent = FVector(500.0f, 500.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator", meta = (ClampMin = "0.0"))
	float MinSeparationCm = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator", meta = (ClampMin = "1"))
	int32 MaxPlacementAttemptsPerEntity = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator")
	bool bAlignToOwnerRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator")
	bool bRandomizeYaw = true;

	/** No point whose nearest ZoneGraph lane carries the tag of this name (the traffic's vehicle lanes): walkers
	 *  placed there would snap onto that lane. None: no lane is avoided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Generator")
	FName ExcludeLaneTagName = FName(TEXT("Vehicle"));
};
