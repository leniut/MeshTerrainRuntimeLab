// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabActor.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "MeshPartition.h"
#include "MeshPartitionChannel.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionMeshData.h"
#include "MeshPartitionSettings.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "MeshPartitionStaticMeshDescriptor.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "Components/SceneComponent.h"

#if MTR_WITH_RUNTIME_NANITE_BUILDER
#include "Components.h"
#include "Modules/ModuleManager.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"
#include "StaticMeshResources.h"
#include "Templates/PimplPtr.h"
#endif

#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#endif

namespace
{
const FName MeshPartitionStaticMeshMaterialSlotName(TEXT("MegaMeshStaticMeshMaterial"));
const FName MeshPartitionHighEndRenderVariantName(TEXT("HighEndPlatform_RenderData"));
const TCHAR* MeshPartitionDefaultChannelTexturePath = TEXT("/MeshPartition/Textures/Void2DArray.Void2DArray");
constexpr uint8 MeshPartitionInactiveChannelTableValue = 0xFF;
constexpr float MeshPartitionStaticMeshNaniteFallbackPercentTriangles = 0.2f;
constexpr double MeshPartitionStaticMeshSkirtWidth = 10.0;
constexpr double MeshPartitionStaticMeshSkirtPushDown = 10.0;

bool IsRenderBuildVariant(const FName& BuildVariantName)
{
	return BuildVariantName == MeshPartitionHighEndRenderVariantName;
}

#if WITH_EDITOR
UMaterialInstanceConstant* CreateRuntimeTerrainMaterialInstance(UObject* Outer, const TCHAR* NameBase, UMaterialInterface* Parent)
{
	if (!Outer)
	{
		return nullptr;
	}

	UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(
		Outer,
		MakeUniqueObjectName(Outer, UMaterialInstanceConstant::StaticClass(), NameBase));

	if (!MaterialInstance)
	{
		return nullptr;
	}

	MaterialInstance->SetParentEditorOnly(Parent ? Parent : UMaterial::GetDefaultMaterial(MD_Surface), false);

	constexpr uint32 RequiredUsageFlags = (1u << MATUSAGE_StaticMesh) | (1u << MATUSAGE_Nanite);
	MaterialInstance->BasePropertyOverrides.bOverride_UsageFlags |= RequiredUsageFlags;
	MaterialInstance->BasePropertyOverrides.UsageFlags |= RequiredUsageFlags;
	MaterialInstance->UpdateStaticPermutation();

	return MaterialInstance;
}
#endif
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
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: RebuildTerrain aborted - no world."));
		return nullptr;
	}

	FBox2f MeshUVRegion(FVector2f::ZeroVector, FVector2f(1.0f, 1.0f));
	RuntimeStaticMesh = CreateFlatRuntimeStaticMesh(MeshUVRegion);
	if (!RuntimeStaticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: RebuildTerrain aborted - runtime static mesh build failed."));
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
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: RebuildTerrain aborted - failed to spawn AMeshPartition."));
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
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: RebuildTerrain aborted - failed to create ACompiledSection."));
		MeshPartition->Destroy();
		RuntimeStaticMesh = nullptr;
		return nullptr;
	}

	UE::MeshPartition::FStaticMeshDescriptor Descriptor;
	const bool bRenderBuildVariant = IsRenderBuildVariant(BuildInfo.BuildVariantName);

	// The render build variant (HighEndPlatform_RenderData, the default) mirrors the editor render
	// section, which never carries collision. So when this variant is selected, bBuildSimpleCollision
	// is intentionally ignored and the terrain has no collision. Warn so the combination is not a
	// silent surprise: a gameplay build variant must be selected to get runtime collision.
	if (bRenderBuildVariant && bBuildSimpleCollision)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("MeshTerrainRuntimeLab: bBuildSimpleCollision is ignored for render build variant '%s' (parity: render sections have no collision). Select a gameplay variant for collision."),
			*BuildInfo.BuildVariantName.ToString());
	}

	Descriptor.CollisionProfileName = bRenderBuildVariant
		? UCollisionProfile::NoCollision_ProfileName
		: (bBuildSimpleCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	Descriptor.bCanEverAffectNavigation = !bRenderBuildVariant && bBuildSimpleCollision;
	// Mirror FStaticMeshTransformer::FinalizeStaticMeshes: the descriptor UV region must be the
	// actual mesh UV bounds (tiled), not a hardcoded unit box, so the channel/material UV layout
	// matches an editor-compiled section.
	Descriptor.UVRegion = MeshUVRegion;

	ApplyMinimalRuntimeChannelData(CompiledSection, ResolvedDefinition);

#if WITH_EDITOR
	if (RuntimeStaticMesh && CompiledSection->GetMaterialInstance())
	{
		RuntimeStaticMesh->SetMaterial(0, CompiledSection->GetMaterialInstance());
	}
#endif

	CompiledSection->AddStaticMesh(RuntimeStaticMesh, Descriptor);

	// Editor builds attach a savable UMaterialInstanceConstant above. In non-editor builds that path
	// is unavailable, so give the generated mesh components a real runtime per-instance material here.
	ApplyRuntimeMaterialInstance(CompiledSection, ResolveTerrainMaterial());

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
	RuntimeChannelTexturePath.Reset();
	bRuntimeNaniteDataValid = false;
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
	using namespace UE::MeshPartition;

	// 1. Explicit override, if the user assigned a specific definition asset.
	if (!MeshPartitionDefinition.IsNull())
	{
		if (UMeshPartitionDefinition* Loaded = MeshPartitionDefinition.LoadSynchronous())
		{
			return Loaded;
		}
	}

	// 2. Engine-configured default definition, mirroring AMeshPartition::InitializeDefinition():
	//    USettings::GetDefaultDefinition() resolves DefaultMeshPartition.ini to
	//    /MeshPartition/DataAssets/MPD_Default. This is what Mesh Terrain Mode uses by default,
	//    and it is the real default definition (not the bare class default object).
	if (const USettings* Settings = GetDefault<USettings>())
	{
		TSoftObjectPtr<UMeshPartitionDefinition> ConfiguredDefault = Settings->GetDefaultDefinition();
		if (!ConfiguredDefault.IsNull())
		{
			if (UMeshPartitionDefinition* Loaded = ConfiguredDefault.LoadSynchronous())
			{
				return Loaded;
			}
		}
	}

	// 3. Last-resort bare class default (CDO) so runtime terrain is never definition-less.
	return const_cast<UMeshPartitionDefinition*>(UMeshPartitionDefinition::GetDefaultMegaMeshDefinition());
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
	UMaterialInstanceConstant* MaterialInstance = CreateRuntimeTerrainMaterialInstance(
		Section->GetPackage(),
		TEXT("CompiledSectionMIC"),
		ResolveTerrainMaterial());

	if (MaterialInstance)
	{
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
	ChannelTable.Init(MeshPartitionInactiveChannelTableValue, NumChannels);

	const float UnitPerU = static_cast<float>(FMath::Max(1.0, SizeX) / FMath::Max(0.001, UVTilingX));
	const float UnitPerV = static_cast<float>(FMath::Max(1.0, SizeY) / FMath::Max(0.001, UVTilingY));
	const FVector2f ChannelTexcoordDesc(UnitPerU, UnitPerV);

	if (UTexture* DefaultChannelTexture = LoadObject<UTexture>(nullptr, MeshPartitionDefaultChannelTexturePath))
	{
		CompiledSection->SetChannelTexture(DefaultChannelTexture);
		RuntimeChannelTexturePath = DefaultChannelTexture->GetPathName();
	}
	else
	{
		RuntimeChannelTexturePath.Reset();
	}

	CompiledSection->SetChannelData(ChannelTable, ChannelTexcoordDesc);

	RuntimeChannelTableLength = ChannelTable.Num();
	RuntimeChannelTexcoordDesc = FVector2D(
		static_cast<double>(ChannelTexcoordDesc.X),
		static_cast<double>(ChannelTexcoordDesc.Y));
}

void AMeshTerrainRuntimeLabActor::ApplyRuntimeMaterialInstance(
	UE::MeshPartition::ACompiledSection* CompiledSection,
	UMaterialInterface* ResolvedMaterial)
{
#if !WITH_EDITOR
	// Editor builds attach a savable UMaterialInstanceConstant via CreateRuntimeCompiledSection and
	// SetMaterial. Neither route exists in packaged/Shipping builds: ACompiledSection::SetMaterialInstance
	// only accepts a UMaterialInstanceConstant (not creatable at runtime) and SetChannelTexture binds
	// the channel texture through an editor-only call. So in non-editor builds we give the section a
	// real per-instance material at runtime - a UMaterialInstanceDynamic applied directly to the
	// generated mesh components - and bind the channel texture via the runtime-safe parameter setter.
	if (!CompiledSection || !ResolvedMaterial)
	{
		return;
	}

	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(ResolvedMaterial, this);
	if (!DynamicMaterial)
	{
		return;
	}

	if (UTexture* ChannelTexture = CompiledSection->GetChannelTexture())
	{
		DynamicMaterial->SetTextureParameterValue(UE::MeshPartition::ChannelTextureParameterName, ChannelTexture);
	}

	for (UE::MeshPartition::UMeshPartitionStaticMeshComponent* MeshComponent : CompiledSection->GetMeshComponents())
	{
		if (MeshComponent)
		{
			MeshComponent->SetMaterial(0, DynamicMaterial);
		}
	}
#else
	(void)CompiledSection;
	(void)ResolvedMaterial;
#endif
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
	const int32 NumBaseVertices = (ClampedQuadsX + 1) * (ClampedQuadsY + 1);
	const int32 NumBoundaryVertices = bApplyEditorSkirtForParity ? 2 * (ClampedQuadsX + 1) + 2 * (ClampedQuadsY - 1) : 0;
	const int32 NumBoundaryEdges = bApplyEditorSkirtForParity ? 2 * (ClampedQuadsX + ClampedQuadsY) : 0;
	MeshData.ReserveAdditionalVertices(NumBaseVertices + NumBoundaryVertices);
	MeshData.ReserveAdditionalTriangles((ClampedQuadsX * ClampedQuadsY * 2) + (NumBoundaryEdges * 2));

	TArray<int32> Vertices;
	Vertices.SetNum(NumBaseVertices);

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

	auto AddTriangleVertexIds = [&MeshData](int32 A, int32 B, int32 C)
	{
		MeshData.AppendTriangle(UE::Geometry::FIndex3i(A, B, C));
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

	if (bApplyEditorSkirtForParity)
	{
		TArray<int32> SkirtVertices;
		SkirtVertices.Init(INDEX_NONE, Vertices.Num());

		auto GetOrCreateSkirtVertex = [&](int32 X, int32 Y) -> int32
		{
			const int32 VertexIndex = GetVertexIndex(X, Y);
			int32& SkirtVertex = SkirtVertices[VertexIndex];
			if (SkirtVertex != INDEX_NONE)
			{
				return SkirtVertex;
			}

			FVector OutwardDirection = FVector::ZeroVector;
			if (X == 0)
			{
				OutwardDirection.X -= 1.0;
			}
			if (X == ClampedQuadsX)
			{
				OutwardDirection.X += 1.0;
			}
			if (Y == 0)
			{
				OutwardDirection.Y -= 1.0;
			}
			if (Y == ClampedQuadsY)
			{
				OutwardDirection.Y += 1.0;
			}
			OutwardDirection = OutwardDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ZeroVector);

			const FVector SourcePosition = VertexPositions[VertexIndex];
			const FVector SourceNormal = VertexNormals[VertexIndex].GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			const FVector SkirtPosition =
				SourcePosition
				+ OutwardDirection * MeshPartitionStaticMeshSkirtWidth
				- SourceNormal * MeshPartitionStaticMeshSkirtPushDown;

			SkirtVertex = MeshData.AppendVertex(FVector3d(SkirtPosition));
			MeshData.SetChannelUV(SkirtVertex, MeshData.GetChannelUV(Vertices[VertexIndex]));
			MeshData.SetVertexUV(SkirtVertex, MeshData.GetVertexUV(Vertices[VertexIndex], 0), 0);
			MeshData.SetVertexNormal(SkirtVertex, FVector3f(SourceNormal));
			return SkirtVertex;
		};

		for (int32 X = 0; X < ClampedQuadsX; ++X)
		{
			const int32 V0 = Vertices[GetVertexIndex(X, 0)];
			const int32 V1 = Vertices[GetVertexIndex(X + 1, 0)];
			const int32 S0 = GetOrCreateSkirtVertex(X, 0);
			const int32 S1 = GetOrCreateSkirtVertex(X + 1, 0);
			AddTriangleVertexIds(V0, S0, S1);
			AddTriangleVertexIds(V0, S1, V1);
		}

		for (int32 X = 0; X < ClampedQuadsX; ++X)
		{
			const int32 V0 = Vertices[GetVertexIndex(X, ClampedQuadsY)];
			const int32 V1 = Vertices[GetVertexIndex(X + 1, ClampedQuadsY)];
			const int32 S0 = GetOrCreateSkirtVertex(X, ClampedQuadsY);
			const int32 S1 = GetOrCreateSkirtVertex(X + 1, ClampedQuadsY);
			AddTriangleVertexIds(V0, S1, S0);
			AddTriangleVertexIds(V0, V1, S1);
		}

		for (int32 Y = 0; Y < ClampedQuadsY; ++Y)
		{
			const int32 V0 = Vertices[GetVertexIndex(0, Y)];
			const int32 V1 = Vertices[GetVertexIndex(0, Y + 1)];
			const int32 S0 = GetOrCreateSkirtVertex(0, Y);
			const int32 S1 = GetOrCreateSkirtVertex(0, Y + 1);
			AddTriangleVertexIds(V0, S1, S0);
			AddTriangleVertexIds(V0, V1, S1);
		}

		for (int32 Y = 0; Y < ClampedQuadsY; ++Y)
		{
			const int32 V0 = Vertices[GetVertexIndex(ClampedQuadsX, Y)];
			const int32 V1 = Vertices[GetVertexIndex(ClampedQuadsX, Y + 1)];
			const int32 S0 = GetOrCreateSkirtVertex(ClampedQuadsX, Y);
			const int32 S1 = GetOrCreateSkirtVertex(ClampedQuadsX, Y + 1);
			AddTriangleVertexIds(V0, S0, S1);
			AddTriangleVertexIds(V0, S1, V1);
		}
	}

	MeshData.SummarizeUVRegion();
	return MeshData;
}

UStaticMesh* AMeshTerrainRuntimeLabActor::CreateFlatRuntimeStaticMesh(FBox2f& OutUVRegion)
{
	const UE::MeshPartition::FMeshData MeshData = CreateFlatRuntimeMeshData();

	// Authoritative UV bounds (after FMeshData::SummarizeUVRegion), used for the section descriptor
	// exactly as the editor StaticMeshTransformer does.
	OutUVRegion = MeshData.GetUVRegion();

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

	UMaterialInterface* StaticMeshMaterial = ResolveTerrainMaterial();
#if WITH_EDITOR
	if (UMaterialInstanceConstant* StaticMeshMaterialInstance = CreateRuntimeTerrainMaterialInstance(
		StaticMesh,
		TEXT("RuntimeMeshTerrainMaterial"),
		StaticMeshMaterial))
	{
		StaticMeshMaterial = StaticMeshMaterialInstance;
	}
#endif

	FStaticMaterial StaticMaterial;
	StaticMaterial.MaterialInterface = StaticMeshMaterial;
	StaticMaterial.MaterialSlotName = MeshPartitionStaticMeshMaterialSlotName;
	StaticMaterial.ImportedMaterialSlotName = MeshPartitionStaticMeshMaterialSlotName;
	StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.0f);
	StaticMesh->SetStaticMaterials({ StaticMaterial });

	// SPIKE - runtime Nanite. Two routes:
	//  * bUseManualNaniteBuilder=false (editor only): UStaticMesh full build (bFastBuild=false)
	//    compiles Nanite, but only under WITH_EDITOR. Packaged builds hard-assert bFastBuild==true.
	//  * bUseManualNaniteBuilder=true: keep the runtime-safe fast build, then inject Nanite via the
	//    Developer NaniteBuilder module manually (BuildAndInjectRuntimeNanite). In this plugin the
	//    manual builder is compiled for Editor/PIE targets only; packaged Nanite is a separate
	//    source-engine spike.
	bRuntimeNaniteDataValid = false;
	bool bWantNanite = false;
#if WITH_EDITOR || MTR_WITH_RUNTIME_NANITE_BUILDER
	bWantNanite = bBuildNaniteData;
#endif
	bool bUseFastBuild = true;

	if (bWantNanite)
	{
#if WITH_EDITOR || MTR_WITH_RUNTIME_NANITE_BUILDER
		FMeshNaniteSettings NaniteSettings = StaticMesh->GetNaniteSettings();
		NaniteSettings.bEnabled = true;
		NaniteSettings.FallbackPercentTriangles = MeshPartitionStaticMeshNaniteFallbackPercentTriangles;
		StaticMesh->SetNaniteSettings(NaniteSettings);
#endif

#if WITH_EDITOR
		// Editor-only convenience route: let the full build compile Nanite for us.
		if (!bUseManualNaniteBuilder)
		{
			bUseFastBuild = false;
		}
#endif
	}

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = bUseFastBuild;
	BuildParams.bCommitMeshDescription = true;
	BuildParams.bMarkPackageDirty = false;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bAllowCpuAccess = true;

	if (!StaticMesh->BuildFromMeshDescriptions({ &MeshDescription }, BuildParams))
	{
		return nullptr;
	}

	if (bWantNanite && bUseManualNaniteBuilder)
	{
		bRuntimeNaniteDataValid = BuildAndInjectRuntimeNanite(StaticMesh, MeshData);
	}
	else
	{
		bRuntimeNaniteDataValid = StaticMesh->HasValidNaniteData();
	}

	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		if (bBuildSimpleCollision && !IsRenderBuildVariant(BuildVariantName))
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

bool AMeshTerrainRuntimeLabActor::BuildAndInjectRuntimeNanite(UStaticMesh* StaticMesh, const UE::MeshPartition::FMeshData& MeshData) const
{
#if MTR_WITH_RUNTIME_NANITE_BUILDER
	if (!StaticMesh || !StaticMesh->GetRenderData())
	{
		return false;
	}

	// The Developer NaniteBuilder module is stripped from Shipping/Test. Load it explicitly so the
	// failure is graceful (and logged) where it is unavailable.
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("NaniteBuilder"))
		&& !FModuleManager::Get().LoadModule(TEXT("NaniteBuilder")))
	{
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: NaniteBuilder module unavailable; cannot build runtime Nanite."));
		return false;
	}
	Nanite::IBuilderModule& Builder = Nanite::IBuilderModule::Get();

	// Source the Nanite input from the exact same FMeshData used for the UStaticMesh fast build, so
	// the Nanite representation matches the fallback geometry exactly - including the optional parity
	// skirt and any non-flat height. This relies on the FMeshData being densely packed (vertex and
	// triangle IDs are 0..Count-1), which is guaranteed by CreateFlatRuntimeMeshData (sequential
	// appends, no deletions).
	const int32 NumVerts = MeshData.VertexCount();
	const int32 NumTris = MeshData.TriangleCount();
	if (NumVerts <= 0 || NumTris <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: runtime Nanite input mesh is empty (verts=%d, tris=%d)."), NumVerts, NumTris);
		return false;
	}

	Nanite::IBuilderModule::FInputMeshData Input;
	Input.NumTexCoords = 1;
	Input.NumBoneInfluences = 0;
	Input.PercentTriangles = 1.0f;
	Input.MaxDeviation = 0.0f;
	Input.Vertices.Empty(NumVerts, /*NumTexCoords*/ 1, /*NumBoneInfluences*/ 0);

	FVector3f BoundsMin(TNumericLimits<float>::Max());
	FVector3f BoundsMax(TNumericLimits<float>::Lowest());

	for (int32 VertexID = 0; VertexID < NumVerts; ++VertexID)
	{
		const FVector3f Position(MeshData.GetVertex(VertexID));
		const FVector3f Normal = MeshData.GetVertexNormal(VertexID).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		// Any consistent tangent perpendicular to the normal is fine for this flat/heightfield terrain.
		const FVector3f Tangent = FVector3f::CrossProduct(FVector3f::UpVector, Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector);
		const FVector3f Bitangent = FVector3f::CrossProduct(Normal, Tangent);
		const FVector2f UV = MeshData.GetVertexUV(VertexID, 0);

		Input.Vertices.Position.Add(Position);
		Input.Vertices.TangentX.Add(Tangent);
		Input.Vertices.TangentY.Add(Bitangent);
		Input.Vertices.TangentZ.Add(Normal);
		Input.Vertices.Color.Add(FColor::White);
		Input.Vertices.UVs[0].Add(UV);

		BoundsMin = BoundsMin.ComponentMin(Position);
		BoundsMax = BoundsMax.ComponentMax(Position);
	}

	Input.VertexBounds.Min = BoundsMin;
	Input.VertexBounds.Max = BoundsMax;

	Input.TriangleIndices.Reserve(NumTris * 3);
	for (int32 TriangleID = 0; TriangleID < NumTris; ++TriangleID)
	{
		const UE::Geometry::FIndex3i Triangle = MeshData.GetTriangle(TriangleID);
		Input.TriangleIndices.Add(static_cast<uint32>(Triangle.A));
		Input.TriangleIndices.Add(static_cast<uint32>(Triangle.B));
		Input.TriangleIndices.Add(static_cast<uint32>(Triangle.C));
	}

	const uint32 NumTriangles = Input.TriangleIndices.Num() / 3;
	Input.TriangleCounts.Add(NumTriangles);

	Nanite::FMeshDataSection Section;
	Section.MaterialIndex = 0;
	Section.FirstIndex = 0;
	Section.NumTriangles = NumTriangles;
	Section.MinVertexIndex = 0;
	Section.MaxVertexIndex = static_cast<uint32>(NumVerts - 1);
	Input.Sections.Add(Section);

	if (!Builder.BuildMaterialIndices(Input.Sections, NumTriangles, Input.MaterialIndices))
	{
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: Nanite BuildMaterialIndices failed."));
		return false;
	}

	// Allocate the resources, run the (slow) Nanite build, then attach + upload them.
	StaticMesh->GetRenderData()->NaniteResourcesPtr = MakePimpl<Nanite::FResources>();
	Nanite::FResources& Resources = *StaticMesh->GetRenderData()->NaniteResourcesPtr;

	FMeshNaniteSettings Settings = StaticMesh->GetNaniteSettings();
	Settings.bEnabled = true;

	if (!Builder.Build(Resources, Input, /*OutFallback*/ nullptr, /*OutRayTracingFallback*/ nullptr,
		/*RayTracingFallbackSettings*/ nullptr, Settings, /*AssemblyData*/ nullptr))
	{
		StaticMesh->GetRenderData()->NaniteResourcesPtr.Reset();
		UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: Nanite IBuilderModule::Build failed."));
		return false;
	}

	StaticMesh->GetRenderData()->NaniteResourcesPtr->InitResources(StaticMesh);

	const bool bValid = StaticMesh->HasValidNaniteData();
	UE_LOG(LogTemp, Display, TEXT("MeshTerrainRuntimeLab: manual runtime Nanite build %s (verts=%d, tris=%u)."),
		bValid ? TEXT("succeeded") : TEXT("produced no valid data"), NumVerts, NumTriangles);
	return bValid;
#else
	// NaniteBuilder (Developer module) is not linked in this target. Packaged runtime Nanite
	// requires a separate source-engine experiment; Editor/PIE is unaffected.
	(void)StaticMesh;
	(void)MeshData;
	UE_LOG(LogTemp, Warning, TEXT("MeshTerrainRuntimeLab: runtime Nanite builder not compiled in this target."));
	return false;
#endif
}
