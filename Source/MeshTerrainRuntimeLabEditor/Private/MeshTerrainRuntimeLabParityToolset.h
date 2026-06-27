// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "MeshTerrainRuntimeLabParityToolset.generated.h"

class AActor;
class UStaticMesh;

namespace UE::MeshPartition
{
class UMeshPartitionDefinition;
}

/** Inspect MeshPartition/MeshTerrain objects for runtime parity checks through Unreal MCP. */
UCLASS(Blueprintable)
class UMeshTerrainRuntimeLabParityToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/** Returns a JSON snapshot of a MeshPartition actor. */
	UFUNCTION(meta = (AIIgnore), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotMeshPartition(AActor* Actor);

	/** Finds a MeshPartition actor by object path and returns SnapshotMeshPartition. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotMeshPartitionByPath(const FString& ActorPath);

	/** Returns a JSON snapshot of a CompiledSection actor. */
	UFUNCTION(meta = (AIIgnore), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotCompiledSection(AActor* Actor);

	/** Finds a CompiledSection actor by object path and returns SnapshotCompiledSection. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotCompiledSectionByPath(const FString& ActorPath);

	/** Returns a JSON snapshot of a MeshPartitionDefinition asset. */
	UFUNCTION(meta = (AIIgnore), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotMeshPartitionDefinition(UE::MeshPartition::UMeshPartitionDefinition* Definition);

	/** Loads a MeshPartitionDefinition by object path and returns SnapshotMeshPartitionDefinition. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotMeshPartitionDefinitionByPath(const FString& DefinitionPath);

	/** Returns a JSON snapshot of a StaticMesh used by a compiled section, including Nanite settings. */
	UFUNCTION(meta = (AIIgnore), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotStaticMesh(UStaticMesh* StaticMesh);

	/** Loads a StaticMesh by object path and returns SnapshotStaticMesh. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString SnapshotStaticMeshByPath(const FString& StaticMeshPath);

	/** Compares two MeshPartitionDefinition assets and returns a JSON difference summary. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString CompareMeshPartitionDefinitionsByPath(const FString& LeftDefinitionPath, const FString& RightDefinitionPath);

	/** Builds and snapshots a transient editor-pipeline CompiledSection without saving the level. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString BuildTransientReferenceCompiledSectionByPath(
		const FString& ActorPath,
		const FString& BuildVariantName,
		int32 QuadsX,
		int32 QuadsY,
		double SizeX,
		double SizeY,
		double UVTilingX,
		double UVTilingY);

	/** Builds and snapshots a transient editor-pipeline CompiledSection from a definition asset without saving the level. */
	UFUNCTION(meta = (AICallable), Category = "MeshTerrainRuntimeLab")
	static FString BuildTransientReferenceCompiledSectionForDefinitionByPath(
		const FString& DefinitionPath,
		const FString& BuildVariantName,
		int32 QuadsX,
		int32 QuadsY,
		double SizeX,
		double SizeY,
		double UVTilingX,
		double UVTilingY);
};
