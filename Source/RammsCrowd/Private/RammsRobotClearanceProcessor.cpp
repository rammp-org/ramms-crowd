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
		FVector2D Velocity = FVector2D::ZeroVector;
		double Radius = 0.0;
		double Margin = 0.0;
		double ApproachBand = 0.0;
		bool bAnticipatePassing = false;
		FRammsPassingSettings Passing;
	};

	FVector2D ClosestOnSegment(const FVector2D& SegA, const FVector2D& SegB, const FVector2D& Point)
	{
		const FVector2D AB = SegB - SegA;
		const double LengthSq = AB.SizeSquared();
		const double T = LengthSq > UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - SegA, AB) / LengthSq, 0.0, 1.0) : 0.0;
		return SegA + AB * T;
	}
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
	AvatarQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);

	CrowdQuery.AddTagRequirement<FRammsCrowdMemberTag>(EMassFragmentPresence::All);
	CrowdQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	CrowdQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	CrowdQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	CrowdQuery.AddRequirement<FRammsRobotContactFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	CrowdQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	// Mass movement re-derives velocity from the desired velocity every frame, so turns
	// and slowdowns have to go there to last beyond this frame.
	CrowdQuery.AddRequirement<FMassDesiredMovementFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

bool URammsRobotClearanceProcessor::PlanPass(const FVector2D& SegA, const FVector2D& SegB, const FVector2D& RobotVelocity,
	const double Clearance, const FRammsPassingSettings& Settings, const double DesiredSpeed, const double DeltaSeconds,
	const FVector2D& Position, FVector2D& InOutVelocity, int8& InOutSide)
{
	const FVector2D Relative = InOutVelocity - RobotVelocity;
	const double RelativeSpeedSq = Relative.SizeSquared();
	const FVector2D Center = (SegA + SegB) * 0.5;
	const double Closing = FVector2D::DotProduct(Position - Center, Relative);
	if (Closing >= 0.0 || RelativeSpeedSq < 25.0)
	{
		// Moving away (or not moving relative to the robot): the pass is over.
		InOutSide = 0;
		return false;
	}

	bool bChanged = false;

	// Squeezing past: ease off toward a share of the walking speed near the robot.
	// A clamp, not a scale, so it does not compound frame over frame.
	const double Gap = (Position - ClosestOnSegment(SegA, SegB, Position)).Size() - Clearance;
	double Speed = InOutVelocity.Size();
	if (Settings.SlowdownFraction < 1.0 && DesiredSpeed > UE_KINDA_SMALL_NUMBER && Gap < Settings.SlowdownBandCm)
	{
		const double Share = Settings.SlowdownBandCm > UE_KINDA_SMALL_NUMBER
			? FMath::Lerp(Settings.SlowdownFraction, 1.0, FMath::Clamp(Gap / Settings.SlowdownBandCm, 0.0, 1.0))
			: Settings.SlowdownFraction;
		const double MaxSpeed = DesiredSpeed * Share;
		if (Speed > MaxSpeed)
		{
			InOutVelocity *= MaxSpeed / Speed;
			Speed = MaxSpeed;
			bChanged = true;
		}
	}

	// Where the agent will be, relative to the robot, at closest approach.
	const double TimeToClosest = -Closing / RelativeSpeedSq;
	if (TimeToClosest > Settings.LookaheadSeconds || Speed < UE_KINDA_SMALL_NUMBER)
	{
		return bChanged;
	}
	const FVector2D Predicted = Position + Relative * TimeToClosest;
	const FVector2D Offset = Predicted - ClosestOnSegment(SegA, SegB, Predicted);
	const FVector2D Direction = Relative / FMath::Sqrt(RelativeSpeedSq);
	// The agent's right, relative to its motion past the robot (X forward, Y right).
	const FVector2D Right(-Direction.Y, Direction.X);
	const double Lateral = FVector2D::DotProduct(Offset, Right);
	const double Planned = Clearance + Settings.ComfortCm;
	if (InOutSide == 0)
	{
		if (Offset.Size() >= Planned)
		{
			return bChanged;
		}
		// Head-on and early enough: keep right. Otherwise stay on the side it is already on.
		const bool bHeadOn = FMath::Abs(Lateral) < Settings.HeadOnShare * Planned;
		InOutSide = (Settings.bPreferRight && bHeadOn && TimeToClosest >= Settings.MinHeadOnDecisionSeconds) ? 1
			: (Lateral >= 0.0 ? 1 : -1);
	}

	// Open the predicted miss to the planned distance on the chosen side, spread over the time left.
	const double Needed = InOutSide * Planned - Lateral;
	if (Needed * InOutSide <= 0.0)
	{
		return bChanged;
	}
	const double MaxSideways = Settings.MaxSidewaysShare * Speed;
	const double Sideways = FMath::Clamp(Needed / FMath::Max(TimeToClosest, 0.4), -MaxSideways, MaxSideways);
	// Turn toward that, no faster than a person would, and do not speed up.
	const FVector2D Wanted = (InOutVelocity + Right * Sideways).GetSafeNormal();
	const double Current = FMath::Atan2(InOutVelocity.Y, InOutVelocity.X);
	const double Turn = FMath::Clamp(FMath::FindDeltaAngleRadians(Current, FMath::Atan2(Wanted.Y, Wanted.X)),
		-FMath::DegreesToRadians(Settings.MaxTurnDegreesPerSecond) * DeltaSeconds,
		FMath::DegreesToRadians(Settings.MaxTurnDegreesPerSecond) * DeltaSeconds);
	InOutVelocity = FVector2D(FMath::Cos(Current + Turn), FMath::Sin(Current + Turn)) * Speed;
	return true;
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
		const TConstArrayView<FMassVelocityFragment> AvatarVelocityList = Context.GetFragmentView<FMassVelocityFragment>();
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
			Footprint.Velocity = AvatarVelocityList.IsEmpty() ? FVector2D::ZeroVector : FVector2D(AvatarVelocityList[EntityIt].Value);
			Footprint.bAnticipatePassing = Settings.bAnticipatePassing;
			Footprint.Passing.bPreferRight = Settings.bPassOnRight;
			Footprint.Passing.LookaheadSeconds = Settings.PassingLookaheadSeconds;
			Footprint.Passing.ComfortCm = Settings.PassingComfortCm;
			Footprint.Passing.SlowdownFraction = Settings.PassingSlowdownFraction;
			Footprint.Passing.SlowdownBandCm = Settings.PassingSlowdownBandCm;
		}
	});

	if (Footprints.IsEmpty())
	{
		return;
	}

	const float Now = Context.GetWorld() != nullptr ? static_cast<float>(Context.GetWorld()->GetTimeSeconds()) : 0.0f;
	const double DeltaSeconds = Context.GetDeltaTimeSeconds();
	CrowdQuery.ForEachEntityChunk(Context, [&Footprints, Now, DeltaSeconds](FMassExecutionContext& Context)
	{
		const TArrayView<FRammsRobotContactFragment> ContactList = Context.GetMutableFragmentView<FRammsRobotContactFragment>();
		const TArrayView<FTransformFragment> TransformList = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();
		const TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();
		const TArrayView<FMassDesiredMovementFragment> DesiredMovementList = Context.GetMutableFragmentView<FMassDesiredMovementFragment>();
		for (FMassExecutionContext::FEntityIterator EntityIt = Context.CreateEntityIterator(); EntityIt; ++EntityIt)
		{
			FTransform& Transform = TransformList[EntityIt].GetMutableTransform();
			FVector Location = Transform.GetLocation();
			FVector& Velocity = VelocityList[EntityIt].Value;
			FVector2D Position(Location);
			FVector2D Velocity2D(Velocity);

			bool bChanged = false;
			bool bContact = false;
			int8 PassSide = ContactList.IsEmpty() ? 0 : ContactList[EntityIt].PassSide;
			const double DesiredSpeed = MoveTargetList.IsEmpty() ? Velocity2D.Size() : static_cast<double>(MoveTargetList[EntityIt].DesiredSpeed.Get());
			for (const FRammsFootprint& Footprint : Footprints)
			{
				const double Clearance = Footprint.Radius + RadiusList[EntityIt].Radius + Footprint.Margin;
				if (Footprint.bAnticipatePassing)
				{
					// Velocity only: the change shows up as a turn over the next frames.
					bChanged |= PlanPass(Footprint.SegA, Footprint.SegB, Footprint.Velocity, Clearance, Footprint.Passing, DesiredSpeed,
						DeltaSeconds, Position, Velocity2D, PassSide);
				}
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
				Contact.PassSide = PassSide;
			}
			if (bChanged)
			{
				Location.X = Position.X;
				Location.Y = Position.Y;
				Transform.SetLocation(Location);
				Velocity.X = Velocity2D.X;
				Velocity.Y = Velocity2D.Y;
				if (!DesiredMovementList.IsEmpty())
				{
					FVector& Desired = DesiredMovementList[EntityIt].DesiredVelocity;
					Desired.X = Velocity2D.X;
					Desired.Y = Velocity2D.Y;
				}
			}
		}
	});
}
