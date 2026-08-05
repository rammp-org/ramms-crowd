// Copyright (c) RAMMP. All rights reserved.

using UnrealBuildTool;

// Anim graph node(s) for the vendored crowd animation nodes. UncookedOnly —
// NOT Editor — because uncooked `-game` runs load anim blueprints whose
// exports reference the graph node class; with an Editor-type module the
// class is missing there and the crowd AnimBP loads broken (T-posing NPCs).
// Same pattern as the engine's AnimationWarpingEditor module.
public class RammsCrowdUncooked : ModuleRules
{
	public RammsCrowdUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph",
			"AnimGraphRuntime",
			"AnimationCore",
			"AnimationWarpingRuntime",
			"Core",
			"CoreUObject",
			"Engine",
			"RammsCrowd",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SlateCore",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"BlueprintGraph",
				"EditorFramework",
				"Kismet",
				"UnrealEd",
			});
		}
	}
}
