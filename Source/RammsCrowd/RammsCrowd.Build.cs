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

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
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
