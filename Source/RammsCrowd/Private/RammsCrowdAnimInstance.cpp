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
#include "Animation/TrajectoryTypes.h"
#include "GameFramework/Actor.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchRole.h"
#include "RammsCrowdAnimComponent.h"
#include "RammsCrowdAnimSet.h"
#include "RammsCrowdLog.h"
#include "RammsSeatedPoseAnimInstance.h"

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

/** Per-pedestrian motion matching state: the pose history the search reads, the clips playing, and recent path. */
struct FRammsCrowdMotionMatching
{
	struct FLayer
	{
		const UAnimSequence* Sequence = nullptr;
		float Time = 0.0f;
		float Weight = 0.0f;
		bool bLooping = false;
	};

	struct FPastSample
	{
		double Time = 0.0;
		FVector Position = FVector::ZeroVector;
		FQuat Facing = FQuat::Identity;
	};

	enum class EPhase : uint8
	{
		Stand,
		Start,
		Move,
		Stop,
	};

	UE::PoseSearch::FPoseHistory History;
	bool bResetHistory = true;
	EPhase Phase = EPhase::Stand;
	float PhaseSeconds = 0.0f;
	/** A clip has been picked since the phase began (start / stop clips then play out). */
	bool bPhaseClipChosen = false;
	/** Playing clips, oldest first; the last is the current one, fading in over the others. */
	TArray<FLayer, TInlineAllocator<4>> Layers;
	float SinceSearch = UE_BIG_NUMBER;
	double Clock = 0.0;
	TArray<FPastSample> Past;

	FRammsCrowdMotionMatching()
	{
		History.Initialize_AnyThread(/*NumPoses*/ 12, /*SamplingInterval*/ 0.04f);
	}

	void Reset()
	{
		bResetHistory = true;
		Layers.Reset();
		SinceSearch = UE_BIG_NUMBER;
		Past.Reset();
		Phase = EPhase::Stand;
		PhaseSeconds = 0.0f;
		bPhaseClipChosen = false;
	}
};

namespace
{
	/** Ground speed of a clip's root motion around Time, cm/s. */
	float ClipRootSpeed(const UAnimSequence* Sequence, const float Time)
	{
		const float Length = Sequence->GetPlayLength();
		const float Start = FMath::Clamp(Time - 0.1f, 0.0f, Length);
		const float End = FMath::Clamp(Time + 0.1f, 0.0f, Length);
		if (End - Start < 0.02f)
		{
			return 0.0f;
		}
		const FTransform A = Sequence->ExtractRootTrackTransform(FAnimExtractContext(static_cast<double>(Start)), nullptr);
		const FTransform B = Sequence->ExtractRootTrackTransform(FAnimExtractContext(static_cast<double>(End)), nullptr);
		return static_cast<float>((B.GetLocation() - A.GetLocation()).Size2D() / (End - Start));
	}

	/** Cumulative ground distance of a clip's root motion, sampled at 30 Hz (cached per clip; game thread). */
	const TArray<float>& RootDistanceTable(const UAnimSequence* Sequence)
	{
		static TMap<TObjectKey<UAnimSequence>, TArray<float>> Cache;
		TArray<float>& Table = Cache.FindOrAdd(TObjectKey<UAnimSequence>(Sequence));
		if (Table.IsEmpty() && Sequence != nullptr)
		{
			const float Length = Sequence->GetPlayLength();
			const int32 Count = FMath::Max(2, FMath::CeilToInt(Length * 30.0f) + 1);
			FVector Previous = Sequence->ExtractRootTrackTransform(FAnimExtractContext(0.0), nullptr).GetLocation();
			float Sum = 0.0f;
			Table.Add(0.0f);
			for (int32 I = 1; I < Count; ++I)
			{
				const FVector At = Sequence->ExtractRootTrackTransform(FAnimExtractContext(static_cast<double>(FMath::Min(I / 30.0f, Length))), nullptr).GetLocation();
				Sum += static_cast<float>((At - Previous).Size2D());
				Previous = At;
				Table.Add(Sum);
			}
		}
		return Table;
	}

	FQuat YawOnly(const FQuat& Rotation)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(Rotation.Rotator().Yaw));
	}
}

bool URammsCrowdAnimInstance::UpdateMotionMatching(const float DeltaSeconds)
{
	const URammsCrowdAnimComponent* Comp = Bridge.Get();
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (Comp == nullptr || Mesh == nullptr || !AnimSet->bUseMotionMatching || AnimSet->MotionMatching.IsEmpty())
	{
		bMotionMatching = false;
		return false;
	}
	if (!MotionMatching.IsValid())
	{
		MotionMatching = MakeShared<FRammsCrowdMotionMatching>();
	}
	FRammsCrowdMotionMatching& State = *MotionMatching;
	State.Clock += DeltaSeconds;

	// The path, in the frame the clips were captured in: the mesh component's yaw
	// (City Sample bodies face +Y in component space, like the database clips).
	const FTransform ComponentTransform = Mesh->GetComponentTransform();
	const FVector Position = ComponentTransform.GetLocation();
	const FQuat Facing = YawOnly(ComponentTransform.GetRotation());
	// The body faces the mesh's +Y (City Sample / MetaHuman / UEFN convention, as in
	// the clips), not the actor's X: the facing that means "walking along V" is V's
	// yaw plus this offset.
	const FVector BodyForward = ComponentTransform.GetRotation().RotateVector(FVector::YAxisVector).GetSafeNormal2D();
	const float MeshYawOffset = FRotator3f::NormalizeAxis(static_cast<float>(Facing.Rotator().Yaw - FMath::RadiansToDegrees(FMath::Atan2(BodyForward.Y, BodyForward.X))));
	State.Past.Add({ State.Clock, Position, Facing });
	while (State.Past.Num() > 2 && State.Clock - State.Past[1].Time > 1.0)
	{
		State.Past.RemoveAt(0);
	}

	FTransformTrajectory Trajectory;
	auto AddSample = [&Trajectory](const float Time, const FVector& At, const FQuat& Facing)
	{
		FTransformTrajectorySample& Sample = Trajectory.Samples.AddDefaulted_GetRef();
		Sample.TimeInSeconds = Time;
		Sample.Position = At;
		Sample.Facing = Facing;
	};
	// History: interpolated from the recorded path.
	for (int32 Step = 8; Step >= 1; --Step)
	{
		const double When = State.Clock - Step * 0.1;
		int32 Next = 0;
		while (Next < State.Past.Num() && State.Past[Next].Time < When)
		{
			++Next;
		}
		const FRammsCrowdMotionMatching::FPastSample& B = State.Past[FMath::Min(Next, State.Past.Num() - 1)];
		const FRammsCrowdMotionMatching::FPastSample& A = State.Past[FMath::Max(Next - 1, 0)];
		const float Alpha = B.Time > A.Time ? static_cast<float>(FMath::Clamp((When - A.Time) / (B.Time - A.Time), 0.0, 1.0)) : 0.0f;
		AddSample(-Step * 0.1f, FMath::Lerp(A.Position, B.Position, Alpha), FQuat::Slerp(A.Facing, B.Facing, Alpha));
	}
	// Prediction: from the current velocity toward the path Mass is following, at its
	// desired speed, slowing to a stop at a goal where the pedestrian will stand.
	const float StandSpeed = AnimSet->MotionMatchingStandSpeedCmS;
	FVector Velocity(Comp->MassVelocity.X, Comp->MassVelocity.Y, 0.0);
	const float Speed0 = static_cast<float>(Velocity.Size2D());
	FVector PathDirection = Comp->MassPathDirection.GetSafeNormal2D();
	if (PathDirection.IsNearlyZero())
	{
		PathDirection = Speed0 > StandSpeed ? Velocity.GetSafeNormal2D() : BodyForward;
	}
	// Standing agents may still be finishing the approach to their stand point.
	const float TargetSpeed = Comp->bMassIsMoving ? Comp->MassDesiredSpeed : (Comp->MassDistanceToStop > 0.0f ? Speed0 : 0.0f);
	if (Speed0 < StandSpeed && TargetSpeed < StandSpeed)
	{
		// Shuffling under avoidance: treat as standing rather than walking in place.
		Velocity = FVector::ZeroVector;
	}
	float Remaining = Comp->bMassStopsAtGoal ? Comp->MassDistanceToStop : UE_BIG_NUMBER;
	const float Tau = FMath::Max(AnimSet->TrajectoryResponseSeconds, 0.01f);
	const float StepDecay = FMath::Exp(-0.1f / Tau);
	float FacingYaw = static_cast<float>(Facing.Rotator().Yaw);
	FVector At = Position;
	float Speed1 = Speed0;
	AddSample(0.0f, Position, Facing);
	for (int32 Step = 1; Step <= 12; ++Step)
	{
		const float Allowed = Comp->bMassStopsAtGoal ? FMath::Min(TargetSpeed, FMath::Sqrt(2.0f * 150.0f * FMath::Max(Remaining, 0.0f))) : TargetSpeed;
		const FVector VelocityTarget = Allowed < StandSpeed ? FVector::ZeroVector : PathDirection * Allowed;
		Velocity = VelocityTarget + (Velocity - VelocityTarget) * StepDecay;
		At += Velocity * 0.1;
		Remaining -= static_cast<float>(Velocity.Size2D()) * 0.1f;
		if (Velocity.Size2D() > StandSpeed)
		{
			// Pedestrians turn to face where they walk.
			const float Target = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X))) + MeshYawOffset;
			FacingYaw += FRotator3f::NormalizeAxis(Target - FacingYaw) * (1.0f - FMath::Exp(-0.1f / 0.25f));
		}
		if (Step == 10)
		{
			Speed1 = static_cast<float>(Velocity.Size2D());
		}
		AddSample(Step * 0.1f, At, FQuat(FVector::UpVector, FMath::DegreesToRadians(FacingYaw)));
	}
	State.History.SetTrajectory(Trajectory);

	// What the pedestrian is doing picks the databases to search, like GASP's chooser.
	using EPhase = FRammsCrowdMotionMatching::EPhase;
	EPhase Want = EPhase::Move;
	if (Speed0 < StandSpeed && Speed1 < StandSpeed)
	{
		Want = EPhase::Stand;
	}
	else if (Speed1 < StandSpeed)
	{
		Want = EPhase::Stop;
	}
	else if ((State.Phase == EPhase::Stand || State.Phase == EPhase::Start) && Speed0 < 0.7f * FMath::Max(Speed1, StandSpeed) && State.PhaseSeconds < 1.0f)
	{
		Want = EPhase::Start;
	}
	// A start or stop clip, once chosen, plays out: re-searching mid-transition would
	// hop between left- and right-foot variants and tangle the feet. Stay in Stop
	// until its clip has nearly finished even once Mass reports standing.
	const bool bTransitionPlaying = (State.Phase == EPhase::Stop || State.Phase == EPhase::Start) && State.bPhaseClipChosen
		&& !State.Layers.IsEmpty() && !State.Layers.Last().bLooping
		&& State.Layers.Last().Time < State.Layers.Last().Sequence->GetPlayLength() - 0.2f;
	if (bTransitionPlaying && State.Phase == EPhase::Stop && Want == EPhase::Stand)
	{
		Want = EPhase::Stop;
	}
	// Hysteresis: once stopping, only walk on again if the pedestrian clearly carries on
	// (short wander legs otherwise flip Move <-> Stop every few tenths of a second, and
	// every flip is a cross-fade).
	if (State.Phase == EPhase::Stop && Want == EPhase::Move && Speed1 < 2.5f * StandSpeed)
	{
		Want = EPhase::Stop;
	}
	State.PhaseSeconds += DeltaSeconds;
	bool bPhaseChanged = false;
	if (Want != State.Phase && (State.PhaseSeconds > 0.25f || State.Phase == EPhase::Stand || Want == EPhase::Stop))
	{
		State.Phase = Want;
		State.PhaseSeconds = 0.0f;
		bPhaseChanged = true;
		State.bPhaseClipChosen = false;
	}
	static const FName PhaseNames[] = { TEXT("Stand"), TEXT("Start"), TEXT("Move"), TEXT("Stop") };
	MotionPhase = PhaseNames[static_cast<int32>(State.Phase)];
	const FRammsCrowdMotionDatabases& Sets = AnimSet->MotionMatching;
	TArray<const UObject*, TInlineAllocator<16>> Assets;
	auto AddSet = [&Assets](const TArray<TObjectPtr<UPoseSearchDatabase>>& Databases)
	{
		for (const UPoseSearchDatabase* Database : Databases)
		{
			if (Database != nullptr)
			{
				Assets.Add(Database);
			}
		}
	};
	switch (State.Phase)
	{
	case EPhase::Stand:
		AddSet(Sets.Stand);
		// Turning on the spot only when the pedestrian has somewhere else to face.
		if (FMath::Abs(FRotator3f::NormalizeAxis(FacingYaw - static_cast<float>(Facing.Rotator().Yaw))) > 30.0f || Assets.IsEmpty())
		{
			AddSet(Sets.TurnInPlace);
		}
		break;
	case EPhase::Start: AddSet(Sets.Start); break;
	case EPhase::Stop: AddSet(Sets.Stop); break;
	default: break;
	}
	if (Assets.IsEmpty())
	{
		AddSet(Sets.Move);
	}
	// A clip that doesn't loop is about to run out: pick what follows it.
	const bool bClipEnding = !State.Layers.IsEmpty() && !State.Layers.Last().bLooping
		&& State.Layers.Last().Time >= State.Layers.Last().Sequence->GetPlayLength() - 0.15f;

	// Search now and then (and as soon as the pose history has a pose in it).
	State.SinceSearch += DeltaSeconds;
	const bool bPeriodicSearch = State.SinceSearch >= AnimSet->MotionMatchingSearchInterval && !bTransitionPlaying;
	if (!State.History.IsEmpty() && (State.Layers.IsEmpty() || bPhaseChanged || bClipEnding || bPeriodicSearch))
	{
		State.SinceSearch = 0.0f;
		FPoseSearchContinuingProperties Continuing;
		if (!State.Layers.IsEmpty() && !bClipEnding)
		{
			Continuing.PlayingAsset = State.Layers.Last().Sequence;
			Continuing.PlayingAssetAccumulatedTime = State.Layers.Last().Time;
		}
		const UObject* Contexts[] = { this };
		const UE::PoseSearch::FRole Roles[] = { UE::PoseSearch::DefaultRole };
		const UE::PoseSearch::IPoseHistory* Histories[] = { &State.History };
		const UE::PoseSearch::FSearchResult Result = Assets.IsEmpty() ? UE::PoseSearch::FSearchResult()
			: UPoseSearchLibrary::MotionMatch(Contexts, Roles, Histories, Assets, Continuing, FPoseSearchFutureProperties(), FPoseSearchEvent());
		if (Result.IsValid() && !Result.bIsContinuingPoseSearch && Result.IsAssetTimeValid())
		{
			const UAnimSequence* Sequence = Cast<UAnimSequence>(Result.GetCurrentResultAnimationAsset());
			const float Time = Result.GetAssetTime();
			const bool bSameSpot = !State.Layers.IsEmpty() && State.Layers.Last().Sequence == Sequence && FMath::Abs(State.Layers.Last().Time - Time) < 0.2f;
			if (Sequence != nullptr && !bSameSpot)
			{
				const UE::PoseSearch::FSearchIndexAsset* IndexAsset = Result.GetSearchIndexAsset();
				if (State.Layers.Num() >= 4)
				{
					State.Layers.RemoveAt(0);
				}
				State.Layers.Add({ Sequence, Time, State.Layers.IsEmpty() ? 1.0f : 0.0f, IndexAsset != nullptr && IndexAsset->IsLooping() });
				MotionMatchedClip = Sequence->GetFName();
				++MotionMatchSwitches;
				State.bPhaseClipChosen = true;
			}
		}
	}
	if (State.Layers.IsEmpty())
	{
		// Nothing selected yet (no pose recorded): use the blended clips this frame.
		Frame.PoseHistory = &State.History;
		Frame.bResetPoseHistory = State.bResetHistory;
		State.bResetHistory = false;
		bMotionMatching = false;
		return false;
	}

	// Crossfade: the newest clip fades in, the older ones share what's left.
	FRammsCrowdMotionMatching::FLayer& Current = State.Layers.Last();
	const float BlendSeconds = AnimSet->MotionMatchingBlendSeconds;
	Current.Weight = BlendSeconds > UE_KINDA_SMALL_NUMBER ? FMath::Min(1.0f, Current.Weight + DeltaSeconds / BlendSeconds) : 1.0f;
	float Others = 0.0f;
	for (int32 Index = 0; Index < State.Layers.Num() - 1; ++Index)
	{
		Others += State.Layers[Index].Weight;
	}
	for (int32 Index = State.Layers.Num() - 2; Index >= 0; --Index)
	{
		FRammsCrowdMotionMatching::FLayer& Layer = State.Layers[Index];
		Layer.Weight = Others > UE_KINDA_SMALL_NUMBER ? Layer.Weight / Others * (1.0f - Current.Weight) : 0.0f;
		if (Layer.Weight < 0.01f)
		{
			State.Layers.RemoveAt(Index);
		}
	}

	// Keep the feet at the agent's speed: part by playing the clip faster or slower,
	// the rest by lengthening or shortening the steps (stride warping), so neither
	// shows on its own.
	const float RootSpeed = ClipRootSpeed(Current.Sequence, Current.Time);
	const float AgentSpeed = Speed0;
	LocomotionPlayRate = 1.0f;
	StrideScale = 1.0f;
	if (RootSpeed > 30.0f && AgentSpeed > 30.0f)
	{
		const float Ratio = AgentSpeed / RootSpeed;
		const float MinRate = AnimSet->MotionMatchingMinPlayRate;
		const float MaxRate = FMath::Max(AnimSet->MotionMatchingMaxPlayRate, MinRate);
		LocomotionPlayRate = FMath::Clamp(AnimSet->bStrideWarping ? FMath::Sqrt(Ratio) : Ratio, MinRate, MaxRate);
		if (AnimSet->bStrideWarping)
		{
			StrideScale = FMath::Clamp(Ratio / LocomotionPlayRate, AnimSet->MinStrideScale, FMath::Max(AnimSet->MaxStrideScale, AnimSet->MinStrideScale));
		}
	}
	FootSpeedCmS = RootSpeed * LocomotionPlayRate * StrideScale;
	FootLiftScale = AnimSet->bScaleFootLift && RootSpeed > 30.0f && AgentSpeed > 1.0f
		? FMath::Clamp(AgentSpeed / RootSpeed, AnimSet->MinFootLiftScale, 1.0f)
		: 1.0f;

	// Distance matching for stops: play the stop clip so that the distance its root
	// still travels equals the distance Mass has left to the goal, so the feet come to
	// rest where the body does instead of the lock having to drag them there.
	float CurrentAdvance = DeltaSeconds * LocomotionPlayRate;
	if (State.Phase == EPhase::Stop && Comp->bMassStopsAtGoal && !Current.bLooping)
	{
		const TArray<float>& Distances = RootDistanceTable(Current.Sequence);
		if (Distances.Num() > 2)
		{
			const float Total = Distances.Last();
			const float SampleRate = 30.0f;
			const auto DistanceAt = [&Distances, SampleRate](const float Time)
			{
				const float F = FMath::Clamp(Time * SampleRate, 0.0f, static_cast<float>(Distances.Num() - 1));
				const int32 I = FMath::Min(FMath::FloorToInt(F), Distances.Num() - 2);
				return FMath::Lerp(Distances[I], Distances[I + 1], F - I);
			};
			const float ClipLeft = Total - DistanceAt(Current.Time);
			const float ToGoal = Comp->MassDistanceToStop;
			if (ClipLeft > 1.0f && ToGoal < ClipLeft + 50.0f)
			{
				// The time at which the clip has ToGoal left to travel.
				int32 Index = 0;
				while (Index < Distances.Num() - 1 && Total - Distances[Index + 1] > ToGoal)
				{
					++Index;
				}
				const float TargetTime = (Index + 1) / SampleRate;
				CurrentAdvance = FMath::Clamp(TargetTime - Current.Time, DeltaSeconds * 0.3f, DeltaSeconds * 1.6f);
				StrideScale = 1.0f;
			}
		}
	}
	for (FRammsCrowdMotionMatching::FLayer& Layer : State.Layers)
	{
		const float Length = Layer.Sequence->GetPlayLength();
		Layer.Time += &Layer == &Current ? CurrentAdvance : DeltaSeconds * LocomotionPlayRate;
		Layer.Time = Layer.bLooping ? FMath::Fmod(Layer.Time, FMath::Max(Length, 0.01f)) : FMath::Min(Layer.Time, Length);
		Frame.Locomotion.Add({ Layer.Sequence, Layer.Time, Layer.Weight });
	}
	Frame.LocomotionAlpha = 1.0f;
	LocomotionAlpha = 1.0f;
	Frame.PoseHistory = &State.History;
	Frame.bResetPoseHistory = State.bResetHistory;
	State.bResetHistory = false;
	bMotionMatching = true;
	return true;
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
		bPendingFootReset = true;
		// New entity: snap the filters instead of easing in from the previous one's state.
		SmoothedSpeed = Comp->MassSpeed;
		SmoothedDirection = Comp->DirectionAngleDegrees;
		LookAlpha = 0.0f;
		if (MotionMatching.IsValid())
		{
			MotionMatching->Reset();
		}
	}

	const float SpeedAlpha = Smoothing(DeltaSeconds, AnimSet->SpeedSmoothingSeconds);
	SmoothedSpeed += (Comp->MassSpeed - SmoothedSpeed) * SpeedAlpha;
	if (Comp->MassSpeed > AnimSet->IdleBlendStartCmS * 0.5f)
	{
		const float DirectionAlpha = Smoothing(DeltaSeconds, AnimSet->DirectionSmoothingSeconds);
		SmoothedDirection = FRotator3f::NormalizeAxis(SmoothedDirection + FRotator3f::NormalizeAxis(Comp->DirectionAngleDegrees - SmoothedDirection) * DirectionAlpha);
	}

	Frame = FRammsCrowdAnimFrame();
	StrideScale = 1.0f;
	FootLiftScale = 1.0f;
	const bool bMotionMatched = UpdateMotionMatching(DeltaSeconds);
	if (!bMotionMatched)
	{
		UpdateLocomotion(DeltaSeconds, SmoothedSpeed, SmoothedDirection);
	}

	if (IdleSequence != nullptr && !bMotionMatched)
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

	Frame.bLockFeet = AnimSet->bLockPlantedFeet;
	Frame.StrideScale = LocomotionAlpha > 0.0f ? FMath::Lerp(1.0f, StrideScale, LocomotionAlpha) : 1.0f;
	Frame.FootLiftScale = LocomotionAlpha > 0.0f ? FMath::Lerp(1.0f, FootLiftScale, LocomotionAlpha) : 1.0f;
	Frame.AgentVelocity = Comp->MassVelocity;
	Frame.bResetFootLocks = bPendingFootReset;
	bPendingFootReset = false;

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
	// Whatever the rate range can't cover goes into the step length.
	StrideScale = AnimSet->bStrideWarping && ClipSpeed > 1.0f && Speed > 30.0f
		? FMath::Clamp(Speed / (ClipSpeed * LocomotionPlayRate), AnimSet->MinStrideScale, FMath::Max(AnimSet->MaxStrideScale, AnimSet->MinStrideScale))
		: 1.0f;
	FootSpeedCmS = LocomotionAlpha > 0.0f ? ClipSpeed * LocomotionPlayRate * StrideScale : 0.0f;
	FootLiftScale = AnimSet->bScaleFootLift && ClipSpeed > 30.0f && Speed > 1.0f
		? FMath::Clamp(Speed / ClipSpeed, AnimSet->MinFootLiftScale, 1.0f)
		: 1.0f;

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
	if (URammsCrowdAnimInstance* Instance = Cast<URammsCrowdAnimInstance>(InAnimInstance))
	{
		Frame = Instance->Frame;
		// Results of the previous evaluation, for debugging.
		Instance->LockedFeet = LockedFeet;
		Instance->FootLockCorrectionCm = FootLockCorrection;
		Instance->FootLockOverstretches = FootLockOverstretches;
		Instance->FootContactFromCurves = FootContactFromCurves;
	}
}

namespace
{
	const TCHAR* const LegChains[2][4] = {
		{ TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l") },
		{ TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r") },
	};

	FCompactPoseBoneIndex FindBone(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}

	/** Moves a leg's foot (keeping its rotation) with two-bone IK, the knee bending the way it already does. */
	void PlaceFoot(FCSPose<FCompactPose>& ComponentPose, const FCompactPoseBoneIndex Thigh, const FCompactPoseBoneIndex Calf, const FCompactPoseBoneIndex Foot, const FVector& Target)
	{
		FTransform ThighT = ComponentPose.GetComponentSpaceTransform(Thigh);
		FTransform CalfT = ComponentPose.GetComponentSpaceTransform(Calf);
		FTransform FootT = ComponentPose.GetComponentSpaceTransform(Foot);
		const FVector Mid = (ThighT.GetLocation() + FootT.GetLocation()) * 0.5;
		FVector Bend = CalfT.GetLocation() - Mid;
		if (Bend.SizeSquared() < 1.0)
		{
			Bend = FVector::YAxisVector;
		}
		const FVector Pole = CalfT.GetLocation() + Bend.GetSafeNormal() * 50.0;
		URammsSeatedPoseAnimInstance::SolveLimb(ThighT, CalfT, FootT, Target, Pole, /*bKeepEndRotation*/ true);
		TArray<FBoneTransform, TInlineAllocator<3>> Changed;
		Changed.Add(FBoneTransform(Thigh, ThighT));
		Changed.Add(FBoneTransform(Calf, CalfT));
		Changed.Add(FBoneTransform(Foot, FootT));
		ComponentPose.SafeSetCSBoneTransforms(Changed);
	}
}

void FRammsCrowdAnimProxy::AdjustFeet(FCompactPose& Pose, const FBlendedCurve& Curve)
{
	if (Frame.bResetFootLocks)
	{
		FootLocks[0] = FootLocks[1] = FFootLock();
	}
	const URammsCrowdAnimInstance* Instance = Cast<URammsCrowdAnimInstance>(GetAnimInstanceObject());
	const URammsCrowdAnimSet* Set = Instance != nullptr ? Instance->AnimSet.Get() : nullptr;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	const FCompactPoseBoneIndex Pelvis = FindBone(Bones, TEXT("pelvis"));
	if (Set == nullptr || Pelvis == INDEX_NONE)
	{
		return;
	}
	FCompactPoseBoneIndex Legs[2][4];
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Joint = 0; Joint < 4; ++Joint)
		{
			Legs[Side][Joint] = FindBone(Bones, LegChains[Side][Joint]);
			if (Legs[Side][Joint] == INDEX_NONE)
			{
				FootLocks[0] = FootLocks[1] = FFootLock();
				return;
			}
		}
	}
	const float DeltaSeconds = FMath::Max(GetDeltaSeconds(), UE_KINDA_SMALL_NUMBER);
	const FTransform& ComponentToWorld = GetComponentTransform();

	// Flat-foot heights from the reference pose (component space), once per mesh.
	if (Frame.bResetFootLocks || RefAnkleHeight <= 0.0f)
	{
		FCompactPose RefPose;
		RefPose.SetBoneContainer(&Bones);
		RefPose.ResetToRefPose();
		FCSPose<FCompactPose> RefComponentPose;
		RefComponentPose.InitPose(RefPose);
		RefAnkleHeight = static_cast<float>(RefComponentPose.GetComponentSpaceTransform(Legs[0][2]).GetLocation().Z);
		RefBallHeight = static_cast<float>(RefComponentPose.GetComponentSpaceTransform(Legs[0][3]).GetLocation().Z);
		const FVector RefAnkle = RefComponentPose.GetComponentSpaceTransform(Legs[0][2]).GetLocation();
		const FVector RefBall = RefComponentPose.GetComponentSpaceTransform(Legs[0][3]).GetLocation();
		RefFootPitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(RefBall.Z - RefAnkle.Z, FVector::Dist2D(RefBall, RefAnkle))));
	}

	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Pose);

	// Stride warping: lengthen or shorten the steps along the direction of travel so
	// the feet keep pace with the body without playing the clip faster or slower.
	const float Stride = Frame.StrideScale;
	if (FMath::Abs(Stride - 1.0f) > 0.01f && Frame.AgentVelocity.SizeSquared2D() > 400.0)
	{
		const FVector Direction = ComponentToWorld.InverseTransformVectorNoScale(Frame.AgentVelocity).GetSafeNormal2D();
		const FVector Hips = ComponentPose.GetComponentSpaceTransform(Pelvis).GetLocation();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector FootAt = ComponentPose.GetComponentSpaceTransform(Legs[Side][2]).GetLocation();
			const double Along = FVector::DotProduct(FootAt - Hips, Direction);
			PlaceFoot(ComponentPose, Legs[Side][0], Legs[Side][1], Legs[Side][2], FootAt + Direction * (Along * (Stride - 1.0f)));
		}
	}

	// Foot lift scaling: pull the foot's height and heel / toe rotation toward flat for a
	// pedestrian walking slower than the clip, as a slow walk lifts the feet less.
	const float LiftScale = Frame.FootLiftScale;
	if (LiftScale < 0.99f)
	{
		// Flat stance, measured on the clips: the ankle sits ~1.5 cm higher and the foot
		// ~5 degrees more toe-down than the bind pose.
		const float FlatAnkle = RefAnkleHeight + 1.5f;
		const float FlatPitch = RefFootPitch - 5.0f;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FTransform FootT = ComponentPose.GetComponentSpaceTransform(Legs[Side][2]);
			const FVector Ankle = FootT.GetLocation();
			const FVector Ball = ComponentPose.GetComponentSpaceTransform(Legs[Side][3]).GetLocation();
			const FVector Along = FVector(Ball.X - Ankle.X, Ball.Y - Ankle.Y, 0.0).GetSafeNormal();
			if (Along.IsNearlyZero())
			{
				continue;
			}
			const float Pitch = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Ball.Z - Ankle.Z, FVector::Dist2D(Ball, Ankle))));
			const float NewPitch = FlatPitch + (Pitch - FlatPitch) * LiftScale;
			// Rotate about the foot's sideways axis (Along x Up: a positive angle lifts the toe).
			const FVector Sideways = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
			const FQuat Turn(Sideways, FMath::DegreesToRadians(NewPitch - Pitch));
			FootT.SetRotation((Turn * FootT.GetRotation()).GetNormalized());
			const float Height = static_cast<float>(Ankle.Z);
			const float NewHeight = Height > FlatAnkle ? FlatAnkle + (Height - FlatAnkle) * LiftScale : Height;
			const FVector Target(Ankle.X, Ankle.Y, NewHeight);
			// Rotation first (the IK keeps it), then reach the lowered ankle.
			TArray<FBoneTransform, TInlineAllocator<1>> Rotated;
			Rotated.Add(FBoneTransform(Legs[Side][2], FootT));
			ComponentPose.SafeSetCSBoneTransforms(Rotated);
			if (FMath::Abs(NewHeight - Height) > 0.05f)
			{
				PlaceFoot(ComponentPose, Legs[Side][0], Legs[Side][1], Legs[Side][2], Target);
			}
		}
	}

	if (!Frame.bLockFeet)
	{
		FootLocks[0] = FootLocks[1] = FFootLock();
		LockedFeet = 0;
		FootLockCorrection = 0.0f;
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(ComponentPose, Pose);
		return;
	}

	// Foot contact: from the clips' authored contact curves when they have them
	// (GASP: heel strike to toe-off), else from foot heights.
	static const FName ContactCurves[2] = { FName(TEXT("contact_l")), FName(TEXT("contact_r")) };
	const float FootStepSeconds = FMath::Max(Set->FootStepSeconds, 0.05f);
	int32 NumLocked = 0;
	FootLockCorrection = 0.0f;
	FootContactFromCurves = 0;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FCompactPoseBoneIndex Foot = Legs[Side][2];
		const FCompactPoseBoneIndex Ball = Legs[Side][3];
		FFootLock& Lock = FootLocks[Side];
		const FFootLock& Other = FootLocks[1 - Side];
		const FTransform FootT = ComponentPose.GetComponentSpaceTransform(Foot);
		const FVector AnkleCS = FootT.GetLocation();
		const FVector BallCS = ComponentPose.GetComponentSpaceTransform(Ball).GetLocation();
		const bool bBallDown = BallCS.Z < RefBallHeight + 2.5f;

		bool bHasCurve = false;
		const float CurveValue = Curve.Get(ContactCurves[Side], bHasCurve, 0.0f);
		bool bContact;
		if (bHasCurve)
		{
			bContact = CurveValue >= 0.5f;
			++FootContactFromCurves;
		}
		else if (Lock.bLocked)
		{
			// Stay down until the toe lifts.
			bContact = BallCS.Z < Set->FootPlantHeightCm + 4.0f && AnkleCS.Z < RefAnkleHeight + 15.0f;
		}
		else
		{
			// Heel strike (ankle low), or the ball down with the heel near the ground.
			// The ankle check keeps a toe dipping mid-swing from counting.
			bContact = AnkleCS.Z < RefAnkleHeight + 3.5f || (BallCS.Z < Set->FootPlantHeightCm && AnkleCS.Z < RefAnkleHeight + 6.0f);
		}

		float Lift = 0.0f;
		if (Lock.StepTime >= 0.0f)
		{
			// A procedural step back under the body (see below).
			Lock.StepTime += DeltaSeconds / FootStepSeconds;
			const float T = FMath::Min(Lock.StepTime, 1.0f);
			Lock.Offset = Lock.StepFrom * (1.0f - FMath::SmoothStep(0.0f, 1.0f, T));
			Lock.Alpha = 1.0f;
			Lift = FMath::Sin(T * UE_PI) * Set->FootStepHeightCm;
			if (Lock.StepTime >= 1.0f)
			{
				Lock.StepTime = -1.0f;
				Lock.Offset = FVector::ZeroVector;
				Lock.Alpha = 0.0f;
				Lock.bLocked = false;
			}
		}
		if (Lock.StepTime < 0.0f)
		{
			const FVector Applied = Lock.Offset * (Lock.bLocked ? 1.0f : FMath::SmoothStep(0.0f, 1.0f, Lock.Alpha));
			if (!Lock.bLocked && bContact)
			{
				// Touch-down. Pin the point the foot lands on: the heel at heel strike
				// (the foot then rolls about it), the ball if it is already down. Start
				// from where the (still correcting) foot is, so nothing jumps.
				Lock.bLocked = true;
				Lock.bPinBall = bBallDown;
				const FVector PinCS = Lock.bPinBall ? BallCS : FVector(AnkleCS.X, AnkleCS.Y, 0.0);
				Lock.PinLocal = FootT.InverseTransformPosition(PinCS);
				Lock.LockedWorld = ComponentToWorld.TransformPosition(PinCS + Applied);
				Lock.Offset = Applied;
				Lock.Alpha = 1.0f;
			}
			else if (Lock.bLocked && !bContact)
			{
				// Toe-off: the foot follows the clip again, easing in over the swing.
				Lock.bLocked = false;
			}
			if (Lock.bLocked)
			{
				if (!Lock.bPinBall && bBallDown)
				{
					// The foot has rolled flat: from here it pivots about the ball (and
					// at toe-off about the ball still). Re-pin at the ball where it is now.
					Lock.bPinBall = true;
					Lock.PinLocal = FootT.InverseTransformPosition(BallCS);
					Lock.LockedWorld = ComponentToWorld.TransformPosition(BallCS + Lock.Offset);
				}
				const FVector PinWorld = ComponentToWorld.TransformPosition(FootT.TransformPosition(Lock.PinLocal));
				FVector Correction = Lock.LockedWorld - PinWorld;
				Correction.Z = 0.0;
				Lock.Offset = ComponentToWorld.InverseTransformVector(Correction);
				Lock.Offset.Z = 0.0;
				const float Distance = static_cast<float>(Correction.Size());
				// The body has drifted too far from a planted foot (a standing pedestrian
				// pushed aside): take a step rather than stretch the leg or slide the foot.
				// One foot at a time unless badly overstretched.
				if (Distance > Set->FootLockMaxDistanceCm && (Other.StepTime < 0.0f || Distance > 1.6f * Set->FootLockMaxDistanceCm))
				{
					Lock.bLocked = false;
					Lock.StepTime = 0.0f;
					Lock.StepFrom = Lock.Offset;
					Lock.Alpha = 1.0f;
					++FootLockOverstretches;
				}
			}
			else if (Lock.StepTime < 0.0f && Lock.Alpha > 0.0f)
			{
				// In the swing: hand the foot back to the clip smoothly.
				Lock.Alpha = FMath::Max(0.0f, Lock.Alpha - DeltaSeconds / 0.4f);
			}
		}
		if (Lock.bLocked)
		{
			++NumLocked;
		}

		const float Weight = Lock.bLocked || Lock.StepTime >= 0.0f ? 1.0f : FMath::SmoothStep(0.0f, 1.0f, Lock.Alpha);
		const FVector Offset = Lock.Offset * Weight + FVector(0.0, 0.0, Lift);
		FootLockCorrection = FMath::Max(FootLockCorrection, static_cast<float>((Lock.Offset * Weight).Size()));
		if (Offset.SizeSquared() < 0.01)
		{
			continue;
		}
		PlaceFoot(ComponentPose, Legs[Side][0], Legs[Side][1], Foot, AnkleCS + Offset);
	}
	LockedFeet = NumLocked;
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(ComponentPose, Pose);
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

	// Motion matching reads the recent body pose (feet, pelvis) from this history.
	if (Frame.PoseHistory != nullptr)
	{
		FCSPose<FCompactPose> ComponentPose;
		ComponentPose.InitPose(Output.Pose);
		UE::PoseSearch::FComponentSpacePoseProvider Provider(ComponentPose);
		const TArray<FBoneIndexType> NoRequiredBones;
		Frame.PoseHistory->EvaluateComponentSpace_AnyThread(GetDeltaSeconds(), Provider, /*bStoreScales*/ false,
			/*RootBoneRecoveryTime*/ 0.0f, 1.0f, 1.0f, Frame.bResetPoseHistory, /*bCacheBones*/ false, NoRequiredBones, Output.Curve, TConstArrayView<FName>()
#if WITH_EDITORONLY_DATA
			, GetComponentTransform()
#endif
		);
	}

	AdjustFeet(Output.Pose, Output.Curve);

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
