// Copyright (c) RAMMP. All rights reserved.

#include "RammsRobotClearanceProcessor.h"

#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassNavigationFragments.h"
#include "RammsCrowdAgentTrait.h"
#include "RammsPlayerAvatarTrait.h"

namespace
{
	struct FRammsFootprint
	{
		FVector2D SegA;
		FVector2D SegB;
		double Radius = 0.0;
		double Margin = 0.0;
		double ApproachBand = 0.0;
	};
}

URammsRobotClearanceProcessor::URammsRobotClearanceProcessor()
	: AvatarQuery(*this)
	, CrowdQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
	// After agents have moved this frame, before their actors follow them.
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
}

void URammsRobotClearanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	AvatarQuery.AddTagRequirement<FRammsPlayerAvatarTag>(EMassFragmentPresence::All);
	AvatarQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FMassAvoidanceColliderFragment>(EMassFragmentAccess::ReadOnly);
	AvatarQuery.AddRequirement<FRammsAvatarFootprintFragment>(EMassFragmentAccess::ReadOnly);

	CrowdQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	CrowdQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	CrowdQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	CrowdQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	CrowdQuery.AddRequirement<FRammsRobotContactFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

bool URammsRobotClearanceProcessor::ResolveAgainstFootprint(const FVector2D& SegA, const FVector2D& SegB, const double Clearance,
	const double ApproachBand, FVector2D& InOutPosition, FVector2D& InOutVelocity, bool* bOutContact)
{
	const FVector2D AB = SegB - SegA;
	const double LengthSq = AB.SizeSquared();
	const double T = LengthSq > UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(InOutPosition - SegA, AB) / LengthSq, 0.0, 1.0) : 0.0;
	const FVector2D Closest = SegA + AB * T;

	FVector2D Normal = InOutPosition - Closest;
	double Distance = Normal.Size();
	if (Distance >= Clearance + ApproachBand)
	{
		return false;
	}
	if (Distance > UE_KINDA_SMALL_NUMBER)
	{
		Normal /= Distance;
	}
	else
	{
		// On the footprint's axis: leave sideways from it.
		Normal = LengthSq > UE_SMALL_NUMBER ? FVector2D(-AB.Y, AB.X).GetSafeNormal() : FVector2D(1.0, 0.0);
		Distance = 0.0;
	}

	bool bChanged = false;
	if (Distance < Clearance)
	{
		InOutPosition = Closest + Normal * Clearance;
		Distance = Clearance;
		bChanged = true;
		if (bOutContact != nullptr)
		{
			*bOutContact = true;
		}
	}

	const double Inward = FVector2D::DotProduct(InOutVelocity, Normal);
	if (Inward < 0.0)
	{
		// Share of the approach to redirect: all of it at the boundary, none at the
		// outer edge of the approach band.
		const double Share = ApproachBand > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(1.0 - (Distance - Clearance) / ApproachBand, 0.0, 1.0) : 1.0;
		if (Share > 0.0)
		{
			const double Speed = InOutVelocity.Size();
			FVector2D Tangent(-Normal.Y, Normal.X);
			if (FVector2D::DotProduct(InOutVelocity, Tangent) < 0.0)
			{
				Tangent = -Tangent;
			}
			FVector2D Redirected = InOutVelocity - Normal * (Inward * Share) - Tangent * (Inward * Share);
			// Keep the walking speed: the agent goes around, it does not speed up or stall.
			Redirected = Redirected.GetSafeNormal() * Speed;
			InOutVelocity = Redirected;
			bChanged = true;
		}
	}
	return bChanged;
}

void URammsRobotClearanceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TArray<FRammsFootprint, TInlineAllocator<4>> Footprints;
	AvatarQuery.ForEachEntityChunk(Context, [&Footprints](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassAvoidanceColliderFragment> ColliderList = Context.GetFragmentView<FMassAvoidanceColliderFragment>();
		const TConstArrayView<FRammsAvatarFootprintFragment> FootprintList = Context.GetFragmentView<FRammsAvatarFootprintFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			const FRammsAvatarFootprintFragment& Settings = FootprintList[EntityIt];
			if (!Settings.bKeepCrowdClear)
			{
				continue;
			}
			const FTransform& Transform = TransformList[EntityIt].GetTransform();
			const FVector2D Center(Transform.GetLocation());
			const FVector2D Forward = FVector2D(Transform.GetRotation().GetForwardVector()).GetSafeNormal();
			const FMassAvoidanceColliderFragment& Collider = ColliderList[EntityIt];

			FRammsFootprint& Footprint = Footprints.AddDefaulted_GetRef();
			double HalfLength = 0.0;
			if (Collider.Type == EMassColliderType::Pill)
			{
				const FMassPillCollider Pill = Collider.GetPillCollider();
				Footprint.Radius = Pill.Radius;
				HalfLength = Pill.HalfLength;
			}
			else
			{
				Footprint.Radius = Collider.GetCircleCollider().Radius;
			}
			Footprint.SegA = Center - Forward * HalfLength;
			Footprint.SegB = Center + Forward * HalfLength;
			Footprint.Margin = Settings.ClearanceMarginCm;
			Footprint.ApproachBand = Settings.ApproachBandCm;
		}
	});

	if (Footprints.IsEmpty())
	{
		return;
	}

	const float Now = Context.GetWorld() != nullptr ? static_cast<float>(Context.GetWorld()->GetTimeSeconds()) : 0.0f;
	CrowdQuery.ForEachEntityChunk(Context, [&Footprints, Now](FMassExecutionContext& Context)
	{
		const TArrayView<FRammsRobotContactFragment> ContactList = Context.GetMutableFragmentView<FRammsRobotContactFragment>();
		const TArrayView<FTransformFragment> TransformList = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			FTransform& Transform = TransformList[EntityIt].GetMutableTransform();
			FVector Location = Transform.GetLocation();
			FVector& Velocity = VelocityList[EntityIt].Value;
			FVector2D Position(Location);
			FVector2D Velocity2D(Velocity);

			bool bChanged = false;
			bool bContact = false;
			for (const FRammsFootprint& Footprint : Footprints)
			{
				const double Clearance = Footprint.Radius + RadiusList[EntityIt].Radius + Footprint.Margin;
				bChanged |= ResolveAgainstFootprint(Footprint.SegA, Footprint.SegB, Clearance, Footprint.ApproachBand, Position, Velocity2D, &bContact);
			}
			if (!ContactList.IsEmpty())
			{
				FRammsRobotContactFragment& Contact = ContactList[EntityIt];
				if (bContact)
				{
					if (!Contact.bInContact)
					{
						++Contact.ContactCount;
					}
					Contact.LastContactTime = Now;
				}
				Contact.bInContact = bContact;
			}
			if (bChanged)
			{
				Location.X = Position.X;
				Location.Y = Position.Y;
				Transform.SetLocation(Location);
				Velocity.X = Velocity2D.X;
				Velocity.Y = Velocity2D.Y;
			}
		}
	});
}
