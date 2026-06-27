// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeshTerrainRuntimeLabActor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/**
 * Runtime lab actor that creates a minimal MeshPartition compiled section.
 *
 * This is intentionally narrow: it proves that runtime-generated geometry can be
 * attached to real MeshPartition runtime actors before moving more of Epic's
 * editor build pipeline into a runtime compiler.
 */
UCLASS(BlueprintType, Blueprintable)
class MESHTERRAINRUNTIMELAB_API AMeshTerrainRuntimeLabActor : public AActor
{
	GENERATED_BODY()

public:
	AMeshTerrainRuntimeLabActor();

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab")
	AActor* RebuildTerrain();

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab")
	AActor* BuildFlatMeshTerrain();

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab")
	void SetTerrainMaterial(UMaterialInterface* NewMaterial, bool bRebuildNow = true);

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab")
	void ClearBuiltTerrain();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UStaticMesh* CreateFlatRuntimeStaticMesh();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab")
	bool bBuildOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "1", UIMin = "1"))
	int32 QuadsX = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "1", UIMin = "1"))
	int32 QuadsY = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "1.0", UIMin = "100.0"))
	double SizeX = 2000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "1.0", UIMin = "100.0"))
	double SizeY = 2000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "0.001", UIMin = "0.1"))
	double UVTilingX = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "0.001", UIMin = "0.1"))
	double UVTilingY = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab")
	bool bBuildSimpleCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<AActor> SpawnedMeshPartition = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<AActor> SpawnedCompiledSection = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<UStaticMesh> RuntimeStaticMesh = nullptr;
};
