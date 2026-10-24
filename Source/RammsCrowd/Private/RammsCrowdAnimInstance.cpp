// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RammsCrowdAnimComponent.h"
#include "RammsCrowdAnimSet.h"
#include "RammsCrowdLog.h"

namespace RammsCrowdAnim
{
	bool BracketDirection(const TConstArrayView<float> Directions, const float AngleDegrees, int32& OutA, int32& OutB, float& OutAlphaB)
	{
		OutA = OutB = INDEX_NONE;
		OutAlphaB = 0.0f;
		if (Directions.IsEmpty())
		{
			return false;
		}
		const float Angle = FRotator3f::NormalizeAxis(AngleDegrees);
		float BelowGap = TNumericLimits<float>::Max();
		float AboveGap = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Directions.Num(); ++Index)
		{
			// Positive: the clip's direction is below (counter-clockwise of) the angle.
			const float Delta = FRotator3f::NormalizeAxis(Angle - Directions[Index]);
			if (Delta >= 0.0f && Delta < BelowGap)
			{
				BelowGap = Delta;
				OutA = Index;
			}
			if (Delta <= 0.0f && -Delta < AboveGap)
			{
				AboveGap = -Delta;
				OutB = Index;
			}
		}
		if (OutA == INDEX_NONE)
		{
			OutA = OutB;
			BelowGap = 0.0f;
		}
		if (OutB == INDEX_NONE)
		{
			OutB = OutA;
			AboveGap = 0.0f;
		}
		const float Span = BelowGap + AboveGap;
		OutAlphaB = (OutA == OutB || Span < UE_KINDA_SMALL_NUMBER) ? 0.0f : BelowGap / Span;
		return true;
	}

	void FGaitTrack::Init(const UAnimSequence* Sequence)
	{
		TArray<float> LeftFoot;
		if (Sequence != nullptr)
		{
			static const FName LeftMarker(TEXT("L"));
			for (const FAnimSyncMarker& Marker : Sequence->AuthoredSyncMarkers)
			{
				if (Marker.MarkerName == LeftMarker)
				{
					LeftFoot.Add(Marker.Time);
				}
			}
		}
		InitFromMarkers(LeftFoot, Sequence != nullptr ? Sequence->GetPlayLength() : 0.0f);
	}

	void FGaitTrack::InitFromMarkers(const TConstArrayView<float> LeftFootTimes, const float InLength)
	{
		Length = FMath::Max(InLength, UE_KINDA_SMALL_NUMBER);
		CycleStarts.Reset();
		for (const float Time : LeftFootTimes)
		{
			if (Time >= 0.0f && Time < Length)
			{
				CycleStarts.AddUnique(Time);
			}
		}
		CycleStarts.Sort();
		if (CycleStarts.IsEmpty())
		{
			CycleStarts.Add(0.0f);
		}
		Cycle = 0;
	}

	float FGaitTrack::CycleDuration() const
	{
		const float Start = CycleStarts[Cycle];
		const float End = Cycle + 1 < CycleStarts.Num() ? CycleStarts[Cycle + 1] : CycleStarts[0] + Length;
		return FMath::Max(End - Start, 0.01f);
	}

	float FGaitTrack::TimeAt(const float Phase) const
	{
		float Time = CycleStarts[Cycle] + FMath::Clamp(Phase, 0.0f, 1.0f) * CycleDuration();
		if (Time >= Length)
		{
			Time -= Length;
		}
		return FMath::Clamp(Time, 0.0f, Length);
	}

	void FGaitTrack::NextCycle()
	{
		Cycle = (Cycle + 1) % CycleStarts.Num();
	}
}

namespace
{
	using FBoneWeights = TArray<float, TInlineAllocator<256>>;

	struct FMaskRoot
	{
		FName Bone;
		float Weight;
	};

	float Smoothing(const float DeltaSeconds, const float TimeConstant)
	{
		return TimeConstant <= UE_KINDA_SMALL_NUMBER ? 1.0f : 1.0f - FMath::Exp(-DeltaSeconds / TimeConstant);
	}

	float LoopTime(const UAnimSequence* Sequence, const float Time)
	{
		const float Length = Sequence != nullptr ? Sequence->GetPlayLength() : 0.0f;
		return Length > UE_KINDA_SMALL_NUMBER ? FMath::Fmod(FMath::Max(Time, 0.0f), Length) : 0.0f;
	}

	void EvaluateSample(const FRammsCrowdPoseSample& Sample, FPoseContext& Out)
	{
		FAnimationPoseData PoseData(Out);
		const FAnimExtractContext Context(static_cast<double>(Sample.Time), /*bExtractRootMotion*/ true, FDeltaTimeRecord(), /*bLooping*/ true);
		Sample.Sequence->GetAnimationPose(PoseData, Context);
		// Pedestrians are moved by Mass, not root motion. Clips authored without root
		// motion enabled would otherwise carry the root away and pop at every loop.
		const FCompactPoseBoneIndex Root(0);
		Out.Pose[Root] = Out.Pose.GetRefPose(Root);
	}

	/** Per compact-pose bone weights: each listed bone sets the weight of its whole subtree, unless a descendant is listed too. */
	void BuildMask(const FCompactPose& Pose, const TConstArrayView<FMaskRoot> Roots, FBoneWeights& OutMask)
	{
		const FBoneContainer& Bones = Pose.GetBoneContainer();
		const int32 NumBones = Pose.GetNumBones();
		TArray<float, TInlineAllocator<256>> Explicit;
		Explicit.Init(-1.0f, NumBones);
		for (const FMaskRoot& Root : Roots)
		{
			const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Root.Bone);
			if (MeshIndex == INDEX_NONE)
			{
				continue;
			}
			const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Compact != INDEX_NONE)
			{
				Explicit[Compact.GetInt()] = Root.Weight;
			}
		}
		OutMask.SetNumUninitialized(NumBones);
		for (const FCompactPoseBoneIndex Index : Pose.ForEachBoneIndex())
		{
			const int32 I = Index.GetInt();
			if (Explicit[I] >= 0.0f)
			{
				OutMask[I] = Explicit[I];
				continue;
			}
			const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
			OutMask[I] = Parent != INDEX_NONE ? OutMask[Parent.GetInt()] : 0.0f;
		}
	}

	void LayerBones(FCompactPose& Base, const FCompactPose& Layer, const TConstArrayView<float> Weights)
	{
		for (const FCompactPoseBoneIndex Index : Base.ForEachBoneIndex())
		{
			const float Weight = Weights[Index.GetInt()];
			if (Weight >= 1.0f - UE_KINDA_SMALL_NUMBER)
			{
				Base[Index] = Layer[Index];
			}
			else if (Weight > UE_KINDA_SMALL_NUMBER)
			{
				Base[Index].BlendWith(Layer[Index], Weight);
			}
		}
	}

	/**
	 * Turns the head toward a component-space yaw/pitch, spread down the neck and upper
	 * spine. Each bone rotates by its share about component-space axes, children
	 * keeping their local transforms, so the shares add up at the head.
	 */
	void ApplyLookAt(FCompactPose& Pose, const float YawDegrees, const float PitchDegrees, const float Alpha)
	{
		if (Alpha <= UE_KINDA_SMALL_NUMBER || (FMath::Abs(YawDegrees) < 0.1f && FMath::Abs(PitchDegrees) < 0.1f))
		{
			return;
		}
		static const FName Names[] = { TEXT("spine_04"), TEXT("spine_05"), TEXT("neck_01"), TEXT("neck_02"), TEXT("head") };
		static constexpr float Shares[] = { 0.1f, 0.15f, 0.25f, 0.2f, 0.3f };

		const FBoneContainer& Bones = Pose.GetBoneContainer();
		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(Pose);

		const float Yaw = FMath::DegreesToRadians(YawDegrees * Alpha);
		const float Pitch = FMath::DegreesToRadians(PitchDegrees * Alpha);
		// The face looks along component +Y; after the yaw its sideways axis is the yawed X.
		const FVector PitchAxis = FQuat(FVector::UpVector, Yaw).RotateVector(FVector::XAxisVector);

		struct FChange
		{
			FCompactPoseBoneIndex Index;
			FQuat NewLocalRotation;
		};
		TArray<FChange, TInlineAllocator<5>> Changes;
		for (int32 Bone = 0; Bone < static_cast<int32>(UE_ARRAY_COUNT(Names)); ++Bone)
		{
			const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Names[Bone]);
			if (MeshIndex == INDEX_NONE)
			{
				continue;
			}
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Index == INDEX_NONE)
			{
				continue;
			}
			const FCompactPoseBoneIndex Parent = Pose.GetParentBoneIndex(Index);
			const FQuat ParentRotation = Parent != INDEX_NONE ? ComponentPose.GetComponentSpaceTransform(Parent).GetRotation() : FQuat::Identity;
			const FQuat Delta = FQuat(PitchAxis, Pitch * Shares[Bone]) * FQuat(FVector::UpVector, Yaw * Shares[Bone]);
			const FQuat LocalDelta = ParentRotation.Inverse() * Delta * ParentRotation;
			Changes.Add({ Index, (LocalDelta * Pose[Index].GetRotation()).GetNormalized() });
		}
		for (const FChange& Change : Changes)
		{
			Pose[Change.Index].SetRotation(Change.NewLocalRotation);
		}
	}
}

FAnimInstanceProxy* URammsCrowdAnimInstance::CreateAnimInstanceProxy()
{
	return new FRammsCrowdAnimProxy(this);
}

void URammsCrowdAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	const AActor* Owner = GetOwningActor();
	URammsCrowdAnimComponent* Comp = Owner != nullptr ? Owner->FindComponentByClass<URammsCrowdAnimComponent>() : nullptr;
	Bridge = Comp;
	if (AnimSet == nullptr && Comp != nullptr)
	{
		AnimSet = Comp->AnimSet;
	}
	// Re-pick on the next update (instances are re-created when the mesh changes).
	SelectedEntityKey = MAX_uint64;
	LocoClips.Reset();
}

void URammsCrowdAnimInstance::SelectStyle(const uint32 Seed)
{
	Random.Initialize(static_cast<int32>(Seed));

	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const FString MeshName = Mesh != nullptr && Mesh->GetSkeletalMeshAsset() != nullptr ? Mesh->GetSkeletalMeshAsset()->GetName() : FString();
	const FRammsCrowdBodyAnims* Body = AnimSet->FindBody(MeshName);

	LocoClips.Reset();
	DirectionalAngles.Reset();
	IdleSequence = nullptr;
	CarrySequence = nullptr;
	CarryLeft = CarryRight = CarryHead = 0.0f;
	ReactionSequence = nullptr;
	bSeenStimulus = false;
	WalkName = IdleName = CarryName = NAME_None;
	BodyKey.Reset();
	if (Body == nullptr)
	{
		ApplyAccessory(nullptr, INDEX_NONE);
		return;
	}
	BodyKey = Body->BodyKey;

	auto MakeClip = [](const FRammsCrowdLocomotionClip& Source)
	{
		FLocoClip Clip;
		Clip.Sequence = Source.Sequence;
		Clip.SpeedCmS = FMath::Max(Source.SpeedCmS, 1.0f);
		Clip.DirectionDegrees = Source.DirectionDegrees;
		Clip.Gait.Init(Source.Sequence);
		return Clip;
	};

	TArray<const FRammsCrowdLocomotionClip*, TInlineAllocator<8>> Walks;
	for (const FRammsCrowdLocomotionClip& Walk : Body->Walks)
	{
		if (Walk.Sequence != nullptr)
		{
			Walks.Add(&Walk);
		}
	}
	const int32 WalkPick = Walks.IsEmpty() ? INDEX_NONE : Random.RandRange(0, Walks.Num() - 1);
	LocoClips.Add(WalkPick != INDEX_NONE ? MakeClip(*Walks[WalkPick]) : FLocoClip());
	LocoClips.Add(MakeClip(Body->QuickWalk));
	DirectionalAngles.Add(0.0f);
	for (const FRammsCrowdLocomotionClip& Directional : AnimSet->Directionals)
	{
		if (Directional.Sequence != nullptr)
		{
			LocoClips.Add(MakeClip(Directional));
			DirectionalAngles.Add(Directional.DirectionDegrees);
		}
	}
	WalkName = LocoClips[0].Sequence != nullptr ? LocoClips[0].Sequence->GetFName() : NAME_None;
	// Start everyone at a different point of their stride.
	GaitPhase = Random.FRand();

	TArray<const UAnimSequence*, TInlineAllocator<8>> Idles;
	for (const UAnimSequence* Idle : Body->Idles)
	{
		if (Idle != nullptr)
		{
			Idles.Add(Idle);
		}
	}
	if (!Idles.IsEmpty())
	{
		IdleSequence = Idles[Random.RandRange(0, Idles.Num() - 1)];
		IdleTime = Random.FRand() * IdleSequence->GetPlayLength();
		IdleName = IdleSequence->GetFName();
	}

	const FRammsCrowdCarryStyle* Carry = nullptr;
	int32 PropIndex = INDEX_NONE;
	const float CarryRoll = Random.FRand();
	float TotalWeight = 0.0f;
	for (const FRammsCrowdCarryStyle& Style : Body->Carries)
	{
		TotalWeight += Style.Pose != nullptr ? Style.Weight : 0.0f;
	}
	const float StyleRoll = Random.FRand() * TotalWeight;
	const int32 PropRoll = Random.RandRange(0, 1 << 16);
	if (CarryRoll < AnimSet->CarryChance && TotalWeight > 0.0f)
	{
		float Accumulated = 0.0f;
		for (const FRammsCrowdCarryStyle& Style : Body->Carries)
		{
			if (Style.Pose == nullptr)
			{
				continue;
			}
			Accumulated += Style.Weight;
			Carry = &Style;
			if (StyleRoll <= Accumulated)
			{
				break;
			}
		}
	}
	if (Carry != nullptr)
	{
		CarrySequence = Carry->Pose;
		CarryTime = Random.FRand() * CarrySequence->GetPlayLength();
		CarryLeft = Carry->Arms != ERammsCrowdCarryArms::Right ? 1.0f : 0.0f;
		CarryRight = Carry->Arms != ERammsCrowdCarryArms::Left ? 1.0f : 0.0f;
		CarryHead = Carry->bLayerHead ? 1.0f : 0.0f;
		CarryName = Carry->Name;
		PropIndex = Carry->Props.IsEmpty() ? INDEX_NONE : PropRoll % Carry->Props.Num();
	}
	ApplyAccessory(Carry, PropIndex);
	bWatchesRobot = Random.FRand() < AnimSet->WatchRobotChance;
}

void URammsCrowdAnimInstance::ApplyAccessory(const FRammsCrowdCarryStyle* Style, const int32 PropIndex)
{
	AActor* Owner = GetOwningActor();
	USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (Owner == nullptr || Mesh == nullptr || AnimSet == nullptr)
	{
		return;
	}
	UStaticMesh* Prop = Style != nullptr && Style->Props.IsValidIndex(PropIndex) ? Style->Props[PropIndex].Get() : nullptr;

	static const FName CreatedName(TEXT("RammsCrowdAccessory"));
	UStaticMeshComponent* Accessory = nullptr;
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(Owner);
	for (UStaticMeshComponent* Candidate : StaticMeshes)
	{
		if (Candidate->GetFName() == AnimSet->AccessoryComponentName || Candidate->GetFName() == CreatedName)
		{
			Accessory = Candidate;
			break;
		}
	}
	if (Accessory == nullptr)
	{
		if (Prop == nullptr)
		{
			return;
		}
		Accessory = NewObject<UStaticMeshComponent>(Owner, CreatedName);
		Accessory->SetupAttachment(Mesh);
		Accessory->RegisterComponent();
	}

	Accessory->SetStaticMesh(Prop);
	if (Prop != nullptr)
	{
		// A carried prop never collides: it would push the robot just like the body did.
		Accessory->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Accessory->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Style->Socket);
		Accessory->SetVisibility(true);
	}
}

void URammsCrowdAnimInstance::PlayReaction()
{
	StartReaction();
}

void URammsCrowdAnimInstance::StartReaction()
{
	if (AnimSet == nullptr)
	{
		return;
	}
	TArray<const UAnimSequence*, TInlineAllocator<8>> Reactions;
	for (const UAnimSequence* Reaction : AnimSet->Reactions)
	{
		if (Reaction != nullptr)
		{
			Reactions.Add(Reaction);
		}
	}
	if (Reactions.IsEmpty())
	{
		return;
	}
	ReactionSequence = Reactions[FMath::RandRange(0, Reactions.Num() - 1)];
	ReactionTime = 0.0f;
	LastReactionWorldTime = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	++ReactionCount;
}

void URammsCrowdAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	URammsCrowdAnimComponent* Comp = Bridge.Get();
	if (Comp == nullptr)
	{
		const AActor* Owner = GetOwningActor();
		Comp = Owner != nullptr ? Owner->FindComponentByClass<URammsCrowdAnimComponent>() : nullptr;
		Bridge = Comp;
	}
	if (AnimSet == nullptr && Comp != nullptr)
	{
		AnimSet = Comp->AnimSet;
	}
	if (Comp == nullptr || AnimSet == nullptr)
	{
		Frame = FRammsCrowdAnimFrame();
		return;
	}

	const FMassEntityHandle Entity = Comp->GetRepresentedEntity();
	if (Entity.IsValid() ? Entity.AsNumber() != SelectedEntityKey : LocoClips.IsEmpty())
	{
		SelectedEntityKey = Entity.IsValid() ? Entity.AsNumber() : MAX_uint64 - 1;
		const uint32 Seed = Entity.IsValid()
			? HashCombine(GetTypeHash(Entity.Index), GetTypeHash(Entity.SerialNumber))
			: GetTypeHash(GetOwningActor());
		SelectStyle(Seed);
		// New entity: snap the filters instead of easing in from the previous one's state.
		SmoothedSpeed = Comp->MassSpeed;
		SmoothedDirection = Comp->DirectionAngleDegrees;
		LookAlpha = 0.0f;
	}

	const float SpeedAlpha = Smoothing(DeltaSeconds, AnimSet->SpeedSmoothingSeconds);
	SmoothedSpeed += (Comp->MassSpeed - SmoothedSpeed) * SpeedAlpha;
	if (Comp->MassSpeed > AnimSet->IdleBlendStartCmS * 0.5f)
	{
		const float DirectionAlpha = Smoothing(DeltaSeconds, AnimSet->DirectionSmoothingSeconds);
		SmoothedDirection = FRotator3f::NormalizeAxis(SmoothedDirection + FRotator3f::NormalizeAxis(Comp->DirectionAngleDegrees - SmoothedDirection) * DirectionAlpha);
	}

	Frame = FRammsCrowdAnimFrame();
	UpdateLocomotion(DeltaSeconds, SmoothedSpeed, SmoothedDirection);

	if (IdleSequence != nullptr)
	{
		IdleTime = LoopTime(IdleSequence, IdleTime + DeltaSeconds);
		Frame.Idle = { IdleSequence, IdleTime, 1.0f };
	}
	if (CarrySequence != nullptr)
	{
		CarryTime = LoopTime(CarrySequence, CarryTime + DeltaSeconds);
		Frame.Carry = { CarrySequence, CarryTime, 1.0f };
		Frame.CarryLeftArm = CarryLeft;
		Frame.CarryRightArm = CarryRight;
	}

	UpdateReaction(DeltaSeconds);
	UpdateLookAt(DeltaSeconds);
	// Someone looking down at their phone looks up when they notice the robot.
	Frame.CarryHead = CarryHead * (1.0f - LookAlpha);
}

void URammsCrowdAnimInstance::UpdateLocomotion(const float DeltaSeconds, const float Speed, const float DirectionDegrees)
{
	LocomotionAlpha = FMath::SmoothStep(AnimSet->IdleBlendStartCmS, FMath::Max(AnimSet->IdleBlendEndCmS, AnimSet->IdleBlendStartCmS + 1.0f), Speed);
	Frame.LocomotionAlpha = LocomotionAlpha;
	if (LocoClips.IsEmpty() || LocoClips[0].Sequence == nullptr)
	{
		Frame.LocomotionAlpha = 0.0f;
		return;
	}

	TArray<float, TInlineAllocator<16>> Weights;
	Weights.SetNumZeroed(LocoClips.Num());
	int32 A = 0;
	int32 B = 0;
	float AlphaB = 0.0f;
	RammsCrowdAnim::BracketDirection(DirectionalAngles, DirectionDegrees, A, B, AlphaB);
	// Direction index 0 is the forward walk (clip 0); directional d is clip d + 1.
	const auto ClipOf = [](const int32 Direction) { return Direction == 0 ? 0 : Direction + 1; };
	Weights[ClipOf(A)] += 1.0f - AlphaB;
	Weights[ClipOf(B)] += AlphaB;

	// Above walking speed, hand the forward share over to the brisk walk.
	const FLocoClip& Walk = LocoClips[0];
	const FLocoClip& Quick = LocoClips[1];
	if (Quick.Sequence != nullptr && Weights[0] > 0.0f && Quick.SpeedCmS > Walk.SpeedCmS + 1.0f)
	{
		const float Share = FMath::Clamp((Speed - Walk.SpeedCmS) / (Quick.SpeedCmS - Walk.SpeedCmS), 0.0f, 1.0f);
		Weights[1] = Weights[0] * Share;
		Weights[0] *= 1.0f - Share;
	}

	float ClipSpeed = 0.0f;
	float CycleSeconds = 0.0f;
	for (int32 Index = 0; Index < LocoClips.Num(); ++Index)
	{
		if (Weights[Index] > 0.0f)
		{
			ClipSpeed += Weights[Index] * LocoClips[Index].SpeedCmS;
			CycleSeconds += Weights[Index] * LocoClips[Index].Gait.CycleDuration();
		}
	}
	// The rate that puts the feet on the ground at the agent's speed: no sliding.
	LocomotionPlayRate = FMath::Clamp(Speed / FMath::Max(ClipSpeed, 1.0f), AnimSet->MinPlayRate, FMath::Max(AnimSet->MaxPlayRate, AnimSet->MinPlayRate));
	FootSpeedCmS = LocomotionAlpha > 0.0f ? ClipSpeed * LocomotionPlayRate : 0.0f;

	if (LocomotionAlpha > 0.0f)
	{
		GaitPhase += DeltaSeconds * LocomotionPlayRate / FMath::Max(CycleSeconds, 0.05f);
		while (GaitPhase >= 1.0f)
		{
			GaitPhase -= 1.0f;
			for (FLocoClip& Clip : LocoClips)
			{
				Clip.Gait.NextCycle();
			}
		}
	}
	for (int32 Index = 0; Index < LocoClips.Num(); ++Index)
	{
		if (Weights[Index] > 1.0e-3f && LocoClips[Index].Sequence != nullptr)
		{
			Frame.Locomotion.Add({ LocoClips[Index].Sequence, LocoClips[Index].Gait.TimeAt(GaitPhase), Weights[Index] });
		}
	}
}

void URammsCrowdAnimInstance::UpdateReaction(const float DeltaSeconds)
{
	const URammsCrowdAnimComponent* Comp = Bridge.Get();
	const UWorld* World = GetWorld();
	if (Comp == nullptr || World == nullptr)
	{
		return;
	}
	if (!bSeenStimulus)
	{
		// First look at this entity: remember its history, react only to what happens next.
		SeenContactCount = Comp->RobotContactCount;
		SeenStartleTime = Comp->LastStartleTime;
		bSeenStimulus = true;
	}
	bool bTrigger = false;
	if (Comp->RobotContactCount != SeenContactCount)
	{
		SeenContactCount = Comp->RobotContactCount;
		bTrigger = true;
	}
	if (Comp->LastStartleTime != SeenStartleTime)
	{
		SeenStartleTime = Comp->LastStartleTime;
		bTrigger |= AnimSet->bReactToStartle;
	}
	if (bTrigger && World->GetTimeSeconds() - LastReactionWorldTime >= AnimSet->ReactionCooldownSeconds)
	{
		StartReaction();
	}

	if (ReactionSequence == nullptr)
	{
		return;
	}
	ReactionTime += DeltaSeconds;
	const float Length = ReactionSequence->GetPlayLength();
	if (ReactionTime >= Length)
	{
		ReactionSequence = nullptr;
		return;
	}
	const float In = AnimSet->ReactionBlendInSeconds > UE_KINDA_SMALL_NUMBER ? ReactionTime / AnimSet->ReactionBlendInSeconds : 1.0f;
	const float Out = AnimSet->ReactionBlendOutSeconds > UE_KINDA_SMALL_NUMBER ? (Length - ReactionTime) / AnimSet->ReactionBlendOutSeconds : 1.0f;
	Frame.Reaction = { ReactionSequence, ReactionTime, AnimSet->ReactionWeight * FMath::Clamp(FMath::Min(In, Out), 0.0f, 1.0f) };
}

void URammsCrowdAnimInstance::UpdateLookAt(const float DeltaSeconds)
{
	const URammsCrowdAnimComponent* Comp = Bridge.Get();
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (Comp == nullptr || Mesh == nullptr)
	{
		return;
	}

	FVector WorldDirection = FVector::ZeroVector;
	const bool bNoticed = Comp->StimulusZone == ERammsStimulusZone::React || Comp->StimulusZone == ERammsStimulusZone::Startle
		|| (Comp->StimulusZone == ERammsStimulusZone::Aware && bWatchesRobot);
	if (Comp->bHasLookAtTarget && ReactionSequence == nullptr)
	{
		WorldDirection = Comp->LookAtDirection;
	}
	else if (Comp->bHasStimulus && (bNoticed || ReactionSequence != nullptr))
	{
		// Look at the robot's user, at about seated head height.
		static const FName Head(TEXT("head"));
		const FVector Eye = Mesh->GetSocketLocation(Head);
		WorldDirection = Comp->StimulusLocation + FVector(0.0, 0.0, AnimSet->LookAtStimulusHeightCm) - Eye;
	}

	bool bHasTarget = !WorldDirection.IsNearlyZero();
	float TargetYaw = 0.0f;
	float TargetPitch = 0.0f;
	if (bHasTarget)
	{
		const FVector Local = Mesh->GetComponentTransform().InverseTransformVectorNoScale(WorldDirection.GetSafeNormal());
		// The mesh faces component +Y; positive yaw turns toward its right (-X).
		TargetYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Local.X, Local.Y)));
		TargetPitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FVector2D(Local.X, Local.Y).Size())));
		// Nobody cranks their head round to look behind them.
		bHasTarget = FMath::Abs(TargetYaw) <= AnimSet->LookAtMaxYawDegrees + 40.0f;
		TargetYaw = FMath::Clamp(TargetYaw, -AnimSet->LookAtMaxYawDegrees, AnimSet->LookAtMaxYawDegrees);
		TargetPitch = FMath::Clamp(TargetPitch, -AnimSet->LookAtMaxPitchDegrees, AnimSet->LookAtMaxPitchDegrees);
	}

	const float Alpha = Smoothing(DeltaSeconds, AnimSet->LookAtBlendSeconds);
	LookAlpha += ((bHasTarget ? 1.0f : 0.0f) - LookAlpha) * Alpha;
	if (bHasTarget)
	{
		LookYaw += (TargetYaw - LookYaw) * Alpha;
		LookPitch += (TargetPitch - LookPitch) * Alpha;
	}
	Frame.LookYawDegrees = LookYaw;
	Frame.LookPitchDegrees = LookPitch;
	Frame.LookAlpha = LookAlpha;
}

void FRammsCrowdAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, const float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const URammsCrowdAnimInstance* Instance = Cast<URammsCrowdAnimInstance>(InAnimInstance))
	{
		Frame = Instance->Frame;
	}
}

bool FRammsCrowdAnimProxy::Evaluate(FPoseContext& Output)
{
	// Locomotion: a running weighted average of the direction/speed clips.
	bool bHavePose = false;
	float Accumulated = 0.0f;
	FPoseContext Scratch(Output);
	for (const FRammsCrowdPoseSample& Sample : Frame.Locomotion)
	{
		if (Sample.Sequence == nullptr || Sample.Weight <= 0.0f)
		{
			continue;
		}
		if (!bHavePose)
		{
			EvaluateSample(Sample, Output);
			Accumulated = Sample.Weight;
			bHavePose = true;
			continue;
		}
		EvaluateSample(Sample, Scratch);
		const float Previous = Accumulated;
		Accumulated += Sample.Weight;
		FAnimationPoseData Blended(Output);
		const FAnimationPoseData Added(Scratch);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Blended, Added, Previous / Accumulated);
	}

	// Standing <-> walking.
	if (Frame.Idle.Sequence != nullptr && (!bHavePose || Frame.LocomotionAlpha < 1.0f))
	{
		if (!bHavePose || Frame.LocomotionAlpha <= 0.0f)
		{
			EvaluateSample(Frame.Idle, Output);
		}
		else
		{
			EvaluateSample(Frame.Idle, Scratch);
			FAnimationPoseData Blended(Output);
			const FAnimationPoseData Idle(Scratch);
			FAnimationRuntime::BlendTwoPosesTogetherInPlace(Blended, Idle, Frame.LocomotionAlpha);
		}
		bHavePose = true;
	}
	if (!bHavePose)
	{
		Output.ResetToRefPose();
		return true;
	}

	FBoneWeights Weights;
	FBoneWeights Mask;

	// What they carry: the hold pose over just the holding arm(s), and the head for a phone.
	if (Frame.Carry.Sequence != nullptr && (Frame.CarryLeftArm > 0.0f || Frame.CarryRightArm > 0.0f || Frame.CarryHead > 0.0f))
	{
		EvaluateSample(Frame.Carry, Scratch);
		const FMaskRoot Parts[] = {
			{ FName(TEXT("clavicle_l")), Frame.CarryLeftArm },
			{ FName(TEXT("clavicle_r")), Frame.CarryRightArm },
			{ FName(TEXT("neck_01")), Frame.CarryHead },
		};
		BuildMask(Output.Pose, Parts, Weights);
		LayerBones(Output.Pose, Scratch.Pose, Weights);
	}

	// A flinch over the upper body, fading in up the spine.
	if (Frame.Reaction.Sequence != nullptr && Frame.Reaction.Weight > 0.0f)
	{
		EvaluateSample(Frame.Reaction, Scratch);
		const FMaskRoot Spine[] = {
			{ FName(TEXT("spine_01")), 0.3f * Frame.Reaction.Weight },
			{ FName(TEXT("spine_02")), 0.5f * Frame.Reaction.Weight },
			{ FName(TEXT("spine_03")), 0.75f * Frame.Reaction.Weight },
			{ FName(TEXT("spine_04")), Frame.Reaction.Weight },
		};
		BuildMask(Output.Pose, Spine, Mask);
		LayerBones(Output.Pose, Scratch.Pose, Mask);
	}

	ApplyLookAt(Output.Pose, Frame.LookYawDegrees, Frame.LookPitchDegrees, Frame.LookAlpha);
	Output.Pose.NormalizeRotations();
	return true;
}
