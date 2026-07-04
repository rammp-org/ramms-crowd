// Copyright (c) RAMMP. All rights reserved.

#include "RammsPlayerAvatarTrait.h"

#include "GameFramework/Actor.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"

void URammsPlayerAvatarTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FRammsAvatarSyncFragment>();
	BuildContext.AddFragment_GetRef<FAgentRadiusFragment>().Radius = AgentRadiusCm;

	FMassAvoidanceColliderFragment Collider;
	if (bUsePillCollider)
	{
		Collider = FMassAvoidanceColliderFragment(FMassPillCollider(AgentRadiusCm, PillHalfLengthCm));
	}
	else
	{
		Collider = FMassAvoidanceColliderFragment(FMassCircleCollider(AgentRadiusCm));
	}
	BuildContext.AddFragment(FConstStructView::Make(Collider));

	BuildContext.AddTag<FRammsPlayerAvatarTag>();
}

URammsPlayerAvatarTranslator::URammsPlayerAvatarTranslator()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	RequiredTags.Add<FRammsPlayerAvatarTag>();
	bRequiresGameThreadExecution = true;
}

void URammsPlayerAvatarTranslator::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRammsAvatarSyncFragment>(EMassFragmentAccess::ReadWrite);
}

void URammsPlayerAvatarTranslator::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();

	EntityQuery.ForEachEntityChunk(Context, [DeltaTime](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassActorFragment> ActorList = Context.GetFragmentView<FMassActorFragment>();
		const TArrayView<FTransformFragment> TransformList = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();
		const TArrayView<FRammsAvatarSyncFragment> SyncList = Context.GetMutableFragmentView<FRammsAvatarSyncFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const AActor* Actor = ActorList[EntityIt].Get();
			if (Actor == nullptr)
			{
				continue;
			}

			const FTransform ActorTransform = Actor->GetActorTransform();
			TransformList[EntityIt].SetTransform(ActorTransform);

			FVector Velocity = Actor->GetVelocity();
			FRammsAvatarSyncFragment& Sync = SyncList[EntityIt];
			const FVector Location = ActorTransform.GetLocation();
			if (Velocity.IsNearlyZero() && Sync.bHasPrevious && DeltaTime > UE_SMALL_NUMBER)
			{
				Velocity = (Location - Sync.PreviousLocation) / DeltaTime;
			}
			Sync.PreviousLocation = Location;
			Sync.bHasPrevious = true;

			VelocityList[EntityIt].Value = Velocity;
		}
	});
}
