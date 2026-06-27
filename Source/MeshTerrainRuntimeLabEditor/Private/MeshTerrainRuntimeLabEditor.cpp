// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabEditor.h"

#include "MeshTerrainRuntimeLabParityToolset.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

void FMeshTerrainRuntimeLabEditorModule::StartupModule()
{
	UToolsetRegistry::RegisterToolsetClass(UMeshTerrainRuntimeLabParityToolset::StaticClass());
}

void FMeshTerrainRuntimeLabEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UToolsetRegistry::UnregisterToolsetClass(UMeshTerrainRuntimeLabParityToolset::StaticClass());
	}
}

IMPLEMENT_MODULE(FMeshTerrainRuntimeLabEditorModule, MeshTerrainRuntimeLabEditor)
