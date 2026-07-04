// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdLookAtSync.h"

#include "GameFramework/Actor.h"
#include "MassActorSubsystem.h"
#include "MassExecutionContext.h"
#include "MassLookAtFragments.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsCrowdAnimComponent.h"

URammsCrowdLookAtSyncProcessor::URammsCrowdLookAtSyncProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	bRequiresGameThreadExecution = true;
}

void URammsCrowdLookAtSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassLookAtFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
}

void URammsCrowdLookAtSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassLookAtFragment> LookAtList = Context.GetFragmentView<FMassLookAtFragment>();
		const TConstArrayView<FMassActorFragment> ActorList = Context.GetFragmentView<FMassActorFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			// Only the handful of entities with a live high-LOD actor cost anything here.
			const AActor* Actor = ActorList[EntityIt].Get();
			if (Actor == nullptr)
			{
				continue;
			}
			if (URammsCrowdAnimComponent* AnimBridge = Actor->FindComponentByClass<URammsCrowdAnimComponent>())
			{
				const FMassLookAtFragment& LookAt = LookAtList[EntityIt];
				const bool bHasTarget = LookAt.LookAtMode != EMassLookAtMode::LookForward;
				AnimBridge->SetLookAt(LookAt.Direction, bHasTarget);
			}
		}
	});
}
