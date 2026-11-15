// Copyright Epic Games, Inc. All Rights Reserved.

#include "RammsCrowdBoxSpawnPointsGenerator.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MassSpawnLocationProcessor.h"
#include "RammsCrowdLog.h"
#include "ZoneGraphSettings.h"
#include "ZoneGraphSubsystem.h"

namespace
{
	/** Whether the ZoneGraph lane nearest Point (as the crowd's lane location initializer would pick it) carries Tag. */
	static bool IsNearestLaneTagged(const UZoneGraphSubsystem* ZoneGraph, const FZoneGraphTag Tag, const FVector& Point)
	{
		if (ZoneGraph == nullptr || !Tag.IsValid())
		{
			return false;
		}
		FZoneGraphLaneLocation Location;
		float DistanceSquared = 0.0f;
		FZoneGraphTagMask Tags;
		return ZoneGraph->FindNearestLane(FBox::BuildAABB(Point, FVector(500.0, 500.0, 200.0)), FZoneGraphTagFilter(), Location, DistanceSquared)
			&& ZoneGraph->GetLaneTags(Location.LaneHandle, Tags) && Tags.Contains(Tag);
	}

	static bool IsFarEnoughFromExistingPoints(const TArray<FVector>& ExistingPoints, const FVector& CandidatePoint, const float MinSeparationCm)
	{
		if (MinSeparationCm <= 0.0f)
		{
			return true;
		}

		const float MinSeparationSquared = FMath::Square(MinSeparationCm);
		for (const FVector& ExistingPoint : ExistingPoints)
		{
			if (FVector::DistSquared(ExistingPoint, CandidatePoint) < MinSeparationSquared)
			{
				return false;
			}
		}

		return true;
	}
} // namespace

void URammsCrowdBoxSpawnPointsGenerator::Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count, FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	if (Count <= 0)
	{
		FinishedGeneratingSpawnPointsDelegate.Execute(Results);
		return;
	}

	BuildResultsFromEntityTypes(Count, EntityTypes, Results);
	if (Results.IsEmpty())
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Box generator produced no results for Count=%d. Check that the MassEntityConfig loads and validates."),
			*GetNameSafe(&QueryOwner),
			Count);
		FinishedGeneratingSpawnPointsDelegate.Execute(Results);
		return;
	}

	const AActor*	 OwnerActor = Cast<const AActor>(&QueryOwner);
	const FTransform OwnerTransform = OwnerActor != nullptr ? OwnerActor->GetActorTransform() : FTransform::Identity;
	const FQuat		 BaseRotation = bAlignToOwnerRotation ? OwnerTransform.GetRotation() : FQuat::Identity;
	FRandomStream	 RandomStream(GetRandomSelectionSeed());
	TArray<FVector>	 AcceptedLocations;
	AcceptedLocations.Reserve(Count);

	// The traffic's vehicle lanes (when the project defines the tag): a walker placed nearer one than any pedestrian
	// lane would snap onto it.
	FZoneGraphTag Excluded;
	if (!ExcludeLaneTagName.IsNone())
	{
		if (const UZoneGraphSettings* Settings = GetDefault<UZoneGraphSettings>())
		{
			for (const FZoneGraphTagInfo& Info : Settings->GetTagInfos())
			{
				if (Info.IsValid() && Info.Name == ExcludeLaneTagName)
				{
					Excluded = Info.Tag;
					break;
				}
			}
		}
	}
	const UWorld* World = QueryOwner.GetWorld();
	const UZoneGraphSubsystem* ZoneGraph = Excluded.IsValid() && World != nullptr ? UWorld::GetSubsystem<UZoneGraphSubsystem>(World) : nullptr;
	int32 OnExcludedLanes = 0;

	auto BuildRandomTransform = [&](const FVector& Location) -> FTransform {
		FQuat Rotation = BaseRotation;
		if (bRandomizeYaw)
		{
			const float RandomYawDegrees = RandomStream.FRandRange(-180.0f, 180.0f);
			Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(RandomYawDegrees)) * Rotation;
		}

		return FTransform(Rotation, Location);
	};

	for (FMassEntitySpawnDataGeneratorResult& Result : Results)
	{
		Result.SpawnDataProcessor = UMassSpawnLocationProcessor::StaticClass();
		Result.SpawnData.InitializeAs<FMassTransformsSpawnData>();
		FMassTransformsSpawnData& TransformData = Result.SpawnData.GetMutable<FMassTransformsSpawnData>();
		TransformData.Transforms.Reserve(Result.NumEntities);

		for (int32 EntityIndex = 0; EntityIndex < Result.NumEntities; ++EntityIndex)
		{
			FVector SelectedLocation = OwnerTransform.TransformPosition(LocalCenter);
			bool	bFoundSeparatedLocation = false;

			for (int32 AttemptIndex = 0; AttemptIndex < MaxPlacementAttemptsPerEntity; ++AttemptIndex)
			{
				const FVector LocalOffset(
					RandomStream.FRandRange(-BoxExtent.X, BoxExtent.X),
					RandomStream.FRandRange(-BoxExtent.Y, BoxExtent.Y),
					RandomStream.FRandRange(-BoxExtent.Z, BoxExtent.Z));
				const FVector CandidateLocation = OwnerTransform.TransformPosition(LocalCenter + LocalOffset);
				if (IsFarEnoughFromExistingPoints(AcceptedLocations, CandidateLocation, MinSeparationCm) && !IsNearestLaneTagged(ZoneGraph, Excluded, CandidateLocation))
				{
					SelectedLocation = CandidateLocation;
					bFoundSeparatedLocation = true;
					break;
				}
			}

			if (bFoundSeparatedLocation == false)
			{
				// No separated point: any point off the excluded lanes, else any point at all.
				for (int32 AttemptIndex = 0; AttemptIndex < FMath::Max(MaxPlacementAttemptsPerEntity, 1); ++AttemptIndex)
				{
					const FVector LocalOffset(
						RandomStream.FRandRange(-BoxExtent.X, BoxExtent.X),
						RandomStream.FRandRange(-BoxExtent.Y, BoxExtent.Y),
						RandomStream.FRandRange(-BoxExtent.Z, BoxExtent.Z));
					SelectedLocation = OwnerTransform.TransformPosition(LocalCenter + LocalOffset);
					if (!IsNearestLaneTagged(ZoneGraph, Excluded, SelectedLocation))
					{
						break;
					}
				}
				OnExcludedLanes += IsNearestLaneTagged(ZoneGraph, Excluded, SelectedLocation) ? 1 : 0;
			}

			AcceptedLocations.Add(SelectedLocation);
			TransformData.Transforms.Add(BuildRandomTransform(SelectedLocation));
		}
	}

	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Box generator produced %d transforms across %d result buckets"),
		*GetNameSafe(&QueryOwner),
		AcceptedLocations.Num(),
		Results.Num());
	if (OnExcludedLanes > 0)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Box generator: %d spawn points are nearest a '%s' lane (no other room in the box); those walkers start on it."),
			*GetNameSafe(&QueryOwner), OnExcludedLanes, *ExcludeLaneTagName.ToString());
	}
	constexpr int32 MaxLoggedLocations = 5;
	for (int32 LocationIndex = 0; LocationIndex < FMath::Min(AcceptedLocations.Num(), MaxLoggedLocations); ++LocationIndex)
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("[%s]   spawn point %d: %s"),
			*GetNameSafe(&QueryOwner), LocationIndex, *AcceptedLocations[LocationIndex].ToCompactString());
	}
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}
