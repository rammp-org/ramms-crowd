// Copyright (c) RAMMP. All rights reserved.

#include "RammsSeatComponent.h"

#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "RammsCrowdLog.h"
#include "RammsSeatedPoseAnimInstance.h"

URammsSeatComponent::URammsSeatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Default occupant: the City Sample crowd character (soft path — the pack is
	// user-installed and may be absent; see RammsCrowd doc/SETUP.md §0).
	OccupantActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(
		TEXT("/Game/Fab/CitySampleCrowd/Blueprints/BP_CrowdCharacter.BP_CrowdCharacter_C")));

	// Relaxed sit for UE-standard humanoid skeletons (City Sample SK_Base, UE4/5
	// mannequin). Local-space offsets over the reference pose. On these skeletons
	// Yaw is the flexion (bend) axis, Pitch swings laterally, and Roll twists the
	// bone about its own long axis — a pure Roll moves no child joint and reads
	// as "the pose isn't applying". Same signs work on both body sides (mirrored
	// bone frames).
	PoseBoneOffsets = {
		{ TEXT("thigh_l"),    FRotator(0.0f, -85.0f, 0.0f) },  // hip flexion: thigh horizontal
		{ TEXT("thigh_r"),    FRotator(0.0f, -85.0f, 0.0f) },
		{ TEXT("calf_l"),     FRotator(0.0f, 85.0f, 0.0f) },   // knee flexion: shin vertical
		{ TEXT("calf_r"),     FRotator(0.0f, 85.0f, 0.0f) },
		{ TEXT("foot_l"),     FRotator(0.0f, -10.0f, 0.0f) },  // ankle: keep feet near flat
		{ TEXT("foot_r"),     FRotator(0.0f, -10.0f, 0.0f) },
		{ TEXT("spine_01"),   FRotator(0.0f, -8.0f, 0.0f) },   // slight recline into backrest
		{ TEXT("spine_02"),   FRotator(0.0f, 5.0f, 0.0f) },
		{ TEXT("upperarm_l"), FRotator(-30.0f, 10.0f, 0.0f) }, // arms in toward body, slightly forward
		{ TEXT("upperarm_r"), FRotator(-30.0f, 10.0f, 0.0f) },
		{ TEXT("lowerarm_l"), FRotator(0.0f, 15.0f, 0.0f) },   // relaxed elbow bend, hands near lap
		{ TEXT("lowerarm_r"), FRotator(0.0f, 15.0f, 0.0f) },
	};
}

void URammsSeatComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bSpawnOnBeginPlay)
	{
		SpawnOccupant();
	}
}

void URammsSeatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearOccupant();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void URammsSeatComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	// Deliberately skip UActorComponent::PreEditChange. It wraps every details-panel
	// edit in a full component re-register, and the matching PostEditChange then calls
	// RerunConstructionScripts() on the owner — on a live physics-assembled pawn (MeBot:
	// simulated vehicle root + constraint-welded arm/gripper) that destroys and recreates
	// every blueprint component mid-PIE, scattering the arm and killing the occupant
	// (unregister fires EndPlay). None of this component's properties need any of that:
	// PostEditChangeProperty below re-applies the pose itself, which is the only side
	// effect an edit requires. This is what makes live pose tuning in PIE safe.
	UObject::PreEditChange(PropertyAboutToChange);
}

void URammsSeatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Live tuning: any edit re-applies both the seat offset and the pose immediately.
	if (Occupant != nullptr)
	{
		ApplyOccupantOffset();
		ApplyPose();
	}
}
#endif

void URammsSeatComponent::SpawnOccupant()
{
	ClearOccupant();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // never saved into the level
	const FTransform SeatTransform = OccupantOffset * GetComponentTransform();

	if (OccupantMode == ERammsSeatOccupantMode::ActorClass)
	{
		UClass* ActorClass = OccupantActorClass.LoadSynchronous();
		if (ActorClass == nullptr)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Seat occupant class '%s' failed to load — seat stays empty. If this is the City Sample crowd character, the pack is probably not installed (see RammsCrowd doc/SETUP.md §0)."),
				*GetPathName(), *OccupantActorClass.ToString());
			return;
		}

		FActorSpawnParameters DeferredParams = Params; // transient + AlwaysSpawn
		DeferredParams.bDeferConstruction = true;     // so RandomOptions lands before construction
		DeferredParams.Owner = GetOwner();
		AActor* Actor = World->SpawnActor(ActorClass, &SeatTransform, DeferredParams);
		if (Actor == nullptr)
		{
			return;
		}
		// City Sample appearance randomization is a plain bool the construction
		// script reads; set it via reflection before construction runs so this
		// works without a hard dependency on the pack's blueprint.
		if (const FBoolProperty* RandomProp = FindFProperty<FBoolProperty>(Actor->GetClass(), TEXT("RandomOptions")))
		{
			RandomProp->SetPropertyValue_InContainer(Actor, bRandomizeAppearance);
		}
		Actor->FinishSpawning(SeatTransform);
		Occupant = Actor;
	}
	else
	{
		USkeletalMesh* Mesh = OccupantMesh.LoadSynchronous();
		if (Mesh == nullptr)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Seat OccupantMesh not set / failed to load — seat stays empty."), *GetPathName());
			return;
		}
		ASkeletalMeshActor* MeshActor = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), SeatTransform, Params);
		if (MeshActor == nullptr)
		{
			return;
		}
		MeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);
		Occupant = MeshActor;
	}

	if (Occupant != nullptr)
	{
		Occupant->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);
		ApplyOccupantOffset();
		ConfigureOccupantActor(Occupant);
		ApplyPose();

		// Outlast the occupant's own deferred setup (async part loads, appearance
		// randomization mesh swaps) which would otherwise reset the pose to ref/A-pose.
		if (UWorld* TimerWorld = GetWorld(); TimerWorld != nullptr && TimerWorld->IsGameWorld())
		{
			PoseEnforceTicksRemaining = 12;
			TimerWorld->GetTimerManager().SetTimer(PoseEnforceTimer, this, &URammsSeatComponent::EnforcePoseTick, 0.25f, /*bLoop*/ true);
		}
	}
}

void URammsSeatComponent::EnforcePoseTick()
{
	ApplyPose();
	ConfigureOccupantActor(Occupant); // late-added parts must not gain collision either
	if (--PoseEnforceTicksRemaining <= 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PoseEnforceTimer);
		}
	}
}

void URammsSeatComponent::ClearOccupant()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PoseEnforceTimer);
	}
	if (Occupant != nullptr)
	{
		Occupant->Destroy();
		Occupant = nullptr;
	}
}

void URammsSeatComponent::ConfigureOccupantActor(AActor* Actor)
{
	// The occupant is decoration riding another physics body: it must never
	// collide (with the seat owner or the world) or simulate physics.
	TInlineComponentArray<UPrimitiveComponent*> Prims;
	Actor->GetComponents(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim != nullptr)
		{
			Prim->SetSimulatePhysics(false);
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

USkeletalMeshComponent* URammsSeatComponent::GetOccupantLeaderMesh() const
{
	if (Occupant == nullptr)
	{
		return nullptr;
	}
	// Assembled characters (City Sample) drive all parts from the root mesh via
	// leader pose; bare skeletal mesh actors have a single mesh.
	if (USkeletalMeshComponent* Root = Cast<USkeletalMeshComponent>(Occupant->GetRootComponent()))
	{
		return Root;
	}
	return Occupant->FindComponentByClass<USkeletalMeshComponent>();
}

void URammsSeatComponent::ApplyOccupantOffset()
{
	// The occupant is attached to this component, so the offset is simply its relative
	// transform — settable live (details panel, PIE), not just at spawn.
	if (Occupant != nullptr)
	{
		Occupant->SetActorRelativeTransform(OccupantOffset, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);
	}
}

void URammsSeatComponent::ApplyPose()
{
	USkeletalMeshComponent* Mesh = GetOccupantLeaderMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetDisablePostProcessBlueprint(true); // see RammsSeatedPoseAnimInstance
	if (Mesh->GetAnimClass() != URammsSeatedPoseAnimInstance::StaticClass())
	{
		Mesh->SetAnimInstanceClass(URammsSeatedPoseAnimInstance::StaticClass());
	}
	if (URammsSeatedPoseAnimInstance* Pose = Cast<URammsSeatedPoseAnimInstance>(Mesh->GetAnimInstance()))
	{
		// Never clear a self-configured instance with an empty map.
		if (PoseBoneOffsets.Num() > 0)
		{
			Pose->BoneOffsets = PoseBoneOffsets;
		}
	}
	// Refresh bones even when offscreen: seated occupants are static, and sensors
	// (other robots' cameras/ToF) may observe them while no player is looking.
	// Plain AlwaysTickPose freezes socket/bone transforms until rendered.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
