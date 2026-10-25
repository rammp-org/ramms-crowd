// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdMotionMatchingLibrary.h"

#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "RammsCrowdEditorLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	/** A fresh copy of Source as <Path>/<Name>, replacing any existing asset of that name. */
	template <typename T>
	T* CopyAsset(const T* Source, const FString& Path, const FString& Name)
	{
		const FString PackageName = Path / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *Name))
		{
			// Move the old object out of the way; the copy takes its name and references.
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
			Existing->MarkAsGarbage();
		}
		T* Copy = DuplicateObject<T>(Source, Package, *Name);
		Copy->SetFlags(RF_Public | RF_Standalone);
		Copy->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Copy);
		return Copy;
	}

	bool SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *FileName, Args);
	}
}

TArray<UPoseSearchDatabase*> URammsCrowdMotionMatchingLibrary::AdaptPoseSearchDatabases(const TArray<UPoseSearchDatabase*>& SourceDatabases, USkeleton* TargetSkeleton,
	const FString& AnimationPath, const FString& AnimationSuffix, const FString& DestinationPath, const FString& NameSuffix)
{
	TArray<UPoseSearchDatabase*> Result;
	if (TargetSkeleton == nullptr)
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("AdaptPoseSearchDatabases: no target skeleton"));
		return Result;
	}

	TMap<const UPoseSearchSchema*, UPoseSearchSchema*> Schemas;
	for (const UPoseSearchDatabase* Source : SourceDatabases)
	{
		if (Source == nullptr || Source->Schema == nullptr)
		{
			continue;
		}

		UPoseSearchSchema*& Schema = Schemas.FindOrAdd(Source->Schema);
		if (Schema == nullptr)
		{
			Schema = CopyAsset(Source->Schema.Get(), DestinationPath, Source->Schema->GetName() + TEXT("_") + NameSuffix);
			// One role on the crowd skeleton, no mirror table. (Skeletons is private;
			// clear it through reflection, then use the public AddSkeleton.)
			const UE::PoseSearch::FRole Role = Schema->GetDefaultRole();
			if (FArrayProperty* SkeletonsProperty = FindFProperty<FArrayProperty>(UPoseSearchSchema::StaticClass(), TEXT("Skeletons")))
			{
				FScriptArrayHelper Skeletons(SkeletonsProperty, SkeletonsProperty->ContainerPtrToValuePtr<void>(Schema));
				Skeletons.EmptyValues();
			}
			Schema->AddSkeleton(TargetSkeleton, nullptr, Role);
			// Re-finalize (bone references are resolved against the new skeleton).
			FPropertyChangedEvent Changed(FindFProperty<FProperty>(UPoseSearchSchema::StaticClass(), TEXT("Skeletons")));
			Schema->PostEditChangeProperty(Changed);
			SavePackageOf(Schema);
		}

		UPoseSearchDatabase* Database = CopyAsset(Source, DestinationPath, Source->GetName() + TEXT("_") + NameSuffix);
		Database->Schema = Schema;
		Database->PreviewMesh = nullptr;
		Database->NormalizationSet = nullptr;
		int32 Kept = 0;
		for (int32 Index = Database->GetNumAnimationAssets() - 1; Index >= 0; --Index)
		{
			FPoseSearchDatabaseAnimationAsset* Entry = Database->GetMutableDatabaseAnimationAsset(Index);
			const UObject* SourceAnim = Entry != nullptr ? Entry->AnimAsset.Get() : nullptr;
			UObject* Twin = nullptr;
			if (SourceAnim != nullptr)
			{
				const FString TwinName = SourceAnim->GetName() + AnimationSuffix;
				Twin = StaticLoadObject(UAnimationAsset::StaticClass(), nullptr, *(AnimationPath / TwinName + TEXT(".") + TwinName), nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (Twin == nullptr)
			{
				UE_LOG(LogRammsCrowdEditor, Warning, TEXT("AdaptPoseSearchDatabases: %s: no retargeted twin for %s, dropped"),
					*Database->GetName(), SourceAnim != nullptr ? *SourceAnim->GetName() : TEXT("(none)"));
				Database->RemoveAnimationAssetAt(Index);
				continue;
			}
			Entry->AnimAsset = Twin;
			Entry->MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
			++Kept;
		}

		using namespace UE::PoseSearch;
		const EAsyncBuildIndexResult Built = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);
		UE_LOG(LogRammsCrowdEditor, Display, TEXT("AdaptPoseSearchDatabases: %s: %d animations, %d poses, index %s"),
			*Database->GetName(), Kept, Database->GetSearchIndex().GetNumPoses(), Built == EAsyncBuildIndexResult::Success ? TEXT("built") : TEXT("FAILED"));
		SavePackageOf(Database);
		Result.Add(Database);
	}
	return Result;
}
