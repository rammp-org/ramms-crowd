// Copyright Epic Games, Inc. All Rights Reserved.
// Vendored from Engine/Plugins/Animation/AnimationWarping AnimNode_FootPlacement.h
// (UE 5.7) with one behavioral patch: trace hits are accepted on actors WITHOUT
// a CharacterMovementComponent, judged by WalkableSlopeAngle instead of being
// rejected wholesale (the stock node casts the owner to ACharacter, which Mass
// crowd puppets can never be - see Epic's own TODO in the stock node). Settings
// structs/enums and runtime data types are reused from the engine header.
// Re-diff against the engine node on engine upgrades.

#pragma once

#include "BoneControllers/AnimNode_FootPlacement.h"
#include "RammsFootPlacement.generated.h"

namespace UE::Anim::FootPlacement
{
	// Defined in RammsFootPlacement.cpp (renamed vendored copy of the stock
	// node's cpp-local FEvaluationContext, carrying the walkable-slope patch).
	struct FRammsFPEvalContext;
}

USTRUCT(BlueprintInternalUseOnly, Experimental)
struct FAnimNode_RammsFootPlacement : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:

	// Foot/Ball speed evaluation mode (Graph or Manual) used to decide when the feet are locked
	// Graph mode uses the root motion attribute from the animations to calculate the joint's speed
	// Manual mode uses a per-foot curve name representing the joint's speed
	UPROPERTY(EditAnywhere, Category = "Settings")
	EWarpingEvaluationMode PlantSpeedMode = EWarpingEvaluationMode::Manual;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference IKFootRootBone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference PelvisBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinHiddenByDefault))
	FFootPlacementPelvisSettings PelvisSettings;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FFootPlacemenLegDefinition> LegDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinHiddenByDefault))
	FFootPlacementPlantSettings PlantSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinHiddenByDefault))
	FFootPlacementInterpolationSettings InterpolationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinHiddenByDefault))
	FFootPlacementTraceSettings TraceSettings;

	// RAMMS patch: walkable-slope fallback used when the owning actor has no
	// CharacterMovementComponent (Mass crowd puppets, plain skeletal actors).
	// Trace hits whose surface is steeper than this angle (degrees) are
	// rejected, mirroring CharacterMovement's IsWalkable. The stock engine
	// node rejects EVERY hit without a movement component.
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float WalkableSlopeAngle = 60.0f;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FVector BaseTranslationDelta = FVector::ZeroVector;

public:
	RAMMSCROWD_API FAnimNode_RammsFootPlacement();

	// FAnimNode_Base interface
	RAMMSCROWD_API virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	RAMMSCROWD_API virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	RAMMSCROWD_API virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	RAMMSCROWD_API virtual void EvaluateSkeletalControl_AnyThread(
		FComponentSpacePoseContext& Output, 
		TArray<FBoneTransform>& OutBoneTransforms) override;
	RAMMSCROWD_API virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase

private:
	// FAnimNode_SkeletalControlBase interface
	RAMMSCROWD_API virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	// Gather raw or trivially calculated values from input pose
	void GatherPelvisDataFromInputs(const UE::Anim::FootPlacement::FRammsFPEvalContext& Context);
	void GatherLegDataFromInputs(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const FFootPlacemenLegDefinition& LegDef);

	void CalculateFootMidpoint(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		TConstArrayView<UE::Anim::FootPlacement::FLegRuntimeData> LegData,
		FVector& OutMidpoint) const;

	// Calculate procedural adjustments before solving the desired pelvis position
	void ProcessCharacterState(const UE::Anim::FootPlacement::FRammsFPEvalContext& Context);
	void ProcessFootAlignment(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData);

	// Calculate the desired pelvis offset, based on procedural character/foot adjustments
	FTransform SolvePelvis(const UE::Anim::FootPlacement::FRammsFPEvalContext& Context);

	FTransform UpdatePelvisInterpolationRootSpace(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const FTransform& TargetPelvisTransform);

	// Post-processing adjustments + fix hyper-extension/compression
	UE::Anim::FootPlacement::FPlantResult FinalizeFootAlignment(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const FFootPlacemenLegDefinition& LegDef,
		const FTransform& PelvisTransformCS);

	FVector GetApproachDirWS(const FAnimationBaseContext& Context) const;

	const FTransform& GetRootToComponent() const;

private:
	float CachedDeltaTime = 0.0f;
	FVector LastComponentLocation = FVector::ZeroVector;

	TArray<UE::Anim::FootPlacement::FLegRuntimeData> LegsData;
	UE::Anim::FootPlacement::FPlantRuntimeSettings PlantRuntimeSettings;
	UE::Anim::FootPlacement::FPelvisRuntimeData PelvisData;
	UE::Anim::FootPlacement::FCharacterData CharacterData;

	// Whether we want to plant, independently from any dynamic pose adjustments we may do
	bool WantsToPlant(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	// Get Alignment Alpha based on current foot speed
	// 0.0 is fully unaligned and the foot is in flight.
	// 1.0 is fully aligned and the foot is planted.
	float GetAlignmentAlpha(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	// This function looks at both the foot bone and the ball bone, returning the smallest distance to the
	// planting plane. Note this distance can be negative, meaning it's penetrating.
	float CalcTargetPlantPlaneDistance(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;

	struct FPelvisOffsetRangeForLimb
	{
		float MaxExtension;
		float MinExtension;
		float DesiredExtension;
	};

	// Find the horizontal pelvis offset range for the foot to reach:
	void FindPelvisOffsetRangeForLimb(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const FVector& PlantTargetLocationCS,
		const FTransform& PelvisTransformCS,
		FPelvisOffsetRangeForLimb& OutPelvisOffsetRangeCS) const;

	// Adjust LastPlantTransformWS to current, to have the foot pivot around the ball instead of the ankle
	FTransform GetFootPivotAroundBallWS(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose,
		const FTransform& LastPlantTransformWS) const;

	// Align the transform the provided world space ground plant plane.
	// Also outputs the twist along the ground plane needed to get there
	void AlignPlantToGround(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const FPlane& PlantPlaneWS,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose,
		FTransform& InOutFootTransformWS,
		FQuat& OutTwistCorrection) const;

	// Handles horizontal interpolation when unlocking the plant
	FTransform UpdatePlantOffsetInterpolation(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		UE::Anim::FootPlacement::FLegRuntimeData::FInterpolationData& InOutInterpData) const;

	// Handles the interpolation of the planting plane. Because the plant transform is specified with respect to the 
	// planting plane, it cannot change abruptly without causing an animation pop. It must be interpolated instead.
	void UpdatePlantingPlaneInterpolation(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const FTransform& FootTransformWS,
		const FTransform& LastAlignedFootTransform,
		const float AlignmentAlpha,
		FPlane& InOutPlantPlane,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose,
		UE::Anim::FootPlacement::FLegRuntimeData::FInterpolationData& InOutInterpData) const;

	// Checks unplanting and replanting conditions to determine if the foot is planted
	void DeterminePlantType(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const FTransform& FKTransformWS,
		const FTransform& CurrentBoneTransformWS,
		UE::Anim::FootPlacement::FLegRuntimeData::FPlantData& InOutPlantData,
		const UE::Anim::FootPlacement::FLegRuntimeData::FInputPoseData& LegInputPose) const;
	
	float GetMaxLimbExtension(const float DesiredExtension, const float LimbLength) const;
	float GetMinLimbExtension(const float DesiredExtension, const float LimbLength) const;

	void ResetRuntimeData();

	
#if ENABLE_FOOTPLACEMENT_DEBUG
	UE::Anim::FootPlacement::FDebugData DebugData;

	void DrawDebug(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData& LegData,
		const UE::Anim::FootPlacement::FPlantResult& PlantResult) const;

	void DrawVLog(
		const UE::Anim::FootPlacement::FRammsFPEvalContext& Context,
		const UE::Anim::FootPlacement::FLegRuntimeData& LegData,
    	const UE::Anim::FootPlacement::FPlantResult& PlantResult) const;
#endif

	bool bIsFirstUpdate = false;
	FGraphTraversalCounter UpdateCounter;
};

