// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabActor.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "MeshPartition.h"
#include "MeshPartitionChannel.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionMeshData.h"
#include "MeshPartitionStaticMeshDescriptor.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "Components/SceneComponent.h"

#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#endif

namespace
{
const FName MeshPartitionStaticMeshMaterialSlotName(TEXT("MegaMeshStaticMeshMaterial"));
}

AMeshTerrainRuntimeLabActor::AMeshTerrainRuntimeLabActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void AMeshTerrainRuntimeLabActor::BeginPlay()
{
	Super::BeginPlay();

	if (bBuildOnBeginPlay)
	{
		RebuildTerrain();
	}
}

AActor* AMeshTerrainRuntimeLabActor::BuildFlatMeshTerrain()
{
	return RebuildTerrain();
}

AActor* AMeshTerrainRuntimeLabActor::RebuildTerrain()
{
	ClearBuiltTerrain();

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	RuntimeStaticMesh = CreateFlatRuntimeStaticMesh();
	if (!RuntimeStaticMesh)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	UE::MeshPartition::AMeshPartition* MeshPartition = World->SpawnActor<UE::MeshPartition::AMeshPartition>(
		UE::MeshPartition::AMeshPartition::StaticClass(),
		GetActorTransform(),
		SpawnParams);

	if (!MeshPartition)
	{
		RuntimeStaticMesh = nullptr;
		return nullptr;
	}

	UE::MeshPartition::UMeshPartitionDefinition* ResolvedDefinition = ResolveMeshPartitionDefinition();
	if (ResolvedDefinition)
	{
		MeshPartition->SetMeshPartitionDefinition(ResolvedDefinition);
	}

	UE::MeshPartition::FCompiledSectionBuildInfo BuildInfo;
	BuildInfo.BuildKey = FGuid::NewGuid();
	BuildInfo.BuildVariantName = BuildVariantName.IsNone() ? NAME_Default : BuildVariantName;
#if WITH_EDITORONLY_DATA
	BuildInfo.MegaMeshGUID = MeshPartition->GetActorGuid().IsValid() ? MeshPartition->GetActorGuid() : FGuid::NewGuid();
#else
	BuildInfo.MegaMeshGUID = FGuid::NewGuid();
#endif
	BuildInfo.MegaMeshPath = MeshPartition;
	BuildInfo.ModifiersHash = FGuid::NewGuid();
	BuildInfo.ModifierSetHash = FGuid::NewGuid();
	BuildInfo.PackageHash = FGuid::NewGuid();
	BuildInfo.ClassHash = FGuid::NewGuid();
	BuildInfo.BuildVariantHash = FGuid::NewGuid();

	if (ResolvedDefinition)
	{
		BuildInfo.SetMegaMeshDefinition(ResolvedDefinition);
	}

	UE::MeshPartition::ACompiledSection* CompiledSection = CreateRuntimeCompiledSection(MeshPartition, BuildInfo);

	if (!CompiledSection)
	{
		MeshPartition->Destroy();
		RuntimeStaticMesh = nullptr;
		return nullptr;
	}

	UE::MeshPartition::FStaticMeshDescriptor Descriptor;
	Descriptor.CollisionProfileName = bBuildSimpleCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName;
	Descriptor.bCanEverAffectNavigation = bBuildSimpleCollision;
	Descriptor.UVRegion = FBox2f(FVector2f::ZeroVector, FVector2f(1.0f, 1.0f));

	ApplyMinimalRuntimeChannelData(CompiledSection, ResolvedDefinition);
	CompiledSection->AddStaticMesh(RuntimeStaticMesh, Descriptor);

	SpawnedMeshPartition = MeshPartition;
	SpawnedCompiledSection = CompiledSection;

	return CompiledSection;
}

void AMeshTerrainRuntimeLabActor::SetTerrainMaterial(UMaterialInterface* NewMaterial, bool bRebuildNow)
{
	Material = NewMaterial;

	if (bRebuildNow)
	{
		RebuildTerrain();
	}
}

bool AMeshTerrainRuntimeLabActor::SetHeightData(const TArray<float>& InHeights, int32 InWidth, int32 InHeight, bool bRebuildNow)
{
	const bool bIsValid = InWidth >= 2
		&& InHeight >= 2
		&& InHeights.Num() == InWidth * InHeight;

	if (!bIsValid)
	{
		HeightData.Reset();
		HeightDataWidth = 0;
		HeightDataHeight = 0;

		if (HeightMode == EMeshTerrainRuntimeLabHeightMode::HeightData)
		{
			HeightMode = EMeshTerrainRuntimeLabHeightMode::Flat;
		}

		if (bRebuildNow)
		{
			RebuildTerrain();
		}

		return false;
	}

	HeightData = InHeights;
	HeightDataWidth = InWidth;
	HeightDataHeight = InHeight;
	HeightMode = EMeshTerrainRuntimeLabHeightMode::HeightData;

	if (bRebuildNow)
	{
		RebuildTerrain();
	}

	return true;
}

void AMeshTerrainRuntimeLabActor::ClearBuiltTerrain()
{
	if (SpawnedCompiledSection)
	{
		SpawnedCompiledSection->Destroy();
		SpawnedCompiledSection = nullptr;
	}

	if (SpawnedMeshPartition)
	{
		SpawnedMeshPartition->Destroy();
		SpawnedMeshPartition = nullptr;
	}

	RuntimeStaticMesh = nullptr;
	RuntimeChannelTableLength = 0;
	RuntimeChannelTexcoordDesc = FVector2D::ZeroVector;
}

bool AMeshTerrainRuntimeLabActor::HasValidHeightData() const
{
	return HeightDataWidth >= 2
		&& HeightDataHeight >= 2
		&& HeightData.Num() == HeightDataWidth * HeightDataHeight;
}

double AMeshTerrainRuntimeLabActor::SampleHeightData(double U, double V) const
{
	if (!HasValidHeightData())
	{
		return 0.0;
	}

	const double SampleX = FMath::Clamp(U, 0.0, 1.0) * static_cast<double>(HeightDataWidth - 1);
	const double SampleY = FMath::Clamp(V, 0.0, 1.0) * static_cast<double>(HeightDataHeight - 1);

	const int32 X0 = FMath::Clamp(FMath::FloorToInt(SampleX), 0, HeightDataWidth - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SampleY), 0, HeightDataHeight - 1);
	const int32 X1 = FMath::Min(X0 + 1, HeightDataWidth - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, HeightDataHeight - 1);

	const double AlphaX = SampleX - static_cast<double>(X0);
	const double AlphaY = SampleY - static_cast<double>(Y0);

	const auto GetHeight = [this](int32 X, int32 Y)
	{
		return static_cast<double>(HeightData[Y * HeightDataWidth + X]);
	};

	const double Height00 = GetHeight(X0, Y0);
	const double Height10 = GetHeight(X1, Y0);
	const double Height01 = GetHeight(X0, Y1);
	const double Height11 = GetHeight(X1, Y1);

	const double HeightX0 = FMath::Lerp(Height00, Height10, AlphaX);
	const double HeightX1 = FMath::Lerp(Height01, Height11, AlphaX);

	return FMath::Lerp(HeightX0, HeightX1, AlphaY);
}

double AMeshTerrainRuntimeLabActor::EvaluateHeight(double U, double V) const
{
	const double ClampedHeightScale = FMath::Max(0.0, HeightScale);
	const double ClampedHeightFrequency = FMath::Max(0.001, HeightFrequency);

	switch (HeightMode)
	{
	case EMeshTerrainRuntimeLabHeightMode::Sine:
		return FMath::Sin(U * UE_DOUBLE_TWO_PI * ClampedHeightFrequency)
			* FMath::Cos(V * UE_DOUBLE_TWO_PI * ClampedHeightFrequency)
			* ClampedHeightScale;

	case EMeshTerrainRuntimeLabHeightMode::Noise:
		return FMath::PerlinNoise2D(FVector2D(U * ClampedHeightFrequency, V * ClampedHeightFrequency))
			* ClampedHeightScale;

	case EMeshTerrainRuntimeLabHeightMode::HeightData:
		return SampleHeightData(U, V);

	case EMeshTerrainRuntimeLabHeightMode::Flat:
	default:
		return 0.0;
	}
}

UE::MeshPartition::UMeshPartitionDefinition* AMeshTerrainRuntimeLabActor::ResolveMeshPartitionDefinition() const
{
	if (MeshPartitionDefinition.IsNull())
	{
		return nullptr;
	}

	return MeshPartitionDefinition.LoadSynchronous();
}

UMaterialInterface* AMeshTerrainRuntimeLabActor::ResolveTerrainMaterial() const
{
	UMaterialInterface* MeshMaterial = Material ? Material.Get() : nullptr;

	if (!MeshMaterial)
	{
		if (const UE::MeshPartition::UMeshPartitionDefinition* ResolvedDefinition = ResolveMeshPartitionDefinition())
		{
			MeshMaterial = ResolvedDefinition->GetMaterial();
		}
	}

	return MeshMaterial ? MeshMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
}

UE::MeshPartition::ACompiledSection* AMeshTerrainRuntimeLabActor::CreateRuntimeCompiledSection(
	UE::MeshPartition::AMeshPartition* MeshPartition,
	const UE::MeshPartition::FCompiledSectionBuildInfo& BuildInfo)
{
	UWorld* World = GetWorld();
	if (!World || !MeshPartition)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	UE::MeshPartition::ACompiledSection* Section = World->SpawnActor<UE::MeshPartition::ACompiledSection>(
		UE::MeshPartition::ACompiledSection::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Section)
	{
		return nullptr;
	}

#if WITH_EDITOR
	Section->SetActorLabel(TEXT("CompiledSection_") + BuildInfo.BuildVariantName.ToString());
#endif

	Section->SetParent(MeshPartition);

	if (USceneComponent* SectionRoot = Section->GetRootComponent())
	{
		SectionRoot->SetMobility(EComponentMobility::Static);
	}

	Section->SetBuildInfo(BuildInfo);

#if WITH_EDITOR
	UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(
		Section->GetPackage(),
		MakeUniqueObjectName(Section, UMaterialInstanceConstant::StaticClass(), TEXT("CompiledSectionMIC")));

	if (MaterialInstance)
	{
		MaterialInstance->SetParentEditorOnly(ResolveTerrainMaterial(), false);
		Section->SetMaterialInstance(MaterialInstance);
	}
#endif

	return Section;
}

void AMeshTerrainRuntimeLabActor::ApplyMinimalRuntimeChannelData(
	UE::MeshPartition::ACompiledSection* CompiledSection,
	const UE::MeshPartition::UMeshPartitionDefinition* Definition)
{
	if (!CompiledSection)
	{
		return;
	}

	const int32 NumChannels = Definition ? FMath::Max(0, Definition->GetChannelMap().GetNumChannels()) : 0;

	TArray<uint8> ChannelTable;
	ChannelTable.Init(UE::MeshPartition::FChannelPacking::SlotInvalid, NumChannels);

	const float UnitPerU = static_cast<float>(FMath::Max(1.0, SizeX) / FMath::Max(0.001, UVTilingX));
	const float UnitPerV = static_cast<float>(FMath::Max(1.0, SizeY) / FMath::Max(0.001, UVTilingY));
	const FVector2f ChannelTexcoordDesc(UnitPerU, UnitPerV);
	CompiledSection->SetChannelData(ChannelTable, ChannelTexcoordDesc);

	RuntimeChannelTableLength = ChannelTable.Num();
	RuntimeChannelTexcoordDesc = FVector2D(
		static_cast<double>(ChannelTexcoordDesc.X),
		static_cast<double>(ChannelTexcoordDesc.Y));
}

UE::MeshPartition::FMeshData AMeshTerrainRuntimeLabActor::CreateFlatRuntimeMeshData() const
{
	const int32 ClampedQuadsX = FMath::Max(1, QuadsX);
	const int32 ClampedQuadsY = FMath::Max(1, QuadsY);
	const double ClampedSizeX = FMath::Max(1.0, SizeX);
	const double ClampedSizeY = FMath::Max(1.0, SizeY);
	const double ClampedUVTilingX = FMath::Max(0.001, UVTilingX);
	const double ClampedUVTilingY = FMath::Max(0.001, UVTilingY);

	UE::MeshPartition::FMeshData MeshData;
	MeshData.SetNumSourceUVChannels(1);
	MeshData.ReserveAdditionalVertices((ClampedQuadsX + 1) * (ClampedQuadsY + 1));
	MeshData.ReserveAdditionalTriangles(ClampedQuadsX * ClampedQuadsY * 2);

	TArray<int32> Vertices;
	Vertices.SetNum((ClampedQuadsX + 1) * (ClampedQuadsY + 1));

	TArray<FVector> VertexPositions;
	VertexPositions.SetNum(Vertices.Num());

	auto GetVertexIndex = [ClampedQuadsX](int32 X, int32 Y)
	{
		return Y * (ClampedQuadsX + 1) + X;
	};

	for (int32 Y = 0; Y <= ClampedQuadsY; ++Y)
	{
		for (int32 X = 0; X <= ClampedQuadsX; ++X)
		{
			const double U = static_cast<double>(X) / static_cast<double>(ClampedQuadsX);
			const double V = static_cast<double>(Y) / static_cast<double>(ClampedQuadsY);
			const FVector Position(
				(U - 0.5) * ClampedSizeX,
				(V - 0.5) * ClampedSizeY,
				EvaluateHeight(U, V));

			const int32 VertexIndex = GetVertexIndex(X, Y);
			VertexPositions[VertexIndex] = Position;
			Vertices[VertexIndex] = MeshData.AppendVertex(FVector3d(Position));

			const FVector2f UV(
				static_cast<float>(U * ClampedUVTilingX),
				static_cast<float>(V * ClampedUVTilingY));
			MeshData.SetChannelUV(Vertices[VertexIndex], UV);
			MeshData.SetVertexUV(Vertices[VertexIndex], UV, 0);
		}
	}

	TArray<FVector> VertexNormals;
	VertexNormals.SetNum(Vertices.Num());

	for (int32 Y = 0; Y <= ClampedQuadsY; ++Y)
	{
		for (int32 X = 0; X <= ClampedQuadsX; ++X)
		{
			const int32 LeftX = FMath::Max(0, X - 1);
			const int32 RightX = FMath::Min(ClampedQuadsX, X + 1);
			const int32 DownY = FMath::Max(0, Y - 1);
			const int32 UpY = FMath::Min(ClampedQuadsY, Y + 1);

			const FVector TangentX = VertexPositions[GetVertexIndex(RightX, Y)] - VertexPositions[GetVertexIndex(LeftX, Y)];
			const FVector TangentY = VertexPositions[GetVertexIndex(X, UpY)] - VertexPositions[GetVertexIndex(X, DownY)];

			FVector Normal = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			if (FVector::DotProduct(Normal, FVector::UpVector) < 0.0)
			{
				Normal *= -1.0;
			}

			const int32 VertexIndex = GetVertexIndex(X, Y);
			VertexNormals[VertexIndex] = Normal;
			MeshData.SetVertexNormal(Vertices[VertexIndex], FVector3f(Normal));
		}
	}

	auto AddTriangle = [&MeshData, &Vertices](int32 A, int32 B, int32 C)
	{
		MeshData.AppendTriangle(UE::Geometry::FIndex3i(Vertices[A], Vertices[B], Vertices[C]));
	};

	for (int32 Y = 0; Y < ClampedQuadsY; ++Y)
	{
		for (int32 X = 0; X < ClampedQuadsX; ++X)
		{
			const int32 Row0 = Y * (ClampedQuadsX + 1);
			const int32 Row1 = (Y + 1) * (ClampedQuadsX + 1);

			const int32 V00 = Row0 + X;
			const int32 V10 = Row0 + X + 1;
			const int32 V01 = Row1 + X;
			const int32 V11 = Row1 + X + 1;

			AddTriangle(V00, V01, V11);
			AddTriangle(V00, V11, V10);
		}
	}

	MeshData.SummarizeUVRegion();
	return MeshData;
}

UStaticMesh* AMeshTerrainRuntimeLabActor::CreateFlatRuntimeStaticMesh()
{
	const UE::MeshPartition::FMeshData MeshData = CreateFlatRuntimeMeshData();

	FMeshDescription MeshDescription;
	FStaticMeshAttributes(MeshDescription).Register();
	MeshData.ConvertToMeshDescription(MeshDescription);

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(this, MakeUniqueObjectName(this, UStaticMesh::StaticClass(), TEXT("RuntimeMeshTerrain")));
	if (!StaticMesh)
	{
		return nullptr;
	}

	StaticMesh->bSupportRayTracing = true;
	StaticMesh->bGenerateMeshDistanceField = false;

	FStaticMaterial StaticMaterial;
	StaticMaterial.MaterialInterface = ResolveTerrainMaterial();
	StaticMaterial.MaterialSlotName = MeshPartitionStaticMeshMaterialSlotName;
	StaticMaterial.ImportedMaterialSlotName = MeshPartitionStaticMeshMaterialSlotName;
	StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.0f);
	StaticMesh->SetStaticMaterials({ StaticMaterial });

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = true;
	BuildParams.bCommitMeshDescription = true;
	BuildParams.bMarkPackageDirty = false;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bAllowCpuAccess = true;

	if (!StaticMesh->BuildFromMeshDescriptions({ &MeshDescription }, BuildParams))
	{
		return nullptr;
	}

	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		if (bBuildSimpleCollision)
		{
			BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			BodySetup->InvalidatePhysicsData();
			BodySetup->CreatePhysicsMeshes();
		}
		else
		{
			BodySetup->CollisionTraceFlag = CTF_UseDefault;
			BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			BodySetup->bNeverNeedsCookedCollisionData = true;
			BodySetup->bHasCookedCollisionData = false;
			BodySetup->InvalidatePhysicsData();
		}
	}

	return StaticMesh;
}
