// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshTerrainRuntimeLab : ModuleRules
{
	public MeshTerrainRuntimeLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Engine/Internal"),
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"MeshConversion",
				"MeshDescription",
				"MeshPartition",
				"StaticMeshDescription",
				// ... add private dependencies that you statically link with here ...
			}
			);

		// SPIKE: manual runtime Nanite build path (Nanite::IBuilderModule). NaniteBuilder is a
		// Developer module and is safe to reference from editor targets. Do not link it from Game /
		// packaged targets here: installed-engine monolithic builds do not ship the required
		// Developer module manifests, and UE 5.8 ReadOnlyTargetRules does not expose a stable
		// "source engine vs installed engine" switch at module-rule level. A packaged Nanite spike
		// should be a separate source-engine experiment.
		bool bWithRuntimeNaniteBuilder = Target.Type == TargetType.Editor;
		if (bWithRuntimeNaniteBuilder)
		{
			PrivateDependencyModuleNames.Add("NaniteBuilder");
		}
		PublicDefinitions.Add("MTR_WITH_RUNTIME_NANITE_BUILDER=" + (bWithRuntimeNaniteBuilder ? "1" : "0"));


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
