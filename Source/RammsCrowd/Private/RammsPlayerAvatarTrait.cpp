// Copyright (c) RAMMP. All rights reserved.

#include "RammsPlayerAvatarTrait.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "RammsCrowdLog.h"

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

	FRammsAvatarFootprintFragment Footprint;
	Footprint.bAutoFootprint = bAutoFootprint;
	Footprint.PaddingCm = FootprintPaddingCm;
	Footprint.bKeepCrowdClear = bKeepCrowdClear;
	Footprint.ClearanceMarginCm = ClearanceMarginCm;
	Footprint.ApproachBandCm = ApproachBandCm;
	BuildContext.AddFragment(FConstStructView::Make(Footprint));

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
	EntityQuery.AddRequirement<FRammsAvatarFootprintFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassAvoidanceColliderFragment>(EMassFragmentAccess::ReadWrite);
}

namespace
{
	/** Fits Footprint, Radius and Collider to the actor's colliding components, in its own frame. */
	void FitFootprint(const AActor& Actor, FRammsAvatarFootprintFragment& Footprint, FAgentRadiusFragment& Radius, FMassAvoidanceColliderFragment& Collider)
	{
		Footprint.bResolved = true;
		// Only what the robot can physically bump into: components with physical
		// collision. Query-only volumes (workspace triggers, sensor proxies) do not count.
		const FTransform ActorToWorld = Actor.GetActorTransform();
		FBox Box(ForceInit);
		TInlineComponentArray<UPrimitiveComponent*> Primitives(&Actor);
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			if (!Primitive->IsRegistered() || !CollisionEnabledHasPhysics(Primitive->GetCollisionEnabled()))
			{
				continue;
			}
			const FTransform ComponentToActor = Primitive->GetComponentTransform().GetRelativeTransform(ActorToWorld);
			Box += Primitive->CalcBounds(ComponentToActor).GetBox();
		}
		if (!Box.IsValid || Box.GetExtent().X < 1.0 || Box.GetExtent().Y < 1.0)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Player avatar: no colliding components to fit a footprint to; keeping the configured collider."), *Actor.GetName());
			return;
		}
		const FVector Center = Box.GetCenter();
		const FVector Extent = Box.GetExtent();
		Footprint.LocalOffset = FVector2D(Center.X, Center.Y);
		// A pill along the longer side: forward (X) for a wheelchair. A box that is wider
		// than long gets a circle over its width.
		if (Extent.X > Extent.Y)
		{
			const float PillRadius = Extent.Y + Footprint.PaddingCm;
			const float HalfLength = Extent.X - Extent.Y;
			Collider = FMassAvoidanceColliderFragment(FMassPillCollider(PillRadius, HalfLength));
			Radius.Radius = PillRadius;
		}
		else
		{
			const float CircleRadius = Extent.Y + Footprint.PaddingCm;
			Collider = FMassAvoidanceColliderFragment(FMassCircleCollider(CircleRadius));
			Radius.Radius = CircleRadius;
		}
		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Player avatar footprint: centre (%.0f, %.0f) cm, %.0f x %.0f cm, radius %.0f cm"),
			*Actor.GetName(), Center.X, Center.Y, 2.0 * Extent.X, 2.0 * Extent.Y, Radius.Radius);
	}
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
		const TArrayView<FRammsAvatarFootprintFragment> FootprintList = Context.GetMutableFragmentView<FRammsAvatarFootprintFragment>();
		const TArrayView<FAgentRadiusFragment> RadiusList = Context.GetMutableFragmentView<FAgentRadiusFragment>();
		const TArrayView<FMassAvoidanceColliderFragment> ColliderList = Context.GetMutableFragmentView<FMassAvoidanceColliderFragment>();

		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const AActor* Actor = ActorList[EntityIt].Get();
			if (Actor == nullptr)
			{
				continue;
			}

			FRammsAvatarFootprintFragment& Footprint = FootprintList[EntityIt];
			if (Footprint.bAutoFootprint && !Footprint.bResolved)
			{
				FitFootprint(*Actor, Footprint, RadiusList[EntityIt], ColliderList[EntityIt]);
			}

			// The entity sits at the footprint centre, so avoidance and clearance use the
			// robot's real outline rather than one centred on its root.
			FTransform ActorTransform = Actor->GetActorTransform();
			ActorTransform.SetLocation(ActorTransform.TransformPosition(FVector(Footprint.LocalOffset.X, Footprint.LocalOffset.Y, 0.0)));
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
