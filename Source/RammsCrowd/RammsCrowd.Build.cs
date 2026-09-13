// Copyright (c) RAMMP. All rights reserved.

using UnrealBuildTool;

public class RammsCrowd : ModuleRules
{
	public RammsCrowd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MassEntity",
				"MassCommon",
				"MassActors",
				"MassSpawner",
				"MassMovement",
				"MassNavigation",
				"MassAIBehavior",
				"MassSignals",
				"StateTreeModule",
				"ZoneGraph",
			}
			);

		// UE 5.8 moved the core Mass types (FMassFragment, FTransformFragment,
		// FMassEntityHandle, ...) out of MassEntity into the new MassCore module.
		if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8))
		{
			PublicDependencyModuleNames.Add("MassCore");
		}

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraphRuntime",
				"AnimationCore",
				"AnimationWarpingRuntime",
				"MassCrowd",
				"MassZoneGraphNavigation",
				"MassNavMeshNavigation",
				"MassRepresentation",
				"MassLOD",
				"ZoneGraphAnnotations",
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd"
				}
				);
		}
	}
}
