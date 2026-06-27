// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MeshTerrainRuntimeLabBakeLibrary.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Editor-only utilities for baking runtime terrain experiments into real StaticMesh assets.
 */
UCLASS()
class MESHTERRAINRUNTIMELABEDITOR_API UMeshTerrainRuntimeLabBakeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Mesh Terrain Runtime Lab|Bake")
	static UStaticMesh* BakeFlatNaniteChunk(
		const FString& PackagePath = TEXT("/Game/MeshTerrainRuntimeLab/Baked/SM_MTR_BakedChunk_0_0"),
		int32 QuadsX = 64,
		int32 QuadsY = 64,
		double SizeX = 10000.0,
		double SizeY = 10000.0,
		double UVTilingX = 10.0,
		double UVTilingY = 10.0,
		UMaterialInterface* Material = nullptr,
		bool bEnableNanite = true,
		bool bBuildSimpleCollision = true,
		bool bSaveAsset = true);

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab|Bake")
	static bool StaticMeshHasValidNaniteData(UStaticMesh* StaticMesh);
};
