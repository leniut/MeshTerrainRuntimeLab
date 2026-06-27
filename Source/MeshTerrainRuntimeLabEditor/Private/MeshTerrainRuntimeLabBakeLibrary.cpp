// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabBakeLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MeshTerrainRuntimeLabEditor
{
	FString MakeObjectPathFromPackagePath(const FString& PackagePath)
	{
		FString AssetName;
		PackagePath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
	}

	FVector ComputeGridNormal(
		const TArray<FVector>& VertexPositions,
		int32 X,
		int32 Y,
		int32 QuadsX,
		int32 QuadsY)
	{
		const auto GetVertexIndex = [QuadsX](int32 InX, int32 InY)
		{
			return InY * (QuadsX + 1) + InX;
		};

		const int32 LeftX = FMath::Max(0, X - 1);
		const int32 RightX = FMath::Min(QuadsX, X + 1);
		const int32 DownY = FMath::Max(0, Y - 1);
		const int32 UpY = FMath::Min(QuadsY, Y + 1);

		const FVector TangentX = VertexPositions[GetVertexIndex(RightX, Y)] - VertexPositions[GetVertexIndex(LeftX, Y)];
		const FVector TangentY = VertexPositions[GetVertexIndex(X, UpY)] - VertexPositions[GetVertexIndex(X, DownY)];

		FVector Normal = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		if (FVector::DotProduct(Normal, FVector::UpVector) < 0.0)
		{
			Normal *= -1.0;
		}

		return Normal;
	}

	bool BuildFlatChunkMeshDescription(
		FMeshDescription& MeshDescription,
		int32 QuadsX,
		int32 QuadsY,
		double SizeX,
		double SizeY,
		double UVTilingX,
		double UVTilingY)
	{
		if (QuadsX < 1 || QuadsY < 1 || SizeX <= 0.0 || SizeY <= 0.0 || UVTilingX <= 0.0 || UVTilingY <= 0.0)
		{
			return false;
		}

		FStaticMeshAttributes(MeshDescription).Register();

		FMeshDescriptionBuilder Builder;
		Builder.SetMeshDescription(&MeshDescription);
		Builder.SetNumUVLayers(1);
		Builder.ReserveNewVertices((QuadsX + 1) * (QuadsY + 1));

		const FPolygonGroupID PolygonGroup = Builder.AppendPolygonGroup(TEXT("BakedTerrain"));

		TArray<FVertexID> Vertices;
		Vertices.SetNum((QuadsX + 1) * (QuadsY + 1));

		TArray<FVector> VertexPositions;
		VertexPositions.SetNum(Vertices.Num());

		const auto GetVertexIndex = [QuadsX](int32 X, int32 Y)
		{
			return Y * (QuadsX + 1) + X;
		};

		for (int32 Y = 0; Y <= QuadsY; ++Y)
		{
			for (int32 X = 0; X <= QuadsX; ++X)
			{
				const double U = static_cast<double>(X) / static_cast<double>(QuadsX);
				const double V = static_cast<double>(Y) / static_cast<double>(QuadsY);
				const FVector Position((U - 0.5) * SizeX, (V - 0.5) * SizeY, 0.0);

				const int32 VertexIndex = GetVertexIndex(X, Y);
				VertexPositions[VertexIndex] = Position;
				Vertices[VertexIndex] = Builder.AppendVertex(Position);
			}
		}

		TArray<FVector> VertexNormals;
		VertexNormals.SetNum(Vertices.Num());

		TArray<FVector> VertexTangents;
		VertexTangents.SetNum(Vertices.Num());

		for (int32 Y = 0; Y <= QuadsY; ++Y)
		{
			for (int32 X = 0; X <= QuadsX; ++X)
			{
				const int32 VertexIndex = GetVertexIndex(X, Y);
				const FVector Normal = ComputeGridNormal(VertexPositions, X, Y, QuadsX, QuadsY);
				FVector Tangent = FVector::ForwardVector - Normal * FVector::DotProduct(FVector::ForwardVector, Normal);
				Tangent = Tangent.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

				VertexNormals[VertexIndex] = Normal;
				VertexTangents[VertexIndex] = Tangent;
			}
		}

		auto AddTriangle = [&Builder, &Vertices, &VertexNormals, &VertexTangents, QuadsX, QuadsY, UVTilingX, UVTilingY](int32 A, int32 B, int32 C, const FPolygonGroupID& Group)
		{
			const FVertexInstanceID InstanceA = Builder.AppendInstance(Vertices[A]);
			const FVertexInstanceID InstanceB = Builder.AppendInstance(Vertices[B]);
			const FVertexInstanceID InstanceC = Builder.AppendInstance(Vertices[C]);

			Builder.SetInstanceTangentSpace(InstanceA, VertexNormals[A], VertexTangents[A], 1.0f);
			Builder.SetInstanceTangentSpace(InstanceB, VertexNormals[B], VertexTangents[B], 1.0f);
			Builder.SetInstanceTangentSpace(InstanceC, VertexNormals[C], VertexTangents[C], 1.0f);

			const auto SetInstanceUV = [&Builder, QuadsX, QuadsY, UVTilingX, UVTilingY](const FVertexInstanceID& Instance, int32 VertexIndex)
			{
				const int32 VertexX = VertexIndex % (QuadsX + 1);
				const int32 VertexY = VertexIndex / (QuadsX + 1);
				const FVector2D UV(
					static_cast<double>(VertexX) / static_cast<double>(QuadsX) * UVTilingX,
					static_cast<double>(VertexY) / static_cast<double>(QuadsY) * UVTilingY);
				Builder.SetInstanceUV(Instance, UV, 0);
			};

			SetInstanceUV(InstanceA, A);
			SetInstanceUV(InstanceB, B);
			SetInstanceUV(InstanceC, C);

			Builder.AppendTriangle(InstanceA, InstanceB, InstanceC, Group);
		};

		for (int32 Y = 0; Y < QuadsY; ++Y)
		{
			for (int32 X = 0; X < QuadsX; ++X)
			{
				const int32 Row0 = Y * (QuadsX + 1);
				const int32 Row1 = (Y + 1) * (QuadsX + 1);

				const int32 V00 = Row0 + X;
				const int32 V10 = Row0 + X + 1;
				const int32 V01 = Row1 + X;
				const int32 V11 = Row1 + X + 1;

				AddTriangle(V00, V01, V11, PolygonGroup);
				AddTriangle(V00, V11, V10, PolygonGroup);
			}
		}

		return true;
	}
}

UStaticMesh* UMeshTerrainRuntimeLabBakeLibrary::BakeFlatNaniteChunk(
	const FString& PackagePath,
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY,
	UMaterialInterface* Material,
	bool bEnableNanite,
	bool bBuildSimpleCollision,
	bool bSaveAsset)
{
	const FString SanitizedPackagePath = PackagePath.TrimStartAndEnd();
	if (!FPackageName::IsValidLongPackageName(SanitizedPackagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: '%s' is not a valid long package path."), *SanitizedPackagePath);
		return nullptr;
	}

	const int32 ClampedQuadsX = FMath::Max(1, QuadsX);
	const int32 ClampedQuadsY = FMath::Max(1, QuadsY);
	const double ClampedSizeX = FMath::Max(1.0, SizeX);
	const double ClampedSizeY = FMath::Max(1.0, SizeY);
	const double ClampedUVTilingX = FMath::Max(0.001, UVTilingX);
	const double ClampedUVTilingY = FMath::Max(0.001, UVTilingY);

	FString AssetName;
	SanitizedPackagePath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: '%s' has no asset name."), *SanitizedPackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*SanitizedPackagePath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: could not create package '%s'."), *SanitizedPackagePath);
		return nullptr;
	}

	Package->FullyLoad();

	const FString ObjectPath = MeshTerrainRuntimeLabEditor::MakeObjectPathFromPackagePath(SanitizedPackagePath);
	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
	const bool bIsNewAsset = StaticMesh == nullptr;

	if (!StaticMesh)
	{
		StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	if (!StaticMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: could not create static mesh '%s'."), *ObjectPath);
		return nullptr;
	}

	FMeshDescription MeshDescription;
	if (!MeshTerrainRuntimeLabEditor::BuildFlatChunkMeshDescription(
		MeshDescription,
		ClampedQuadsX,
		ClampedQuadsY,
		ClampedSizeX,
		ClampedSizeY,
		ClampedUVTilingX,
		ClampedUVTilingY))
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: invalid mesh generation input."));
		return nullptr;
	}

	StaticMesh->Modify();
	StaticMesh->SetImportVersion(EImportStaticMeshVersion::LastVersion);
	StaticMesh->SetLightingGuid();

	UMaterialInterface* MeshMaterial = Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
	StaticMesh->GetStaticMaterials().Reset();
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial(MeshMaterial, TEXT("BakedTerrain")));

	FMeshNaniteSettings NaniteSettings = StaticMesh->GetNaniteSettings();
	NaniteSettings.bEnabled = bEnableNanite;
	NaniteSettings.FallbackPercentTriangles = 1.0f;
	StaticMesh->SetNaniteSettings(NaniteSettings);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = false;
	BuildParams.bCommitMeshDescription = true;
	BuildParams.bMarkPackageDirty = true;
	BuildParams.bBuildSimpleCollision = bBuildSimpleCollision;
	BuildParams.bAllowCpuAccess = false;

	UStaticMesh::FBuildMeshDescriptionsLODParams LODParams;
	LODParams.bUseFullPrecisionUVs = false;
	LODParams.bUseHighPrecisionTangentBasis = false;
	BuildParams.PerLODOverrides.Add(LODParams);

	if (!StaticMesh->BuildFromMeshDescriptions({ &MeshDescription }, BuildParams))
	{
		UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: StaticMesh build failed for '%s'."), *ObjectPath);
		return nullptr;
	}

	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = bBuildSimpleCollision ? CTF_UseSimpleAsComplex : CTF_UseDefault;
	}

	StaticMesh->PostEditChange();
	StaticMesh->MarkPackageDirty();

	if (bIsNewAsset)
	{
		FAssetRegistryModule::AssetCreated(StaticMesh);
	}

	if (bSaveAsset)
	{
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			SanitizedPackagePath,
			FPackageName::GetAssetPackageExtension());

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		if (!UPackage::SavePackage(Package, StaticMesh, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("BakeFlatNaniteChunk failed: could not save '%s'."), *PackageFilename);
			return nullptr;
		}
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("BakeFlatNaniteChunk created '%s' with NaniteEnabled=%s HasValidNaniteData=%s."),
		*ObjectPath,
		bEnableNanite ? TEXT("true") : TEXT("false"),
		StaticMesh->HasValidNaniteData() ? TEXT("true") : TEXT("false"));

	return StaticMesh;
}

bool UMeshTerrainRuntimeLabBakeLibrary::StaticMeshHasValidNaniteData(UStaticMesh* StaticMesh)
{
	return StaticMesh && StaticMesh->HasValidNaniteData();
}
