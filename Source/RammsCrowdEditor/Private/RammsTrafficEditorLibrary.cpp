// Copyright (c) RAMMP. All rights reserved.

#include "RammsTrafficEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "RammsCrowdEditorLog.h"
#include "RammsTrafficTypes.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

UMaterialParameterCollection* URammsTrafficEditorLibrary::EnsureSignalParameterCollection(const FString& AssetPath)
{
	FString PackageName = AssetPath;
	// Accept an object path too ("/Game/X/MPC_Y.MPC_Y").
	int32 Dot = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), Dot))
	{
		PackageName.LeftInline(Dot);
	}
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(PackageName, false, &Reason))
	{
		UE_LOG(LogRammsCrowdEditor, Error, TEXT("EnsureSignalParameterCollection: '%s' is not a valid package name: %s"), *AssetPath, *Reason.ToString());
		return nullptr;
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bool bCreated = false;
	if (Collection == nullptr)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Collection = NewObject<UMaterialParameterCollection>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Collection);
		bCreated = true;
	}

	FProperty* ScalarsProperty = FindFProperty<FProperty>(UMaterialParameterCollection::StaticClass(), GET_MEMBER_NAME_CHECKED(UMaterialParameterCollection, ScalarParameters));
	Collection->PreEditChange(ScalarsProperty);
	int32 Added = 0, Updated = 0;
	for (const TPair<FName, float>& Default : RammsTraffic::GetSignalParameterDefaults())
	{
		FCollectionScalarParameter* Existing = Collection->ScalarParameters.FindByPredicate([&Default](const FCollectionScalarParameter& P) { return P.ParameterName == Default.Key; });
		if (Existing == nullptr)
		{
			FCollectionScalarParameter Parameter;
			Parameter.ParameterName = Default.Key;
			Parameter.DefaultValue = Default.Value;
			Collection->ScalarParameters.Add(Parameter);
			++Added;
		}
		else if (Existing->DefaultValue != Default.Value)
		{
			Existing->DefaultValue = Default.Value;
			++Updated;
		}
	}
	FPropertyChangedEvent Event(ScalarsProperty);
	Collection->PostEditChangeProperty(Event);
	if (bCreated || Added > 0 || Updated > 0)
	{
		Collection->MarkPackageDirty();
	}
	UE_LOG(LogRammsCrowdEditor, Display, TEXT("EnsureSignalParameterCollection: %s %s (%d added, %d updated)."),
		bCreated ? TEXT("created") : TEXT("refreshed"), *Collection->GetPathName(), Added, Updated);
	return Collection;
}

TArray<FName> URammsTrafficEditorLibrary::GetSignalParameterNames()
{
	TArray<FName> Names;
	for (const TPair<FName, float>& Default : RammsTraffic::GetSignalParameterDefaults())
	{
		Names.Add(Default.Key);
	}
	return Names;
}
