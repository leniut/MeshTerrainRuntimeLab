// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeshTerrainRuntimeLabActor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

namespace UE::MeshPartition
{
class ACompiledSection;
class AMeshPartition;
class FMeshData;
class UMeshPartitionDefinition;
struct FCompiledSectionBuildInfo;
}

UENUM(BlueprintType)
enum class EMeshTerrainRuntimeLabHeightMode : uint8
{
	Flat UMETA(DisplayName = "Flat"),
	Sine UMETA(DisplayName = "Sine"),
	Noise UMETA(DisplayName = "Noise"),
	HeightData UMETA(DisplayName = "Height Data")
};

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
	bool SetHeightData(const TArray<float>& InHeights, int32 InWidth, int32 InHeight, bool bRebuildNow = true);

	UFUNCTION(BlueprintCallable, Category = "Mesh Terrain Runtime Lab")
	void ClearBuiltTerrain();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Height Data", meta = (AllowPrivateAccess = "true"))
	TArray<float> HeightData;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Height Data", meta = (AllowPrivateAccess = "true"))
	int32 HeightDataWidth = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Height Data", meta = (AllowPrivateAccess = "true"))
	int32 HeightDataHeight = 0;

	bool HasValidHeightData() const;
	double SampleHeightData(double U, double V) const;
	double EvaluateHeight(double U, double V) const;
	UE::MeshPartition::UMeshPartitionDefinition* ResolveMeshPartitionDefinition() const;
	UMaterialInterface* ResolveTerrainMaterial() const;
	UE::MeshPartition::ACompiledSection* CreateRuntimeCompiledSection(
		UE::MeshPartition::AMeshPartition* MeshPartition,
		const UE::MeshPartition::FCompiledSectionBuildInfo& BuildInfo);
	void ApplyMinimalRuntimeChannelData(
		UE::MeshPartition::ACompiledSection* CompiledSection,
		const UE::MeshPartition::UMeshPartitionDefinition* Definition);

	/**
	 * Non-editor runtime parity: ACompiledSection::SetMaterialInstance only accepts a
	 * UMaterialInstanceConstant (not creatable at runtime) and its channel-texture binding is
	 * editor-only. So in packaged/Shipping builds this assigns a UMaterialInstanceDynamic (with the
	 * channel texture bound) directly to the section's generated mesh components. No-op in editor,
	 * where the UMaterialInstanceConstant path already runs.
	 */
	void ApplyRuntimeMaterialInstance(
		UE::MeshPartition::ACompiledSection* CompiledSection,
		UMaterialInterface* ResolvedMaterial);
	UE::MeshPartition::FMeshData CreateFlatRuntimeMeshData() const;
	UStaticMesh* CreateFlatRuntimeStaticMesh(FBox2f& OutUVRegion);

	/**
	 * Manual runtime Nanite build: constructs Nanite::IBuilderModule::FInputMeshData from the
	 * runtime grid, calls IBuilderModule::Build() to produce Nanite::FResources, attaches them to
	 * the static mesh render data and uploads them via InitResources. Returns true if valid Nanite
	 * data was produced. In this plugin it is compiled for editor targets only; packaged Nanite
	 * remains a separate source-engine spike.
	 */
	bool BuildAndInjectRuntimeNanite(UStaticMesh* StaticMesh, const UE::MeshPartition::FMeshData& MeshData) const;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|Height")
	EMeshTerrainRuntimeLabHeightMode HeightMode = EMeshTerrainRuntimeLabHeightMode::Flat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|Height", meta = (ClampMin = "0.0", UIMin = "0.0"))
	double HeightScale = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|Height", meta = (ClampMin = "0.001", UIMin = "0.1"))
	double HeightFrequency = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "0.001", UIMin = "0.1"))
	double UVTilingX = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab", meta = (ClampMin = "0.001", UIMin = "0.1"))
	double UVTilingY = 10.0;

	/**
	 * Build simple collision for the runtime terrain. IMPORTANT: this is ignored for the render build
	 * variant (HighEndPlatform_RenderData, the default BuildVariantName), because render sections
	 * mirror the editor and never carry collision. To actually get collision, select a gameplay build
	 * variant; otherwise the terrain stays NoCollision regardless of this flag (a warning is logged).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab")
	bool bBuildSimpleCollision = true;

	/**
	 * Adds the same 10-unit edge skirt that the editor StaticMeshTransformer uses for section
	 * bounds/parity checks. Keep this disabled for visual runtime tests: the main terrain surface
	 * is flat, but the lowered border intentionally looks like a lip and casts edge shadows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|MeshPartition")
	bool bApplyEditorSkirtForParity = false;

	/**
	 * Build real Nanite render data for the runtime terrain mesh, matching the editor render
	 * variant (HighEndPlatform_RenderData).
	 *
	 * SPIKE / HARD ENGINE CONSTRAINT: this only works in Editor/PIE builds. UStaticMesh's full
	 * (Nanite-capable) build path is WITH_EDITOR-only; in packaged/Shipping builds
	 * BuildFromMeshDescriptions hard-asserts bFastBuild==true and the fast path never builds
	 * Nanite. In non-editor builds this flag is therefore ignored (no Nanite). True runtime
	 * Nanite in a packaged game would require linking the Developer NaniteBuilder module and
	 * calling Nanite::IBuilderModule::Build() directly (the Voxel Plugin approach) - out of scope
	 * for this spike. Note: the editor full build is significantly slower than the fast build.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|Nanite")
	bool bBuildNaniteData = true;

	/**
	 * Use the manual Nanite build path (Nanite::IBuilderModule::Build -> FResources ->
	 * RenderData->NaniteResourcesPtr -> InitResources) instead of relying on the editor-only
	 * UStaticMesh full build. This is the ONLY route that can produce Nanite in a packaged
	 * (non-editor) runtime build (Voxel Plugin approach), but this plugin currently links that
	 * Developer module only for Editor/PIE targets. Packaged Nanite remains a separate
	 * source-engine experiment; when this path is unavailable the diagnostic stays false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|Nanite")
	bool bUseManualNaniteBuilder = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/**
	 * Optional MeshPartition definition asset. Leave empty (default) to defer to the engine's
	 * configured default definition, exactly as AMeshPartition::InitializeDefinition() does:
	 * MeshPartition::USettings::GetDefaultDefinition() (DefaultMeshPartition.ini ->
	 * /MeshPartition/DataAssets/MPD_Default). If that cannot be resolved either,
	 * ResolveMeshPartitionDefinition() falls back to the class default object.
	 * Set this only to force a specific custom definition.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|MeshPartition")
	TSoftObjectPtr<UE::MeshPartition::UMeshPartitionDefinition> MeshPartitionDefinition;

	/**
	 * Build variant of the definition this terrain targets. Defaults to the render variant of
	 * MPD_Default ("HighEndPlatform_RenderData"), which carries the StaticMeshTransformer
	 * (Nanite render data) rather than "Common_GameplayData" (collision/gameplay only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Terrain Runtime Lab|MeshPartition")
	FName BuildVariantName = TEXT("HighEndPlatform_RenderData");

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<AActor> SpawnedMeshPartition = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<AActor> SpawnedCompiledSection = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab")
	TObjectPtr<UStaticMesh> RuntimeStaticMesh = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Diagnostics")
	int32 RuntimeChannelTableLength = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Diagnostics")
	FVector2D RuntimeChannelTexcoordDesc = FVector2D::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Diagnostics")
	FString RuntimeChannelTexturePath;

	/** Whether the last runtime build produced valid Nanite data (editor/PIE only). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Mesh Terrain Runtime Lab|Diagnostics")
	bool bRuntimeNaniteDataValid = false;
};
