// Copyright Epic Games, Inc. All Rights Reserved.

#include "RammsCrowdSpawner.h"

#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassCrowdMemberTrait.h"
#include "MassCrowdVisualizationTrait.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassLODFragments.h"
#include "MassLODSubsystem.h"
#include "TimerManager.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationProcessor.h"
#include "MassSpawnerTypes.h"
#include "MassVisualizationLODProcessor.h"
#include "MassVisualizationTrait.h"
#include "RammsCrowdLog.h"
#include "RammsCrowdBoxSpawnPointsGenerator.h"
#include "RammsCrowdZoneGraphSpawnPointsGenerator.h"
#include "RammsMassRepresentationSupportTrait.h"

namespace
{
	FString LexToString(const EMassRepresentationType RepresentationType)
	{
		if (const UEnum* Enum = StaticEnum<EMassRepresentationType>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(RepresentationType));
		}

		return TEXT("<unknown>");
	}
} // namespace

ARammsCrowdSpawner::ARammsCrowdSpawner()
{
	Count = DesiredCount;
}

void ARammsCrowdSpawner::BeginPlay()
{
	OnSpawningFinishedEvent.AddUniqueDynamic(this, &ARammsCrowdSpawner::HandleSpawningFinished);
	OnDespawningFinishedEvent.AddUniqueDynamic(this, &ARammsCrowdSpawner::HandleDespawningFinished);

	if (bAutoApplyConfigurationOnBeginPlay)
	{
		ApplyCrowdConfiguration();
	}

	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] BeginPlay with DesiredCount=%d EffectiveCount=%d SpawnMode=%s AutoSpawn=%s"),
		*GetName(),
		DesiredCount,
		GetEffectiveDesiredCount(),
		*StaticEnum<ERammsCrowdSpawnMode>()->GetValueAsString(SpawnMode),
		bAutoSpawnOnBeginPlay ? TEXT("true") : TEXT("false"));

	Super::BeginPlay();
}

#if WITH_EDITOR
void ARammsCrowdSpawner::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bAutoRebuildConfigurationInEditor)
	{
		ApplyCrowdConfiguration();
	}
}
#endif

TArray<FRammsCrowdProfileEntry> ARammsCrowdSpawner::GatherProfileEntries() const
{
	TArray<FRammsCrowdProfileEntry> Entries;
	if (CrowdProfiles.IsEmpty() == false)
	{
		for (const FRammsCrowdProfileEntry& Entry : CrowdProfiles)
		{
			if (Entry.Profile != nullptr && Entry.Profile->IsConfigured() && Entry.Proportion > 0.0f)
			{
				Entries.Add(Entry);
			}
		}
	}
	else if (CrowdProfile != nullptr && CrowdProfile->IsConfigured())
	{
		FRammsCrowdProfileEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Profile = CrowdProfile;
		Entry.Proportion = 1.0f;
	}
	return Entries;
}

bool ARammsCrowdSpawner::HasValidCrowdProfile(FString& OutIssue) const
{
	if (CrowdProfiles.IsEmpty() == false)
	{
		if (GatherProfileEntries().IsEmpty())
		{
			OutIssue = TEXT("CrowdProfiles has no entry with a configured MassEntityConfig and a positive proportion.");
			return false;
		}
		OutIssue.Reset();
		return true;
	}

	if (CrowdProfile == nullptr)
	{
		OutIssue = TEXT("CrowdProfile is not assigned.");
		return false;
	}

	if (CrowdProfile->MassEntityConfig.IsNull())
	{
		OutIssue = TEXT("CrowdProfile does not reference a MassEntityConfig asset.");
		return false;
	}

	OutIssue.Reset();
	return true;
}

void ARammsCrowdSpawner::ApplyCrowdConfiguration()
{
	ResetCrowdConfiguration();

	FString ValidationIssue;
	if (HasValidCrowdProfile(ValidationIssue) == false)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Crowd configuration invalid: %s"), *GetName(), *ValidationIssue);
		Count = 0;
		return;
	}

	Count = GetEffectiveDesiredCount();
	ConfigureEntityTypes();
	ConfigureSpawnGenerators();
	ScaleSpawningCount(GetEffectiveDesiredCountScale());

	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Applied crowd configuration: Count=%d Scale=%.2f EntityTypes=%d Generators=%d"),
		*GetName(),
		Count,
		GetEffectiveDesiredCountScale(),
		EntityTypes.Num(),
		SpawnDataGenerators.Num());

	ValidateConfiguredEntityTypes();

	if (CanUseMassSpawnerWorldApis() && EntityTypes.IsEmpty() == false)
	{
		RegisterEntityTemplates();
	}
}

bool ARammsCrowdSpawner::CanUseMassSpawnerWorldApis() const
{
	return GetWorld() != nullptr
		&& HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) == false;
}

void ARammsCrowdSpawner::ResetCrowdConfiguration()
{
	if (CanUseMassSpawnerWorldApis())
	{
		UE_LOG(LogRammsCrowd, Verbose, TEXT("[%s] Resetting crowd configuration through MassSpawner world APIs"), *GetName());
		UnloadConfig();
	}
	else
	{
		UE_LOG(LogRammsCrowd, Verbose, TEXT("[%s] Resetting crowd configuration without world APIs"), *GetName());
		for (FMassSpawnedEntityType& EntityType : EntityTypes)
		{
			EntityType.UnloadEntityConfig();
		}
	}

	EntityTypes.Reset();
	SpawnDataGenerators.Reset();
}

void ARammsCrowdSpawner::RespawnCrowd()
{
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Respawning crowd"), *GetName());
	DoDespawning();
	ApplyCrowdConfiguration();

	if (Count > 0 && EntityTypes.IsEmpty() == false && SpawnDataGenerators.IsEmpty() == false)
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Invoking DoSpawning with SpawnCount=%d"), *GetName(), GetSpawnCount());
		DoSpawning();
	}
	else
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Skipping DoSpawning because Count=%d EntityTypes=%d Generators=%d"),
			*GetName(),
			Count,
			EntityTypes.Num(),
			SpawnDataGenerators.Num());
	}
}

void ARammsCrowdSpawner::DespawnCrowd()
{
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Despawning crowd"), *GetName());
	DoDespawning();
}

void ARammsCrowdSpawner::SetDesiredCount(int32 NewCount)
{
	DesiredCount = FMath::Max(NewCount, 0);
	if (HasActorBegunPlay())
	{
		RespawnCrowd();
	}
	else
	{
		ApplyCrowdConfiguration();
	}
}

void ARammsCrowdSpawner::SetDesiredCountScale(float NewScale)
{
	DesiredCountScale = FMath::Max(NewScale, 0.01f);
	if (HasActorBegunPlay())
	{
		RespawnCrowd();
	}
	else
	{
		ApplyCrowdConfiguration();
	}
}

void ARammsCrowdSpawner::ConfigureEntityTypes()
{
	for (const FRammsCrowdProfileEntry& Entry : GatherProfileEntries())
	{
		FMassSpawnedEntityType& EntityType = EntityTypes.AddDefaulted_GetRef();
		EntityType.EntityConfig = Entry.Profile->MassEntityConfig;
		EntityType.Proportion = Entry.Proportion;

		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Configured entity type from profile '%s' with config '%s' (proportion %.2f)"),
			*GetName(),
			*GetNameSafe(Entry.Profile),
			*Entry.Profile->MassEntityConfig.ToSoftObjectPath().ToString(),
			Entry.Proportion);
	}
}

void ARammsCrowdSpawner::ConfigureSpawnGenerators()
{
	if (UMassEntitySpawnDataGeneratorBase* Generator = BuildSpawnGenerator())
	{
		FMassSpawnDataGenerator& SpawnGenerator = SpawnDataGenerators.AddDefaulted_GetRef();
		SpawnGenerator.GeneratorClass = Generator->GetClass();
		SpawnGenerator.GeneratorInstance = Generator;
		SpawnGenerator.Proportion = 1.0f;

		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Configured spawn generator '%s'"),
			*GetName(),
			*GetNameSafe(Generator->GetClass()));
	}
}

UMassEntitySpawnDataGeneratorBase* ARammsCrowdSpawner::BuildSpawnGenerator() const
{
	switch (SpawnMode)
	{
		case ERammsCrowdSpawnMode::ZoneGraphLanes:
		{
			URammsCrowdZoneGraphSpawnPointsGenerator* Generator = NewObject<URammsCrowdZoneGraphSpawnPointsGenerator>(const_cast<ARammsCrowdSpawner*>(this));
			Generator->LaneTagFilter = ZoneGraphSpawnSettings.LaneTagFilter;
			Generator->MinGapCm = ZoneGraphSpawnSettings.MinGapCm;
			Generator->MaxGapCm = ZoneGraphSpawnSettings.MaxGapCm;
			Generator->bAlignToLaneDirection = ZoneGraphSpawnSettings.bAlignToLaneDirection;
			return Generator;
		}

		case ERammsCrowdSpawnMode::BoxArea:
		default:
		{
			URammsCrowdBoxSpawnPointsGenerator* Generator = NewObject<URammsCrowdBoxSpawnPointsGenerator>(const_cast<ARammsCrowdSpawner*>(this));
			Generator->LocalCenter = BoxSpawnSettings.LocalCenter;
			Generator->BoxExtent = BoxSpawnSettings.BoxExtent;
			Generator->MinSeparationCm = BoxSpawnSettings.MinSeparationCm;
			Generator->MaxPlacementAttemptsPerEntity = BoxSpawnSettings.MaxPlacementAttemptsPerEntity;
			Generator->bAlignToOwnerRotation = BoxSpawnSettings.bAlignToOwnerRotation;
			Generator->bRandomizeYaw = BoxSpawnSettings.bRandomizeYaw;
			return Generator;
		}
	}
}

int32 ARammsCrowdSpawner::GetEffectiveDesiredCount() const
{
	if (DesiredCount > 0)
	{
		return DesiredCount;
	}

	const TArray<FRammsCrowdProfileEntry> Entries = GatherProfileEntries();
	return Entries.IsEmpty() == false ? Entries[0].Profile->DefaultCount : 0;
}

float ARammsCrowdSpawner::GetEffectiveDesiredCountScale() const
{
	if (DesiredCountScale > 0.0f)
	{
		return DesiredCountScale;
	}

	const TArray<FRammsCrowdProfileEntry> Entries = GatherProfileEntries();
	return Entries.IsEmpty() == false ? Entries[0].Profile->DefaultCountScale : 1.0f;
}

void ARammsCrowdSpawner::ValidateConfiguredEntityTypes()
{
	if (CanUseMassSpawnerWorldApis() == false)
	{
		return;
	}

	UWorld* World = GetWorld();
	check(World);

	for (FMassSpawnedEntityType& EntityType : EntityTypes)
	{
		UMassEntityConfigAsset* EntityConfig = EntityType.GetEntityConfig();
		if (EntityConfig == nullptr)
		{
			UE_LOG(LogRammsCrowd, Error, TEXT("[%s] Failed to load MassEntityConfig asset '%s'"),
				*GetName(),
				*EntityType.EntityConfig.ToSoftObjectPath().ToString());
			continue;
		}

		const bool					   bTemplateValidated = EntityConfig->GetMutableConfig().ValidateEntityTemplate(*World);
		const FMassEntityTemplate&	   EntityTemplate = EntityConfig->GetOrCreateEntityTemplate(*World);
		const FMassEntityTemplateData& TemplateData = EntityTemplate.GetTemplateData();
		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] MassEntityConfig '%s': ValidateEntityTemplate=%s TemplateValid=%s"),
			*GetName(),
			*GetNameSafe(EntityConfig),
			bTemplateValidated ? TEXT("true") : TEXT("false"),
			EntityTemplate.IsValid() ? TEXT("true") : TEXT("false"));

		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] MassEntityConfig '%s' template features: Transform=%s ViewerInfo=%s ActorFragment=%s Representation=%s RepresentationLOD=%s RepresentationParams=%s VisualizationLODParams=%s VisualizationChunk=%s VisualizationTag=%s VisualizationLODTag=%s"),
			*GetName(),
			*GetNameSafe(EntityConfig),
			TemplateData.HasFragment<FTransformFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasFragment<FMassViewerInfoFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasFragment<FMassActorFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasFragment<FMassRepresentationFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasFragment<FMassRepresentationLODFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasConstSharedFragment<FMassRepresentationParameters>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasConstSharedFragment<FMassVisualizationLODParameters>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasChunkFragment<FMassVisualizationChunkFragment>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasTag<FMassVisualizationProcessorTag>() ? TEXT("true") : TEXT("false"),
			TemplateData.HasTag<FMassVisualizationLODProcessorTag>() ? TEXT("true") : TEXT("false"));

		const UMassVisualizationTrait* VisualizationTrait = Cast<UMassVisualizationTrait>(EntityConfig->FindTrait(UMassVisualizationTrait::StaticClass(), false));
		if (VisualizationTrait == nullptr)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' does not include a Mass visualization trait. Without UMassVisualizationTrait / UMassMovableVisualizationTrait / UMassStationaryVisualizationTrait, entities will spawn but neither render nor respond to mass.debug.Representation* diagnostics."),
				*GetName(),
				*GetNameSafe(EntityConfig));
		}
		else
		{
			const FStaticMeshInstanceVisualizationDesc& StaticMeshDesc = VisualizationTrait->StaticMeshInstanceDesc;
			bool										bHasValidStaticMesh = false;
			for (const FMassStaticMeshInstanceVisualizationMeshDesc& MeshDesc : StaticMeshDesc.Meshes)
			{
				if (MeshDesc.Mesh != nullptr)
				{
					bHasValidStaticMesh = true;
					break;
				}
			}

			UE_LOG(LogRammsCrowd, Display, TEXT("[%s] MassEntityConfig '%s' visualization trait '%s': Meshes=%d StaticMeshValid=%s HighResActor=%s LowResActor=%s LODs=[High:%s Medium:%s Low:%s Off:%s]"),
				*GetName(),
				*GetNameSafe(EntityConfig),
				*GetNameSafe(VisualizationTrait->GetClass()),
				StaticMeshDesc.Meshes.Num(),
				bHasValidStaticMesh ? TEXT("true") : TEXT("false"),
				*GetNameSafe(VisualizationTrait->HighResTemplateActor.Get()),
				*GetNameSafe(VisualizationTrait->LowResTemplateActor.Get()),
				*LexToString(VisualizationTrait->Params.LODRepresentation[EMassLOD::High]),
				*LexToString(VisualizationTrait->Params.LODRepresentation[EMassLOD::Medium]),
				*LexToString(VisualizationTrait->Params.LODRepresentation[EMassLOD::Low]),
				*LexToString(VisualizationTrait->Params.LODRepresentation[EMassLOD::Off]));

			if (bHasValidStaticMesh == false
				&& (VisualizationTrait->Params.LODRepresentation[EMassLOD::High] == EMassRepresentationType::StaticMeshInstance
					|| VisualizationTrait->Params.LODRepresentation[EMassLOD::Medium] == EMassRepresentationType::StaticMeshInstance
					|| VisualizationTrait->Params.LODRepresentation[EMassLOD::Low] == EMassRepresentationType::StaticMeshInstance
					|| VisualizationTrait->Params.LODRepresentation[EMassLOD::Off] == EMassRepresentationType::StaticMeshInstance))
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' requests StaticMeshInstance representation for at least one LOD, but the visualization trait has no valid static mesh entries. Unreal will sanitize those LODs to None."),
					*GetName(),
					*GetNameSafe(EntityConfig));
			}

			// A null HighRes template with HighResSpawnedActor requested usually means
			// the template blueprint failed to LOAD — e.g. its parent class lives in a
			// content pack that is not installed (City Sample Crowds is not committed
			// to the repo; the RammsCrowd startup script validates it). Agents fall
			// back to proxy meshes instead of crashing, but say why loudly.
			if (VisualizationTrait->Params.LODRepresentation[EMassLOD::High] == EMassRepresentationType::HighResSpawnedActor
				&& VisualizationTrait->HighResTemplateActor.Get() == nullptr)
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' wants HighRes spawned actors but HighResTemplateActor did not resolve. If the template derives from City Sample content, the pack is probably not installed — see RammsCrowd doc/SETUP.md §0. Agents will render as proxy meshes only."),
					*GetName(),
					*GetNameSafe(EntityConfig));
			}
		}

		if (EntityConfig->FindTrait(URammsMassRepresentationSupportTrait::StaticClass(), false) == nullptr)
		{
			UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' does not include the 'RAMMS Representation Support' trait. Static-mesh crowd visualization typically needs that trait so FMassActorFragment and FTransformFragment exist."),
				*GetName(),
				*GetNameSafe(EntityConfig));
		}

		// UE 5.7: the base visualization LOD/representation processors have
		// bAutoRegisterWithProcessingPhases=false — the only auto-registered
		// implementations ship in MassCrowd and only match FMassCrowdTag
		// entities. A config with a visualization trait but no crowd-member
		// trait spawns entities that never get an LOD or representation.
		if (VisualizationTrait != nullptr)
		{
			if (EntityConfig->FindTrait(UMassCrowdMemberTrait::StaticClass(), false) == nullptr)
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' has a visualization trait but no 'Mass Crowd Member' trait. In UE 5.7 only MassCrowd's visualization processors auto-register and they require FMassCrowdTag — without the Crowd Member trait entities stay Rep=None/LOD=Max and never render."),
					*GetName(),
					*GetNameSafe(EntityConfig));
			}

			if (VisualizationTrait->IsA<UMassCrowdVisualizationTrait>() == false)
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] MassEntityConfig '%s' uses visualization trait '%s'. Crowd entities should use 'Mass Crowd Visualization' (UMassCrowdVisualizationTrait): it sets LODParams.FilterTag=FMassCrowdTag (required by the crowd LOD processor) and the crowd representation subsystem."),
					*GetName(),
					*GetNameSafe(EntityConfig),
					*GetNameSafe(VisualizationTrait->GetClass()));
			}
		}
	}
}

void ARammsCrowdSpawner::HandleSpawningFinished()
{
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Mass spawning finished"), *GetName());

	// Audit after the LOD/visualization processors have had time to run a few
	// frames; distinguishes "entities culled/at wrong LOD" from "processors
	// never ran" when the crowd is invisible.
	GetWorldTimerManager().SetTimer(SpawnAuditTimerHandle, this, &ARammsCrowdSpawner::LogSpawnedEntityAudit, 2.0f, /*bLoop*/ false);
}

void ARammsCrowdSpawner::LogSpawnedEntityAudit()
{
	const UWorld* World = GetWorld();
	const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (EntitySubsystem == nullptr)
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Entity audit: no MassEntitySubsystem"), *GetName());
		return;
	}

	if (const UMassLODSubsystem* LODSubsystem = World->GetSubsystem<UMassLODSubsystem>())
	{
		UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Entity audit: LOD subsystem has %d registered viewers"),
			*GetName(), LODSubsystem->GetViewers().Num());
	}
	else
	{
		UE_LOG(LogRammsCrowd, Warning, TEXT("[%s] Entity audit: no MassLODSubsystem — all LOD calculations default to Off"), *GetName());
	}

	auto LodToString = [](const EMassLOD::Type Lod) -> const TCHAR* {
		switch (Lod)
		{
			case EMassLOD::High: return TEXT("High");
			case EMassLOD::Medium: return TEXT("Medium");
			case EMassLOD::Low: return TEXT("Low");
			case EMassLOD::Off: return TEXT("Off");
			default: return TEXT("Max/unset");
		}
	};

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	int32 TotalEntities = 0;
	for (const FSpawnedEntities& Spawned : AllSpawnedEntities)
	{
		TotalEntities += Spawned.Entities.Num();
	}

	constexpr int32 MaxAuditedEntities = 5;
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Entity audit: %d spawned entities tracked, showing up to %d"),
		*GetName(), TotalEntities, MaxAuditedEntities);

	int32 AuditedCount = 0;
	for (const FSpawnedEntities& Spawned : AllSpawnedEntities)
	{
		for (const FMassEntityHandle Entity : Spawned.Entities)
		{
			if (AuditedCount >= MaxAuditedEntities)
			{
				return;
			}
			++AuditedCount;

			if (!EntityManager.IsEntityValid(Entity))
			{
				UE_LOG(LogRammsCrowd, Warning, TEXT("[%s]   entity %d: INVALID handle"), *GetName(), AuditedCount - 1);
				continue;
			}

			const FTransformFragment* TransformFragment = EntityManager.GetFragmentDataPtr<FTransformFragment>(Entity);
			const FMassRepresentationFragment* Representation = EntityManager.GetFragmentDataPtr<FMassRepresentationFragment>(Entity);
			const FMassRepresentationLODFragment* RepresentationLOD = EntityManager.GetFragmentDataPtr<FMassRepresentationLODFragment>(Entity);
			const FMassViewerInfoFragment* ViewerInfo = EntityManager.GetFragmentDataPtr<FMassViewerInfoFragment>(Entity);

			UE_LOG(LogRammsCrowd, Display, TEXT("[%s]   entity %d: Loc=%s Rep=%s LOD=%s ViewerDist=%s"),
				*GetName(),
				AuditedCount - 1,
				TransformFragment ? *TransformFragment->GetTransform().GetLocation().ToCompactString() : TEXT("<no transform fragment>"),
				Representation ? *LexToString(Representation->CurrentRepresentation) : TEXT("<no representation fragment>"),
				RepresentationLOD ? LodToString(RepresentationLOD->LOD) : TEXT("<no LOD fragment>"),
				ViewerInfo ? *FString::Printf(TEXT("%.0f"), FMath::Sqrt(ViewerInfo->ClosestViewerDistanceSq)) : TEXT("<no viewer fragment>"));
		}
	}
}

void ARammsCrowdSpawner::HandleDespawningFinished()
{
	UE_LOG(LogRammsCrowd, Display, TEXT("[%s] Mass despawning finished"), *GetName());
}
