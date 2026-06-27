// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshTerrainRuntimeLabEditor : ModuleRules
{
	public MeshTerrainRuntimeLabEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"MeshConversion",
				"MeshDescription",
				"MeshTerrainRuntimeLab",
				"StaticMeshDescription",
				"UnrealEd"
			}
			);
	}
}
