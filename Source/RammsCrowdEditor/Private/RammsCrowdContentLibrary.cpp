// Copyright (c) RAMMP. All rights reserved.

#include "RammsCrowdContentLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "IAssetTools.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Modules/ModuleManager.h"
#include "TextureSourceDataUtils.h"
#include "UObject/ObjectRedirector.h"
#include "VirtualTexturingEditorModule.h"
#include "Widgets/Notifications/SNotificationList.h"

bool URammsCrowdContentLibrary::DownsizeTextureSource(UTexture* Texture, const int32 TargetSourceSize)
{
	if (Texture == nullptr || TargetSourceSize < 4)
	{
		return false;
	}
#if WITH_EDITOR
	const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	Texture->PreEditChange(nullptr);
	const bool bChanged = UE::TextureUtilitiesCommon::Experimental::DownsizeTextureSourceData(Texture, TargetSourceSize, RunningPlatform);
	// DownsizeTextureSourceData does not notify by itself.
	Texture->PostEditChange();
	if (bChanged)
	{
		Texture->MarkPackageDirty();
	}
	return bChanged;
#else
	return false;
#endif
}

void URammsCrowdContentLibrary::ShowSetupNotification(const FString& Message, const FString& HyperlinkText, const FString& HyperlinkURL, const float DurationSeconds, const bool bWarning)
{
#if WITH_EDITOR
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = FMath::Max(DurationSeconds, 3.0f);
	Info.bUseLargeFont = false;
	Info.bUseSuccessFailIcons = true;

	if (!HyperlinkURL.IsEmpty())
	{
		Info.HyperlinkText = FText::FromString(HyperlinkText.IsEmpty() ? HyperlinkURL : HyperlinkText);
		const FString URL = HyperlinkURL;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([URL]()
		{
			FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
		});
	}

	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bWarning ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}
#endif
}

int32 URammsCrowdContentLibrary::ConvertTexturesToNonVirtual(const TArray<UTexture2D*>& Textures)
{
#if WITH_EDITOR
	TArray<UTexture2D*> VirtualTextures;
	for (UTexture2D* Texture : Textures)
	{
		if (Texture != nullptr && Texture->VirtualTextureStreaming)
		{
			VirtualTextures.Add(Texture);
		}
	}
	if (VirtualTextures.Num() > 0)
	{
		IVirtualTexturingEditorModule& Module = FModuleManager::LoadModuleChecked<IVirtualTexturingEditorModule>("VirtualTexturingEditor");
		Module.ConvertVirtualTextures(VirtualTextures, /*bConvertBackToNonVirtual*/ true, /*RelatedMaterials*/ nullptr);
	}
	return VirtualTextures.Num();
#else
	return 0;
#endif
}

int32 URammsCrowdContentLibrary::FixUpRedirectorsInFolder(const FString& FolderPath)
{
#if WITH_EDITOR
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*FolderPath));
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
	TArray<FAssetData> AssetList;
	AssetRegistryModule.Get().GetAssets(Filter, AssetList);

	TArray<UObjectRedirector*> Redirectors;
	Redirectors.Reserve(AssetList.Num());
	for (const FAssetData& AssetData : AssetList)
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset()))
		{
			Redirectors.Add(Redirector);
		}
	}
	if (Redirectors.Num() > 0)
	{
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().FixupReferencers(Redirectors, /*bCheckoutDialogPrompt*/ false, ERedirectFixupMode::DeleteFixedUpRedirectors);
	}
	return Redirectors.Num();
#else
	return 0;
#endif
}
