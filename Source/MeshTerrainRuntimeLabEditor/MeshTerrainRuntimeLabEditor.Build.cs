// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
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
				"Json",
				"MeshConversion",
				"MeshDescription",
				"MeshPartition",
				"MeshPartitionEditor",
				"MeshTerrainRuntimeLab",
				"StaticMeshDescription",
				"ToolsetRegistry",
				"UnrealEd"
			}
			);

		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Engine", "Internal"));
	}
}
