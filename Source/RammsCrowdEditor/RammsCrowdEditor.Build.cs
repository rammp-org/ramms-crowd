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
			"AnimGraph",
			"AnimGraphRuntime",
			"AnimationCore",
			"AnimationWarpingEditor",
			"AnimationWarpingRuntime",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"MassAIBehavior",
			"MassCrowd",
			"MassEntity",
			"MassCommon",
			"MassMovement",
			"PropertyBindingUtils",
			"RammsCrowd",
			"RammsCrowdUncooked",
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
