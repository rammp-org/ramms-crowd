// Copyright (c) RAMMP. All rights reserved.

#include "RammsSeatedPoseAnimInstance.h"

#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "TwoBoneIK.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "RammsCrowdLog.h"
#include "RammsSeatComponent.h"
#include "EngineUtils.h"
#include "RammsCrowdAnimComponent.h"

FAnimInstanceProxy* URammsSeatedPoseAnimInstance::CreateAnimInstanceProxy()
{
	return new FRammsSeatedPoseProxy(this);
}

void URammsSeatedPoseAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	USkeletalMeshComponent* Comp = GetSkelMeshComponent();

	// Self-defense: the mesh must refresh bones even while unrendered. Assembled
	// characters hide the leader body mesh under clothing, so it may NEVER count
	// as rendered — with the default tick option its bones freeze at ref pose and
	// the visible follower meshes copy that freeze, regardless of what Evaluate
	// produces. Late assembly passes can reset this, so reassert it here (this
	// runs on every anim-instance recreation).
	if (Comp != nullptr)
	{
		Comp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		// City Sample base meshes carry a post-process AnimBP (m_/f_*_animbp)
		// designed for CitySample's own crowd animation system. With that system
		// absent it ignores the input pose and outputs reference pose — evaluated
		// AFTER this instance, it silently discards the seated pose. It must be
		// disabled through the setter (a raw property write skips the anim
		// re-initialization needed for it to take effect).
		Comp->SetDisablePostProcessBlueprint(true);
	}

	// Each occupant glances on its own schedule.
	MotionRandom.Initialize(static_cast<int32>(GetUniqueID()));
	NextGlance = MotionRandom.FRandRange(1.0f, 6.0f);
	bHaveSeatHistory = false;

	const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	for (USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr; Parent != nullptr; Parent = Parent->GetAttachParent())
	{
		if (URammsSeatComponent* SeatComp = Cast<URammsSeatComponent>(Parent))
		{
			Seat = SeatComp;
			break;
		}
	}
	if (BoneOffsets.Num() > 0)
	{
		return;
	}
	for (const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr; Parent != nullptr; Parent = Parent->GetAttachParent())
	{
		if (const URammsSeatComponent* PoseSeat = Cast<URammsSeatComponent>(Parent))
		{
			BoneOffsets = PoseSeat->PoseBoneOffsets;
			UE_LOG(LogRammsCrowd, Verbose, TEXT("[SeatedPose] re-acquired %d offsets from seat '%s' (mesh '%s')"), BoneOffsets.Num(), *PoseSeat->GetPathName(), *GetNameSafe(Comp));
			return;
		}
	}
	UE_LOG(LogRammsCrowd, Warning, TEXT("[SeatedPose] NO seat found in attach chain of '%s' (owner '%s') — offsets stay empty"),
		*GetNameSafe(Comp), Owner ? *Owner->GetPathName() : TEXT("<null>"));
}

void URammsSeatedPoseAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	Fit = FRammsSeatFit();
	const URammsSeatComponent* SeatComp = Seat.Get();
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (SeatComp == nullptr || Mesh == nullptr)
	{
		return;
	}
	// Seat space -> the occupant mesh's component space.
	const FTransform SeatToComponent = SeatComp->GetComponentTransform().GetRelativeTransform(Mesh->GetComponentTransform());

	FRammsSeatBodyAdjust Adjust;
	const FString MeshName = Mesh->GetSkeletalMeshAsset() != nullptr ? Mesh->GetSkeletalMeshAsset()->GetName() : FString();
	for (const TPair<FString, FRammsSeatBodyAdjust>& Body : SeatComp->BodyAdjustments)
	{
		if (!Body.Key.IsEmpty() && MeshName.Contains(Body.Key))
		{
			Adjust.Pelvis += Body.Value.Pelvis;
			Adjust.OnBody += Body.Value.OnBody;
		}
	}
	if (SeatComp->bAlignPelvis)
	{
		Fit.bAlignPelvis = true;
		Fit.PelvisLocation = SeatToComponent.TransformPosition(SeatComp->PelvisLocation + Adjust.Pelvis);
	}

	const FRammsSeatLimbTarget* Targets[4] = { &SeatComp->LeftHand, &SeatComp->RightHand, &SeatComp->LeftFoot, &SeatComp->RightFoot };
	static const FName Chains[4][3] = {
		{ TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l") },
		{ TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r") },
		{ TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l") },
		{ TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r") },
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FRammsSeatLimbTarget& Target = *Targets[Index];
		FRammsSeatLimbSolve& Limb = Fit.Limbs[Index];
		Limb.bEnabled = Target.bEnabled && Target.Weight > 0.0f;
		if (!Limb.bEnabled)
		{
			continue;
		}
		Limb.Upper = Chains[Index][0];
		Limb.Lower = Chains[Index][1];
		Limb.End = Chains[Index][2];
		Limb.RelativeToBone = Target.RelativeToBone;
		Limb.Location = Target.RelativeToBone.IsNone() ? SeatToComponent.TransformPosition(Target.Location) : SeatToComponent.TransformVector(Target.Location + Adjust.OnBody);
		Limb.Pole = SeatToComponent.TransformPosition(Target.PoleLocation);
		Limb.bKeepEndRotation = Target.EndRotation == ERammsSeatLimbEndRotation::KeepPose;
		Limb.Weight = Target.Weight;
	}

	UpdateMotion(*SeatComp, SeatToComponent, DeltaSeconds);
}

void URammsSeatedPoseAnimInstance::StepSway(float& InOutAngle, float& InOutRate, const float Acceleration, const float TrunkControl, const float DeltaSeconds)
{
	// A seated trunk as an inverted pendulum held by muscle: weaker control means a
	// lower, less damped natural frequency. The chair's acceleration pushes the trunk
	// the other way over a ~45 cm lever.
	const float Control = FMath::Clamp(TrunkControl, 0.0f, 1.0f);
	const float Omega = UE_TWO_PI * FMath::Lerp(0.6f, 1.6f, Control);
	const float Zeta = FMath::Lerp(0.3f, 0.8f, Control);
	const float Drive = -Acceleration / 45.0f;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / 0.005f), 1, 40);
	const float H = DeltaSeconds / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const float Accel = -Omega * Omega * InOutAngle - 2.0f * Zeta * Omega * InOutRate + Drive;
		InOutRate += Accel * H;
		InOutAngle += InOutRate * H;
	}
	const float Limit = FMath::DegreesToRadians(25.0f);
	if (FMath::Abs(InOutAngle) > Limit)
	{
		InOutAngle = FMath::Clamp(InOutAngle, -Limit, Limit);
		InOutRate = 0.0f;
	}
}

void URammsSeatedPoseAnimInstance::UpdateMotion(const URammsSeatComponent& SeatComp, const FTransform& SeatToComponent, const float DeltaSeconds)
{
	FRammsSeatMotion& Motion = Fit.Motion;
	Motion.SeatForward = SeatToComponent.TransformVectorNoScale(FVector::ForwardVector);
	Motion.SeatRight = SeatToComponent.TransformVectorNoScale(FVector::RightVector);
	Motion.SeatUp = SeatToComponent.TransformVectorNoScale(FVector::UpVector);
	if (DeltaSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	Time += DeltaSeconds;
	const FTransform SeatWorld = SeatComp.GetComponentTransform();

	// The chair's motion, seat space, lightly filtered (physics jitter would read as shaking).
	const FVector Location = SeatWorld.GetLocation();
	const float Yaw = static_cast<float>(SeatWorld.Rotator().Yaw);
	if (!bHaveSeatHistory || (Location - PrevSeatLocation).Size() > 200.0f)
	{
		// First frame, or a teleport: start at rest.
		bHaveSeatHistory = true;
		PrevSeatLocation = Location;
		PrevSeatYaw = Yaw;
		SeatVelocity = SeatAccel = FVector::ZeroVector;
		SeatYawRate = 0.0f;
	}
	const FVector RawVelocity = (Location - PrevSeatLocation) / DeltaSeconds;
	const FVector PrevVelocity = SeatVelocity;
	SeatVelocity += (RawVelocity - SeatVelocity) * (1.0f - FMath::Exp(-DeltaSeconds / 0.05f));
	const FVector RawAccel = (SeatVelocity - PrevVelocity) / DeltaSeconds;
	SeatAccel += (RawAccel - SeatAccel) * (1.0f - FMath::Exp(-DeltaSeconds / 0.08f));
	const float RawYawRate = FRotator3f::NormalizeAxis(Yaw - PrevSeatYaw) / DeltaSeconds;
	SeatYawRate += (RawYawRate - SeatYawRate) * (1.0f - FMath::Exp(-DeltaSeconds / 0.1f));
	PrevSeatLocation = Location;
	PrevSeatYaw = Yaw;
	const FVector LocalAccel = SeatWorld.InverseTransformVectorNoScale(SeatAccel);
	const FVector LocalVelocity = SeatWorld.InverseTransformVectorNoScale(SeatVelocity);

	// Passive lean.
	if (SeatComp.bPassiveSway)
	{
		StepSway(SwayPitch, SwayPitchRate, static_cast<float>(LocalAccel.X), SeatComp.TrunkControl, DeltaSeconds);
		StepSway(SwayRoll, SwayRollRate, static_cast<float>(LocalAccel.Y), SeatComp.TrunkControl, DeltaSeconds);
	}
	else
	{
		SwayPitch = SwayPitchRate = SwayRoll = SwayRollRate = 0.0f;
	}
	SwayPitchDegrees = FMath::RadiansToDegrees(SwayPitch) * SeatComp.SwayAmount;
	SwayRollDegrees = FMath::RadiansToDegrees(SwayRoll) * SeatComp.SwayAmount;
	Motion.SwayPitch = SwayPitchDegrees;
	Motion.SwayRoll = SwayRollDegrees;
	Motion.HeadRighting = SeatComp.bPassiveSway ? 0.8f * FMath::Clamp(SeatComp.TrunkControl, 0.0f, 1.0f) : 0.0f;

	// Voluntary upper-body motion.
	const float Voluntary = SeatComp.bVoluntaryMotion ? SeatComp.VoluntaryAmount : 0.0f;
	Motion.Breath = Voluntary * FMath::Sin(Time * UE_TWO_PI * 0.25f);
	Motion.TrunkYaw = Voluntary * 1.5f * FMath::PerlinNoise1D(Time * 0.15f);

	float TargetYaw = 0.0f;
	float TargetPitch = 0.0f;
	bool bTarget = false;
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	static const FName HeadBone(TEXT("head"));
	const FVector Eye = Mesh != nullptr ? Mesh->GetSocketLocation(HeadBone) : Location;
	auto LookTowards = [&](const FVector& WorldPoint)
	{
		const FVector Local = SeatWorld.InverseTransformVectorNoScale(WorldPoint - Eye);
		TargetYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)));
		TargetPitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FVector2D(Local.X, Local.Y).Size())));
		bTarget = true;
	};

	FVector Override;
	if (SeatComp.GetLookAtOverride(Override))
	{
		LookTowards(Override);
	}
	else if (Voluntary > 0.0f)
	{
		// People nearby, now and then.
		if (SeatComp.bLookAtPeople && Time >= NextPeopleCheck)
		{
			NextPeopleCheck = Time + 0.5f;
			if (!WatchedPerson.IsValid() && Time >= PeopleCooldownUntil && GetWorld() != nullptr)
			{
				AActor* Nearest = nullptr;
				double NearestDistance = 350.0;
				for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				{
					if (It->FindComponentByClass<URammsCrowdAnimComponent>() == nullptr)
					{
						continue;
					}
					const FVector Local = SeatWorld.InverseTransformPosition(It->GetActorLocation());
					const double Distance = FVector2D(Local.X, Local.Y).Size();
					if (Local.X > -50.0 && Distance < NearestDistance)
					{
						NearestDistance = Distance;
						Nearest = *It;
					}
				}
				if (Nearest != nullptr && MotionRandom.FRand() < 0.5f)
				{
					WatchedPerson = Nearest;
					WatchUntil = Time + MotionRandom.FRandRange(1.5f, 3.0f);
					PeopleCooldownUntil = WatchUntil + MotionRandom.FRandRange(3.0f, 7.0f);
				}
			}
		}
		if (WatchedPerson.IsValid() && Time < WatchUntil)
		{
			LookTowards(WatchedPerson->GetActorLocation() + FVector(0.0, 0.0, 160.0));
			// Someone standing right by the chair: a glance at their face, not craning up at it.
			TargetPitch = FMath::Min(TargetPitch, 15.0f);
		}
		else
		{
			WatchedPerson = nullptr;
			const float Speed = static_cast<float>(FVector2D(LocalVelocity.X, LocalVelocity.Y).Size());
			// Turning on the spot also moves the seat (it sits off the pivot); that is not reversing.
			const bool bSpinning = FMath::Abs(SeatYawRate) > 15.0f && LocalVelocity.X > -40.0;
			if (SeatComp.bLookWhereGoing && LocalVelocity.X < -15.0 && !bSpinning)
			{
				// Reversing: over the shoulder on the side the chair is swinging toward.
				const float Side = SeatYawRate > 5.0f ? -1.0f : 1.0f;
				TargetYaw = Side * FMath::Clamp(-static_cast<float>(LocalVelocity.X) / 40.0f, 0.0f, 1.0f) * 60.0f;
				TargetPitch = -10.0f;
				bTarget = true;
			}
			else if (SeatComp.bLookWhereGoing && (Speed > 15.0f || FMath::Abs(SeatYawRate) > 10.0f))
			{
				// Into the turn, about half a second ahead.
				TargetYaw = FMath::Clamp(SeatYawRate * 0.5f, -40.0f, 40.0f);
				TargetPitch = -5.0f;
				bTarget = true;
			}
			else
			{
				// Idle: glance around every few seconds.
				if (Time >= NextGlance)
				{
					GlanceYaw = MotionRandom.FRandRange(-35.0f, 35.0f);
					GlancePitch = MotionRandom.FRandRange(-10.0f, 5.0f);
					GlanceUntil = Time + MotionRandom.FRandRange(1.0f, 2.5f);
					NextGlance = GlanceUntil + MotionRandom.FRandRange(3.0f, 8.0f);
				}
				if (Time < GlanceUntil)
				{
					TargetYaw = GlanceYaw;
					TargetPitch = GlancePitch;
					bTarget = true;
				}
			}
		}
	}
	TargetYaw = FMath::Clamp(TargetYaw, -70.0f, 70.0f);
	TargetPitch = FMath::Clamp(TargetPitch, -30.0f, 25.0f);
	if (!bTarget)
	{
		TargetYaw = TargetPitch = 0.0f;
	}
	const float LookAlpha = 1.0f - FMath::Exp(-DeltaSeconds / 0.3f);
	LookYaw += (TargetYaw - LookYaw) * LookAlpha;
	LookPitch += (TargetPitch - LookPitch) * LookAlpha;
	LookYawDegrees = LookYaw;
	LookPitchDegrees = LookPitch;
	Motion.LookYaw = LookYaw;
	Motion.LookPitch = LookPitch;
}

void URammsSeatedPoseAnimInstance::SolveLimb(FTransform& Upper, FTransform& Lower, FTransform& End, const FVector& Effector, const FVector& Pole, const bool bKeepEndRotation)
{
	const FVector UpperPos = Upper.GetLocation();
	const FVector OldLower = Lower.GetLocation();
	const FVector OldEnd = End.GetLocation();
	FVector NewLower;
	FVector NewEnd;
	AnimationCore::SolveTwoBoneIK(UpperPos, OldLower, OldEnd, Pole, Effector, NewLower, NewEnd, /*bAllowStretching*/ false, 1.0, 1.0);

	// Swing the upper bone onto its new direction, then the lower bone (already carried
	// by the upper's swing) onto its own.
	const FQuat SwingUpper = FQuat::FindBetweenNormals((OldLower - UpperPos).GetSafeNormal(), (NewLower - UpperPos).GetSafeNormal());
	const FVector CarriedLowerDir = SwingUpper.RotateVector(OldEnd - OldLower).GetSafeNormal();
	const FQuat SwingLower = FQuat::FindBetweenNormals(CarriedLowerDir, (NewEnd - NewLower).GetSafeNormal());

	Upper.SetRotation((SwingUpper * Upper.GetRotation()).GetNormalized());
	Lower.SetLocation(NewLower);
	Lower.SetRotation((SwingLower * SwingUpper * Lower.GetRotation()).GetNormalized());
	End.SetLocation(NewEnd);
	if (!bKeepEndRotation)
	{
		End.SetRotation((SwingLower * SwingUpper * End.GetRotation()).GetNormalized());
	}
}

void FRammsSeatedPoseProxy::PreUpdate(UAnimInstance* InAnimInstance, const float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	Offsets.Reset();
	if (const URammsSeatedPoseAnimInstance* Instance = Cast<URammsSeatedPoseAnimInstance>(InAnimInstance))
	{
		Fit = Instance->Fit;
		Offsets.Reserve(Instance->BoneOffsets.Num());
		for (const TPair<FName, FRotator>& Pair : Instance->BoneOffsets)
		{
			Offsets.Emplace(Pair.Key, Pair.Value.Quaternion());
		}
	}
}

namespace
{
	struct FMotionShare
	{
		const TCHAR* Bone;
		float Sway;
		float Righting;
		float Breath;
		float Look;
		float Trunk;
	};

	/**
	 * Rotates the spine, neck and head by their shares of each motion about the seat's
	 * axes in component space; children keep their local transforms, so shares add up
	 * toward the head. The pelvis and legs are never touched.
	 */
	void ApplyMotion(FCompactPose& Pose, const FRammsSeatMotion& Motion)
	{
		static const FMotionShare Shares[] = {
			{ TEXT("spine_01"), 0.25f, 0.0f, 0.0f, 0.0f, 0.2f },
			{ TEXT("spine_02"), 0.25f, 0.0f, 0.0f, 0.0f, 0.2f },
			{ TEXT("spine_03"), 0.2f, 0.0f, 0.6f, 0.0f, 0.2f },
			{ TEXT("spine_04"), 0.15f, 0.0f, 0.0f, 0.1f, 0.2f },
			{ TEXT("spine_05"), 0.15f, 0.0f, -0.4f, 0.15f, 0.2f },
			{ TEXT("neck_01"), 0.0f, -0.5f, 0.0f, 0.25f, 0.0f },
			{ TEXT("neck_02"), 0.0f, -0.25f, 0.0f, 0.2f, 0.0f },
			{ TEXT("head"), 0.0f, -0.25f, 0.0f, 0.3f, 0.0f },
		};
		const bool bAny = Motion.SwayPitch != 0.0f || Motion.SwayRoll != 0.0f || Motion.Breath != 0.0f || Motion.LookYaw != 0.0f
			|| Motion.LookPitch != 0.0f || Motion.TrunkYaw != 0.0f;
		if (!bAny)
		{
			return;
		}
		const FBoneContainer& Bones = Pose.GetBoneContainer();
		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(Pose);

		const FVector Forward = Motion.SeatForward.GetSafeNormal();
		const FVector Right = Motion.SeatRight.GetSafeNormal();
		const FVector Up = Motion.SeatUp.GetSafeNormal();
		const float Deg = UE_PI / 180.0f;
		// The head's sideways axis after its turn, for looking up and down.
		const FVector LookRight = FQuat(Up, Motion.LookYaw * Deg).RotateVector(Right);
		const float SwayTotal = 1.0f;

		struct FChange
		{
			FCompactPoseBoneIndex Index;
			FQuat Rotation;
		};
		TArray<FChange, TInlineAllocator<8>> Changes;
		for (const FMotionShare& Share : Shares)
		{
			const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Share.Bone));
			if (MeshIndex == INDEX_NONE)
			{
				continue;
			}
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Index == INDEX_NONE)
			{
				continue;
			}
			// Lean: forward is +pitch about the seat's right axis; toward the right is
			// -roll about its forward axis. Righting takes part of it back at the neck.
			const float LeanShare = Share.Sway * SwayTotal + Share.Righting * Motion.HeadRighting;
			const FQuat Lean = FQuat(Right, Motion.SwayPitch * LeanShare * Deg) * FQuat(Forward, -Motion.SwayRoll * LeanShare * Deg);
			const FQuat Breath = FQuat(Right, 0.8f * Motion.Breath * Share.Breath * Deg);
			// Looking: + yaw turns right; looking up is -pitch about the turned right axis.
			const FQuat Look = FQuat(LookRight, -Motion.LookPitch * Share.Look * Deg) * FQuat(Up, (Motion.LookYaw * Share.Look + Motion.TrunkYaw * Share.Trunk) * Deg);
			const FQuat Delta = Look * Breath * Lean;
			const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
			const FQuat ParentRotation = Parent != INDEX_NONE ? ComponentPose.GetComponentSpaceTransform(Parent).GetRotation() : FQuat::Identity;
			Changes.Add({ Index, ((ParentRotation.Inverse() * Delta * ParentRotation) * Pose[Index].GetRotation()).GetNormalized() });
		}
		for (const FChange& Change : Changes)
		{
			Pose[Change.Index].SetRotation(Change.Rotation);
		}
	}
}

bool FRammsSeatedPoseProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const TPair<FName, FQuat>& Offset : Offsets)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Offset.Key);
		if (MeshIndex == INDEX_NONE)
		{
			continue;
		}
		const FCompactPoseBoneIndex CompactIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (CompactIndex == INDEX_NONE)
		{
			continue;
		}
		FTransform& BoneTransform = Output.Pose[CompactIndex];
		BoneTransform.SetRotation(BoneTransform.GetRotation() * Offset.Value);
	}

	auto Find = [&Bones](const FName Name) -> FCompactPoseBoneIndex
	{
		const int32 MeshIndex = Name.IsNone() ? INDEX_NONE : Bones.GetPoseBoneIndexForBoneName(Name);
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};

	// Fit to the seat: shift the whole pose so the pelvis lands on the seat, then put
	// hands and feet on the chair.
	if (Fit.bAlignPelvis)
	{
		const FCompactPoseBoneIndex Pelvis = Find(TEXT("pelvis"));
		if (Pelvis != INDEX_NONE)
		{
			FCSPose<FCompactPose> ComponentPose;
			ComponentPose.InitPose(Output.Pose);
			const FVector Delta = Fit.PelvisLocation - ComponentPose.GetComponentSpaceTransform(Pelvis).GetLocation();
			// The root's local transform is its component-space transform.
			const FCompactPoseBoneIndex Root(0);
			Output.Pose[Root].AddToTranslation(Delta);
		}
	}

	ApplyMotion(Output.Pose, Fit.Motion);

	bool bAnyLimb = false;
	for (const FRammsSeatLimbSolve& Limb : Fit.Limbs)
	{
		bAnyLimb |= Limb.bEnabled;
	}
	if (bAnyLimb)
	{
		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(Output.Pose);
		for (const FRammsSeatLimbSolve& Limb : Fit.Limbs)
		{
			if (!Limb.bEnabled)
			{
				continue;
			}
			const FCompactPoseBoneIndex Upper = Find(Limb.Upper);
			const FCompactPoseBoneIndex Lower = Find(Limb.Lower);
			const FCompactPoseBoneIndex End = Find(Limb.End);
			if (Upper == INDEX_NONE || Lower == INDEX_NONE || End == INDEX_NONE)
			{
				continue;
			}
			FTransform UpperT = ComponentPose.GetComponentSpaceTransform(Upper);
			FTransform LowerT = ComponentPose.GetComponentSpaceTransform(Lower);
			FTransform EndT = ComponentPose.GetComponentSpaceTransform(End);
			FVector Target = Limb.Location;
			if (!Limb.RelativeToBone.IsNone())
			{
				const FCompactPoseBoneIndex Relative = Find(Limb.RelativeToBone);
				if (Relative == INDEX_NONE)
				{
					continue;
				}
				Target += ComponentPose.GetComponentSpaceTransform(Relative).GetLocation();
			}
			const FVector Effector = FMath::Lerp(EndT.GetLocation(), Target, Limb.Weight);
			URammsSeatedPoseAnimInstance::SolveLimb(UpperT, LowerT, EndT, Effector, Limb.Pole, Limb.bKeepEndRotation);
			const FBoneTransform Solved[] = { FBoneTransform(Upper, UpperT), FBoneTransform(Lower, LowerT), FBoneTransform(End, EndT) };
			ComponentPose.SafeSetCSBoneTransforms(Solved);
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(ComponentPose, Output.Pose);
	}
	return true;
}
