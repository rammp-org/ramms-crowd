// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdStateTreeLibrary.h"

#include "RammsCrowdEditorLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Logging/TokenizedMessage.h"
#include "MassStateTreeSchema.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeState.h"
#include "RammsBackAwayTask.h"
#include "RammsStimulusConditions.h"
#include "Tasks/MassZoneGraphFindWanderTarget.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "Tasks/MassZoneGraphStandTask.h"
#include "RammsStandAtPathEndTask.h"
#include "Conditions/StateTreeCommonConditions.h"
#include "UObject/Package.h"

UStateTree* URammsCrowdStateTreeLibrary::CreatePedestrianWanderStateTree(const FString& PackagePath, const FString& AssetName)
{
	const FString PackageName = PackagePath / AssetName;
	UPackage*	  Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreatePedestrianWanderStateTree: cannot create package '%s'"), *PackageName);
		return nullptr;
	}

	// LoadObject (not FindObject): after an editor restart the asset can exist on
	// disk without being in memory — FindObject would shadow it with a second object.
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn);
	const bool	bCreatedNew = StateTree == nullptr;
	if (bCreatedNew)
	{
		StateTree = NewObject<UStateTree>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(StateTree);
	}

	// Rebuild the editor-side definition from scratch each call so the utility is
	// idempotent; the subsequent compile replaces the runtime data.
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(StateTree, NAME_None, RF_Transactional);
	EditorData->Schema = NewObject<UMassStateTreeSchema>(EditorData);
	StateTree->EditorData = EditorData;

	UStateTreeState& Root = EditorData->AddRootState();
	Root.Name = FName(TEXT("Pedestrian"));

	// Child order is selection priority: BackAway is tried first but gated on the
	// startle zone, so unstartled agents fall through to Wander.
	UStateTreeState& BackAway = Root.AddChildState(FName(TEXT("BackAway")));
	UStateTreeState& Wander = Root.AddChildState(FName(TEXT("Wander")));
	UStateTreeState& Stand = Root.AddChildState(FName(TEXT("Stand")));

	// --- BackAway: personal-space violation -> retreat while facing the stimulus ---
	TStateTreeEditorNode<FRammsStimulusZoneCondition>& StartleCondition = BackAway.AddEnterCondition<FRammsStimulusZoneCondition>();
	StartleCondition.GetNode().MinZone = ERammsStimulusZone::Startle;
	BackAway.AddTask<FRammsBackAwayTask>();
	BackAway.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Wander);

	// --- Wander: pick a lane target and follow it ---
	// Each leg is the engine's wander distance (Mass Crowd settings, 5 m ± 1 m). Most
	// legs walk straight on into the next one; about one in three ends in a pause,
	// so pedestrians stroll across the area instead of stopping every few metres.
	// A leg that ends in a pause is marked that way so Mass brakes into the goal
	// instead of overshooting it and creeping back.
	// Task order matters: the wander target must be computed before path follow reads it.
	Wander.AddTask<FMassZoneGraphFindWanderTarget>();
	Wander.AddTask<FRammsStandAtPathEndTask>().GetInstanceData().StandChance = 0.35f;
	Wander.AddTask<FMassZoneGraphPathFollowTask>();
	// (Look the nodes up after adding them all: adding can reallocate the task array.)
	const FStateTreeEditorNode& FindWanderNode = Wander.Tasks[0];
	const FStateTreeEditorNode& StandAtEndNode = Wander.Tasks[1];
	const FStateTreeEditorNode& PathFollowNode = Wander.Tasks[2];

	bool bBound = EditorData->AddPropertyBinding(FindWanderNode, TEXT("WanderTargetLocation"), StandAtEndNode, TEXT("TargetLocation"))
		&& EditorData->AddPropertyBinding(FindWanderNode, TEXT("WanderTargetLocation"), PathFollowNode, TEXT("TargetLocation"));

	// Succeeded: pause if this leg was chosen to end in one, else walk on.
	FStateTreeTransition& ToStand = Wander.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, &Stand);
	{
		FStateTreeEditorNode& CondNode = ToStand.Conditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<FStateTreeCompareBoolCondition>();
		CondNode.Instance.InitializeAs<FStateTreeCompareBoolConditionInstanceData>();
		CondNode.Instance.GetMutable<FStateTreeCompareBoolConditionInstanceData>().bRight = true;
		bBound = EditorData->AddPropertyBinding(StandAtEndNode, TEXT("bStandAtEnd"), CondNode, TEXT("bLeft")) && bBound;
	}
	Wander.AddTransition(EStateTreeTransitionTrigger::OnStateSucceeded, EStateTreeTransitionType::GotoState, &Wander);
	Wander.AddTransition(EStateTreeTransitionTrigger::OnStateFailed, EStateTreeTransitionType::GotoState, &Wander);

	// --- Stand: idle pause between walks, 2-8 s ---
	TStateTreeEditorNode<FRammsRandomDurationTask>& PauseTask = Stand.AddTask<FRammsRandomDurationTask>();
	PauseTask.GetInstanceData().MinSeconds = 2.0f;
	PauseTask.GetInstanceData().MaxSeconds = 8.0f;
	Stand.AddTask<FMassZoneGraphStandTask>().GetInstanceData().Duration = 3.0f;
	bBound = EditorData->AddPropertyBinding(Stand.Tasks[0], TEXT("Duration"), Stand.Tasks[1], TEXT("Duration")) && bBound;
	if (!bBound)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("CreatePedestrianWanderStateTree: failed to bind the wander / stand tasks"));
	}
	Stand.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Wander);

	// Interrupt Wander/Stand the moment the perception processor reports a startle
	// (its zone-change signal wakes the tree, which evaluates these tick transitions).
	auto AddStartleInterrupt = [&BackAway](UStateTreeState& State) {
		FStateTreeTransition& Interrupt = State.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &BackAway);
		FStateTreeEditorNode& CondNode = Interrupt.Conditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<FRammsStimulusZoneCondition>();
		CondNode.Node.GetMutable<FRammsStimulusZoneCondition>().MinZone = ERammsStimulusZone::Startle;
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(CondNode.Node.Get<FStateTreeNodeBase>().GetInstanceDataType()))
		{
			CondNode.Instance.InitializeAs(InstanceType);
		}
	};
	AddStartleInterrupt(Wander);
	AddStartleInterrupt(Stand);

	FStateTreeCompilerLog CompilerLog;
	const bool			  bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog);
	for (const TSharedRef<FTokenizedMessage>& Message : CompilerLog.ToTokenizedMessages())
	{
		UE_LOG(LogRammsCrowdEditor, Log, TEXT("StateTree compile: %s"), *Message->ToText().ToString());
	}

	StateTree->MarkPackageDirty();

	UE_LOG(LogRammsCrowdEditor, Display, TEXT("CreatePedestrianWanderStateTree: %s '%s' compiled=%s bindingOK=%s"),
		bCreatedNew ? TEXT("created") : TEXT("rebuilt"),
		*PackageName,
		bCompiled ? TEXT("true") : TEXT("false"),
		bBound ? TEXT("true") : TEXT("false"));

	return bCompiled ? StateTree : nullptr;
}
