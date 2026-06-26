// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabActor.h"

#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "MeshPartition.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionStaticMeshDescriptor.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "Components/SceneComponent.h"

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
		BuildFlatMeshTerrain();
	}
}

AActor* AMeshTerrainRuntimeLabActor::BuildFlatMeshTerrain()
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

	UE::MeshPartition::ACompiledSection* CompiledSection = World->SpawnActor<UE::MeshPartition::ACompiledSection>(
		UE::MeshPartition::ACompiledSection::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!CompiledSection)
	{
		MeshPartition->Destroy();
		RuntimeStaticMesh = nullptr;
		return nullptr;
	}

	CompiledSection->SetParent(MeshPartition);

	UE::MeshPartition::FStaticMeshDescriptor Descriptor;
	Descriptor.CollisionProfileName = bBuildSimpleCollision ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName;
	Descriptor.bCanEverAffectNavigation = bBuildSimpleCollision;
	Descriptor.UVRegion = FBox2f(FVector2f::ZeroVector, FVector2f(1.0f, 1.0f));

	CompiledSection->AddStaticMesh(RuntimeStaticMesh, Descriptor);

	SpawnedMeshPartition = MeshPartition;
	SpawnedCompiledSection = CompiledSection;

	return CompiledSection;
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
}

UStaticMesh* AMeshTerrainRuntimeLabActor::CreateFlatRuntimeStaticMesh()
{
	const int32 ClampedQuadsX = FMath::Max(1, QuadsX);
	const int32 ClampedQuadsY = FMath::Max(1, QuadsY);
	const double ClampedSizeX = FMath::Max(1.0, SizeX);
	const double ClampedSizeY = FMath::Max(1.0, SizeY);

	FMeshDescription MeshDescription;
	FStaticMeshAttributes(MeshDescription).Register();

	FMeshDescriptionBuilder Builder;
	Builder.SetMeshDescription(&MeshDescription);
	Builder.SetNumUVLayers(1);
	Builder.ReserveNewVertices((ClampedQuadsX + 1) * (ClampedQuadsY + 1));

	const FPolygonGroupID PolygonGroup = Builder.AppendPolygonGroup(TEXT("RuntimeTerrain"));

	TArray<FVertexID> Vertices;
	Vertices.SetNum((ClampedQuadsX + 1) * (ClampedQuadsY + 1));

	for (int32 Y = 0; Y <= ClampedQuadsY; ++Y)
	{
		for (int32 X = 0; X <= ClampedQuadsX; ++X)
		{
			const double U = static_cast<double>(X) / static_cast<double>(ClampedQuadsX);
			const double V = static_cast<double>(Y) / static_cast<double>(ClampedQuadsY);
			const FVector Position(
				(U - 0.5) * ClampedSizeX,
				(V - 0.5) * ClampedSizeY,
				0.0);

			Vertices[Y * (ClampedQuadsX + 1) + X] = Builder.AppendVertex(Position);
		}
	}

	auto AddTriangle = [&Builder, &Vertices, ClampedQuadsX, ClampedQuadsY](int32 A, int32 B, int32 C, const FPolygonGroupID& Group)
	{
		const FVertexInstanceID InstanceA = Builder.AppendInstance(Vertices[A]);
		const FVertexInstanceID InstanceB = Builder.AppendInstance(Vertices[B]);
		const FVertexInstanceID InstanceC = Builder.AppendInstance(Vertices[C]);

		Builder.SetInstanceTangentSpace(InstanceA, FVector::UpVector, FVector::ForwardVector, 1.0f);
		Builder.SetInstanceTangentSpace(InstanceB, FVector::UpVector, FVector::ForwardVector, 1.0f);
		Builder.SetInstanceTangentSpace(InstanceC, FVector::UpVector, FVector::ForwardVector, 1.0f);

		const auto SetInstanceUV = [&Builder, ClampedQuadsX, ClampedQuadsY](const FVertexInstanceID& Instance, int32 VertexIndex)
		{
			const int32 VertexX = VertexIndex % (ClampedQuadsX + 1);
			const int32 VertexY = VertexIndex / (ClampedQuadsX + 1);
			const FVector2D UV(
				static_cast<double>(VertexX) / static_cast<double>(ClampedQuadsX),
				static_cast<double>(VertexY) / static_cast<double>(ClampedQuadsY));
			Builder.SetInstanceUV(Instance, UV, 0);
		};

		SetInstanceUV(InstanceA, A);
		SetInstanceUV(InstanceB, B);
		SetInstanceUV(InstanceC, C);

		Builder.AppendTriangle(InstanceA, InstanceB, InstanceC, Group);
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

			AddTriangle(V00, V11, V01, PolygonGroup);
			AddTriangle(V00, V10, V11, PolygonGroup);
		}
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(this, MakeUniqueObjectName(this, UStaticMesh::StaticClass(), TEXT("RuntimeMeshTerrain")));
	if (!StaticMesh)
	{
		return nullptr;
	}

	UMaterialInterface* MeshMaterial = Material ? Material.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
	StaticMesh->GetStaticMaterials().Add(FStaticMaterial(MeshMaterial, TEXT("RuntimeTerrain")));

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bFastBuild = true;
	BuildParams.bCommitMeshDescription = true;
	BuildParams.bMarkPackageDirty = false;
	BuildParams.bBuildSimpleCollision = bBuildSimpleCollision;
	BuildParams.bAllowCpuAccess = true;

	if (!StaticMesh->BuildFromMeshDescriptions({ &MeshDescription }, BuildParams))
	{
		return nullptr;
	}

	if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		BodySetup->CollisionTraceFlag = bBuildSimpleCollision ? CTF_UseSimpleAsComplex : CTF_UseDefault;
	}

	return StaticMesh;
}
