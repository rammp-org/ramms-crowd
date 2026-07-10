// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdActorSync.h"

#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"
#include "RammsCrowdAgentTrait.h"

URammsCrowdActorSyncProcessor::URammsCrowdActorSyncProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	// SetActorTransform must run on the game thread; bounded by the HighRes actor cap.
	bRequiresGameThreadExecution = true;
}

void URammsCrowdActorSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FRammsCrowdProfileParameters>();
}

void URammsCrowdActorSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassRepresentationFragment> RepresentationList = Context.GetFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassActorFragment> ActorList = Context.GetMutableFragmentView<FMassActorFragment>();
		const FRammsCrowdProfileParameters& Profile = Context.GetConstSharedFragment<FRammsCrowdProfileParameters>();

		// Meshes that are the actor's root (City Sample) cannot carry a child-relative
		// rotation offset, so the authored-facing correction is applied here.
		const bool bApplyYawOffset = !FMath::IsNearlyZero(Profile.RepresentationActorYawOffsetDegrees);
		const FQuat YawOffset = FQuat(FVector::UpVector, FMath::DegreesToRadians(Profile.RepresentationActorYawOffsetDegrees));

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const EMassRepresentationType Representation = RepresentationList[EntityIt].CurrentRepresentation;
			if (Representation != EMassRepresentationType::HighResSpawnedActor
				&& Representation != EMassRepresentationType::LowResSpawnedActor)
			{
				continue;
			}

			// Only actors Mass spawned for representation; never externally-owned
			// actors (e.g. the player avatar puppet).
			AActor* Actor = ActorList[EntityIt].GetOwnedByMassMutable();
			if (Actor == nullptr)
			{
				continue;
			}

			FTransform Transform = TransformList[EntityIt].GetTransform();
			if (bApplyYawOffset)
			{
				Transform.SetRotation(Transform.GetRotation() * YawOffset);
			}
			Actor->SetActorTransform(Transform, /*bSweep*/ false, /*OutHit*/ nullptr, ETeleportType::TeleportPhysics);
		}
	});
}
