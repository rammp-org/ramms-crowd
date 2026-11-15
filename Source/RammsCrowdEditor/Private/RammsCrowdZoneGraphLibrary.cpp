// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdZoneGraphLibrary.h"

#include "RammsCrowdEditorLog.h"
#include "ZoneGraphDelegates.h"
#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

namespace
{
	/** The mask of the named tags (Project Settings > Zone Graph); Found counts the names that exist. */
	FZoneGraphTagMask MaskFromNames(const TArray<FName>& TagNames, int32& Found)
	{
		FZoneGraphTagMask Mask;
		Found = 0;
		const UZoneGraphSettings* Settings = GetDefault<UZoneGraphSettings>();
		if (Settings == nullptr)
		{
			return Mask;
		}
		const TConstArrayView<FZoneGraphTagInfo> Infos = Settings->GetTagInfos();
		for (const FName& Name : TagNames)
		{
			const FZoneGraphTagInfo* Info = Infos.FindByPredicate([&Name](const FZoneGraphTagInfo& Candidate) { return Candidate.IsValid() && Candidate.Name == Name; });
			if (Info == nullptr)
			{
				UE_LOG(LogRammsCrowdEditor, Warning, TEXT("ZoneGraph tag '%s' is not defined (Project Settings > Zone Graph); skipped."), *Name.ToString());
				continue;
			}
			Mask.Add(Info->Tag);
			++Found;
		}
		return Mask;
	}
}

void URammsCrowdZoneGraphLibrary::UpdateZoneShape(UZoneShapeComponent* ShapeComponent)
{
	if (ShapeComponent != nullptr)
	{
		ShapeComponent->UpdateShape();
	}
}

void URammsCrowdZoneGraphLibrary::RebuildZoneGraph()
{
#if WITH_EDITOR
	UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast();
#endif
}

int32 URammsCrowdZoneGraphLibrary::SetZoneShapeTags(UZoneShapeComponent* ShapeComponent, const TArray<FName>& TagNames, const bool bAdd)
{
	if (ShapeComponent == nullptr)
	{
		return 0;
	}
	int32 Found = 0;
	const FZoneGraphTagMask Mask = MaskFromNames(TagNames, Found);
	ShapeComponent->Modify();
	if (bAdd)
	{
		ShapeComponent->GetMutableTags().Add(Mask);
	}
	else
	{
		ShapeComponent->SetTags(Mask);
	}
	return Found;
}

int32 URammsCrowdZoneGraphLibrary::RemoveZoneShapeTags(UZoneShapeComponent* ShapeComponent, const TArray<FName>& TagNames)
{
	if (ShapeComponent == nullptr)
	{
		return 0;
	}
	int32 Found = 0;
	const FZoneGraphTagMask Mask = MaskFromNames(TagNames, Found);
	ShapeComponent->Modify();
	ShapeComponent->GetMutableTags().Remove(Mask);
	return Found;
}

TArray<FName> URammsCrowdZoneGraphLibrary::GetZoneShapeTagNames(const UZoneShapeComponent* ShapeComponent)
{
	TArray<FName> Names;
	const UZoneGraphSettings* Settings = GetDefault<UZoneGraphSettings>();
	if (ShapeComponent == nullptr || Settings == nullptr)
	{
		return Names;
	}
	const FZoneGraphTagMask Tags = ShapeComponent->GetTags();
	for (const FZoneGraphTagInfo& Info : Settings->GetTagInfos())
	{
		if (Info.IsValid() && Tags.Contains(Info.Tag))
		{
			Names.Add(Info.Name);
		}
	}
	return Names;
}
