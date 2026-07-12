// Copyright (c) RAMMP. All rights reserved.

using UnrealBuildTool;

public class RammsCrowdEditor : ModuleRules
{
	public RammsCrowdEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",
			"MassAIBehavior",
			"MassCrowd",
			"MassEntity",
			"MassCommon",
			"MassMovement",
			"PropertyBindingUtils",
			"RammsCrowd",
			"Slate",
			"SlateCore",
			"StateTreeModule",
			"StateTreeEditorModule",
			"TargetPlatform",
			"TextureUtilitiesCommon",
			"UnrealEd",
			"VirtualTexturingEditor",
			"ZoneGraph",
		});
	}
}
