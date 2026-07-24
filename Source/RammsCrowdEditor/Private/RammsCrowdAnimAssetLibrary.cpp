// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdAnimAssetLibrary.h"

#include "RammsCrowdEditorLog.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "RammsFootPlacementGraphNode.h"
#include "AnimGraphNode_Root.h"
#include "AnimationGraphSchema.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

void URammsCrowdAnimAssetLibrary::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
	if (BlendSpace == nullptr)
	{
		return;
	}
#if WITH_EDITOR
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->MarkPackageDirty();
#endif
}

UAnimBlueprint* URammsCrowdAnimAssetLibrary::CreateFootPlacementPostProcessAnimBlueprint(const FString& PackagePath, const FString& AssetName, USkeleton* Skeleton)
{
	if (Skeleton == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: Skeleton is null."));
		return nullptr;
	}

	// Idempotent: reuse an existing asset (its graph is rebuilt below), else create.
	// Full object path (Package.AssetName), and LoadObject rather than FindObject:
	// after an editor restart the asset can exist on disk without being in memory,
	// and a bare package path fails to resolve the object (see the StateTree
	// utility for the same convention).
	const FString	PackageName = PackagePath / AssetName;
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
	if (AnimBP == nullptr)
	{
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->ParentClass = UAnimInstance::StaticClass();
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AnimBP = Cast<UAnimBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory));
	}
	if (AnimBP == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: failed to create %s/%s."), *PackagePath, *AssetName);
		return nullptr;
	}

	// Locate the anim graph and its output (root) node.
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		// UEdGraph::Schema is a TSubclassOf (a UClass*), so UClass::IsChildOf is
		// the correct schema test here.
		if (Graph != nullptr && Graph->Schema != nullptr && Graph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (AnimGraph == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: %s has no anim graph."), *AssetName);
		return nullptr;
	}

	// Rebuild from scratch: drop everything but the output node.
	for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(AnimGraph->Nodes))
	{
		if (Node != nullptr && !Node->IsA<UAnimGraphNode_Root>())
		{
			AnimGraph->RemoveNode(Node);
		}
	}
	TArray<UAnimGraphNode_Root*> RootNodes;
	AnimGraph->GetNodesOfClass(RootNodes);
	if (RootNodes.Num() == 0)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: %s anim graph has no output node."), *AssetName);
		return nullptr;
	}
	UAnimGraphNode_Root* RootNode = RootNodes[0];
	RootNode->BreakAllNodeLinks();

	// Input Pose: receives the pose the crowd anim component (or any main
	// instance) is already playing — this is what makes it post-process-safe.
	FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> InputCreator(*AnimGraph);
	UAnimGraphNode_LinkedInputPose*					  InputNode = InputCreator.CreateNode();
	InputNode->NodePosX = RootNode->NodePosX - 700;
	InputNode->NodePosY = RootNode->NodePosY;
	InputCreator.Finalize();

	// Foot Placement, configured for the City Sample SK_Base bone naming.
	FGraphNodeCreator<UAnimGraphNode_RammsFootPlacement> FootCreator(*AnimGraph);
	UAnimGraphNode_RammsFootPlacement*					 FootNode = FootCreator.CreateNode();
	FootNode->NodePosX = RootNode->NodePosX - 400;
	FootNode->NodePosY = RootNode->NodePosY;
	{
		FAnimNode_RammsFootPlacement& Node = FootNode->Node;
		Node.PelvisBone.BoneName = TEXT("pelvis");
		Node.IKFootRootBone.BoneName = TEXT("ik_foot_root");
		Node.LegDefinitions.Reset(2);
		for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
		{
			FFootPlacemenLegDefinition& Leg = Node.LegDefinitions.AddDefaulted_GetRef();
			Leg.FKFootBone.BoneName = *FString::Printf(TEXT("foot_%s"), Side);
			Leg.IKFootBone.BoneName = *FString::Printf(TEXT("ik_foot_%s"), Side);
			Leg.BallBone.BoneName = *FString::Printf(TEXT("ball_%s"), Side);
			Leg.NumBonesInLimb = 2;
		}
		// The crowd anims carry no foot-speed curves (Manual plant mode needs
		// them), so disable locking: pure ground conformance, no plant/lock.
		Node.PlantSettings.LockType = EFootPlacementLockType::Unlocked;
	}
	FootCreator.Finalize();

	// Wire Input.Pose -> FootPlacement.ComponentPose -> Output.Result. The anim
	// schema inserts the local<->component space conversion nodes automatically.
	const UAnimationGraphSchema* Schema = GetDefault<UAnimationGraphSchema>();
	UEdGraphPin*				 InputPosePin = InputNode->FindPin(TEXT("Pose"), EGPD_Output);
	UEdGraphPin*				 FootInPin = FootNode->FindPin(TEXT("ComponentPose"), EGPD_Input);
	UEdGraphPin*				 FootOutPin = FootNode->FindPin(TEXT("Pose"), EGPD_Output);
	UEdGraphPin*				 ResultPin = RootNode->FindPin(TEXT("Result"), EGPD_Input);
	if (InputPosePin == nullptr || FootInPin == nullptr || FootOutPin == nullptr || ResultPin == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: unexpected pin layout (input:%d foot-in:%d foot-out:%d result:%d)."),
			InputPosePin != nullptr, FootInPin != nullptr, FootOutPin != nullptr, ResultPin != nullptr);
		return nullptr;
	}
	if (!Schema->TryCreateConnection(InputPosePin, FootInPin) || !Schema->TryCreateConnection(FootOutPin, ResultPin))
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreateFootPlacementPostProcessAnimBlueprint: pin connection failed."));
		return nullptr;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->MarkPackageDirty();
	UE_LOG(LogRammsCrowdEditor, Log, TEXT("CreateFootPlacementPostProcessAnimBlueprint: built %s (skeleton %s)."), *AnimBP->GetPathName(), *Skeleton->GetName());
	return AnimBP;
}
