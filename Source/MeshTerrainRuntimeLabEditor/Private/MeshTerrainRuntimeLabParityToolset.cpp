// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainRuntimeLabParityToolset.h"

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshPartition.h"
#include "MeshPartitionChannel.h"
#include "MeshPartitionChannelCollection.h"
#include "MeshPartitionCollisionComponent.h"
#include "MeshPartitionCompiledSection.h"
#include "MeshPartitionComponent.h"
#include "MeshPartitionDefinition.h"
#include "MeshPartitionEditorComponent.h"
#include "MeshPartitionStaticMeshComponent.h"
#include "MeshPartitionTransformer.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace MeshTerrainRuntimeLab::Parity
{
using namespace UE::MeshPartition;

static FString ObjectPath(const UObject* Object)
{
	return Object ? Object->GetPathName() : TEXT("None");
}

static FString ClassPath(const UObject* Object)
{
	return (Object && Object->GetClass()) ? Object->GetClass()->GetPathName() : TEXT("None");
}

static FString GuidString(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphens);
}

static TSharedRef<FJsonObject> VectorObject(const FVector& Vector)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), Vector.X);
	Object->SetNumberField(TEXT("y"), Vector.Y);
	Object->SetNumberField(TEXT("z"), Vector.Z);
	return Object;
}

static TSharedRef<FJsonObject> Vector2fObject(const FVector2f& Vector)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), Vector.X);
	Object->SetNumberField(TEXT("y"), Vector.Y);
	return Object;
}

static TSharedRef<FJsonObject> IntVectorObject(const FIntVector& Vector)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), Vector.X);
	Object->SetNumberField(TEXT("y"), Vector.Y);
	Object->SetNumberField(TEXT("z"), Vector.Z);
	return Object;
}

static TSharedRef<FJsonObject> IntPointObject(const FIntPoint& Point)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("x"), Point.X);
	Object->SetNumberField(TEXT("y"), Point.Y);
	return Object;
}

static TSharedRef<FJsonObject> BoxObject(const FBox& Box)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("isValid"), Box.IsValid != 0);
	Object->SetObjectField(TEXT("min"), VectorObject(Box.Min));
	Object->SetObjectField(TEXT("max"), VectorObject(Box.Max));
	return Object;
}

static TArray<TSharedPtr<FJsonValue>> NameArray(TConstArrayView<FName> Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

static TArray<TSharedPtr<FJsonValue>> StringArray(TConstArrayView<FString> Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
}

static TArray<TSharedPtr<FJsonValue>> ObjectPathArray(TConstArrayView<TObjectPtr<UObject>> Objects)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const UObject* Object : Objects)
	{
		Values.Add(MakeShared<FJsonValueString>(ObjectPath(Object)));
	}
	return Values;
}

template <typename ObjectType>
static TArray<TSharedPtr<FJsonValue>> TypedObjectPathArray(TConstArrayView<TObjectPtr<ObjectType>> Objects)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const ObjectType* Object : Objects)
	{
		Values.Add(MakeShared<FJsonValueString>(ObjectPath(Object)));
	}
	return Values;
}

static TArray<TSharedPtr<FJsonValue>> SoftObjectPathArray(TConstArrayView<FSoftObjectPath> Paths)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FSoftObjectPath& Path : Paths)
	{
		Values.Add(MakeShared<FJsonValueString>(Path.ToString()));
	}
	return Values;
}

static TArray<TSharedPtr<FJsonValue>> NamePathArray(TConstArrayView<FName> Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

static FString WriteJson(const TSharedRef<FJsonObject>& Object)
{
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Object, Writer);
	return Output;
}

static TSharedRef<FJsonObject> StatusObject(bool bSuccess, const FString& Message)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("success"), bSuccess);
	Object->SetStringField(TEXT("message"), Message);
	return Object;
}

template <typename ObjectType>
static ObjectType* FindOrLoadObject(const FString& ObjectPathString)
{
	if (ObjectPathString.IsEmpty())
	{
		return nullptr;
	}

	if (UObject* Existing = StaticFindObject(nullptr, nullptr, *ObjectPathString))
	{
		return Cast<ObjectType>(Existing);
	}

	return LoadObject<ObjectType>(nullptr, *ObjectPathString);
}

static TSharedRef<FJsonObject> GridSettingsObject(const FGridSettings& Settings)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("cellSize"), Settings.CellSize);
	Object->SetBoolField(TEXT("bIs2D"), Settings.bIs2D);
	Object->SetBoolField(TEXT("bIsGridSplit"), Settings.IsGridSplit());
	Object->SetObjectField(TEXT("worldOriginOffset"), VectorObject(Settings.WorldOriginOffset));
	return Object;
}

static TSharedRef<FJsonObject> BuildInfoObject(const FCompiledSectionBuildInfo& BuildInfo)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("buildKey"), GuidString(BuildInfo.BuildKey));
	Object->SetStringField(TEXT("buildVariantName"), BuildInfo.BuildVariantName.ToString());
	Object->SetStringField(TEXT("megaMeshDefinitionPath"), BuildInfo.MegaMeshDefinitionPath.ToString());
	Object->SetArrayField(TEXT("baseModifierPaths"), SoftObjectPathArray(BuildInfo.BaseModifierPaths));
	Object->SetStringField(TEXT("modifiersHash"), GuidString(BuildInfo.ModifiersHash));
	Object->SetArrayField(TEXT("packageDependencies"), NamePathArray(BuildInfo.PackageDependencies));
	Object->SetStringField(TEXT("packageHash"), GuidString(BuildInfo.PackageHash));
	Object->SetStringField(TEXT("modifierSetHash"), GuidString(BuildInfo.ModifierSetHash));
	Object->SetArrayField(TEXT("classDependencies"), NamePathArray(BuildInfo.ClassDependencies));
	Object->SetStringField(TEXT("classHash"), GuidString(BuildInfo.ClassHash));
	Object->SetStringField(TEXT("buildVariantHash"), GuidString(BuildInfo.BuildVariantHash));
	Object->SetStringField(TEXT("megaMeshGuid"), GuidString(BuildInfo.MegaMeshGUID));
	Object->SetStringField(TEXT("megaMeshPath"), BuildInfo.MegaMeshPath.ToString());
	Object->SetObjectField(TEXT("gridCellCoord"), IntVectorObject(BuildInfo.GridCellCoord));
	Object->SetBoolField(TEXT("hasValidGridCellCoord"), BuildInfo.HasValidGridCellCoord());
	Object->SetObjectField(TEXT("gridSettings"), GridSettingsObject(BuildInfo.GridSettings));
	Object->SetStringField(TEXT("resolvedMegaMeshDefinition"), ObjectPath(BuildInfo.GetMegaMeshDefinition()));
	return Object;
}

static bool ReadPrivateVector2fProperty(const UObject* Object, const FName PropertyName, FVector2f& OutValue)
{
	if (!Object)
	{
		return false;
	}

	const FStructProperty* StructProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName);
	if (!StructProperty)
	{
		return false;
	}

	OutValue = *StructProperty->ContainerPtrToValuePtr<FVector2f>(Object);
	return true;
}

static TSharedRef<FJsonObject> StaticMeshObject(const UStaticMesh* StaticMesh)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), ObjectPath(StaticMesh));
	Object->SetStringField(TEXT("class"), ClassPath(StaticMesh));

	if (!StaticMesh)
	{
		return Object;
	}

	const FMeshNaniteSettings& NaniteSettings = StaticMesh->GetNaniteSettings();
	Object->SetBoolField(TEXT("naniteEnabled"), NaniteSettings.bEnabled);
	Object->SetNumberField(TEXT("naniteFallbackPercentTriangles"), NaniteSettings.FallbackPercentTriangles);
	Object->SetNumberField(TEXT("naniteFallbackRelativeError"), NaniteSettings.FallbackRelativeError);
	Object->SetBoolField(TEXT("supportRayTracing"), StaticMesh->bSupportRayTracing);
	Object->SetBoolField(TEXT("generateMeshDistanceField"), StaticMesh->bGenerateMeshDistanceField);
	Object->SetNumberField(TEXT("numLODs"), StaticMesh->GetNumLODs());
	Object->SetStringField(TEXT("bodySetup"), ObjectPath(StaticMesh->GetBodySetup()));

	TArray<TSharedPtr<FJsonValue>> MaterialValues;
	for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
	{
		TSharedRef<FJsonObject> MaterialObject = MakeShared<FJsonObject>();
		MaterialObject->SetStringField(TEXT("slotName"), StaticMaterial.MaterialSlotName.ToString());
		MaterialObject->SetStringField(TEXT("importedSlotName"), StaticMaterial.ImportedMaterialSlotName.ToString());
		MaterialObject->SetStringField(TEXT("material"), ObjectPath(StaticMaterial.MaterialInterface));
		MaterialValues.Add(MakeShared<FJsonValueObject>(MaterialObject));
	}
	Object->SetArrayField(TEXT("staticMaterials"), MaterialValues);

	return Object;
}

static TSharedRef<FJsonObject> DefinitionObject(const UMeshPartitionDefinition* Definition)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), ObjectPath(Definition));
	Object->SetStringField(TEXT("class"), ClassPath(Definition));

	if (!Definition)
	{
		return Object;
	}

	Object->SetStringField(TEXT("material"), ObjectPath(Definition->GetMaterial()));
	Object->SetArrayField(TEXT("modifierTypePriorities"), NameArray(Definition->GetModifierTypePriorities()));
	Object->SetArrayField(TEXT("channelNames"), NameArray(Definition->GetChannelNames()));
	Object->SetNumberField(TEXT("numChannels"), Definition->GetChannelMap().GetNumChannels());
	Object->SetNumberField(TEXT("channelTexelSize"), Definition->GetChannelTexelSize());
	Object->SetNumberField(TEXT("materialCacheTexelSize"), Definition->GetMaterialCacheTexelSize());
	Object->SetStringField(TEXT("channelUVLayoutMethod"), StaticEnum<EChannelCollectionUVLayoutMethod>()->GetNameStringByValue(static_cast<int64>(Definition->GetChannelUVLayoutMethod())));
	Object->SetNumberField(TEXT("channelVEUVSamplesPerSquareMeter"), Definition->GetChannelVEUVSamplesPerSquareMeter());
	Object->SetObjectField(TEXT("channelVEUVVoxelCount"), IntVectorObject(Definition->GetChannelVEUVVoxelCount()));
	Object->SetStringField(TEXT("channelPlaneProjectionNormalSource"), StaticEnum<EPlaneProjectionNormalSource>()->GetNameStringByValue(static_cast<int64>(Definition->GetChannelPlaneProjectionNormalSource())));
	Object->SetObjectField(TEXT("channelPlaneProjectionFixedNormal"), VectorObject(Definition->GetChannelPlaneProjectionFixedNormal()));
	Object->SetStringField(TEXT("defaultPhysicalMaterial"), ObjectPath(Definition->GetDefaultPhysicalMaterial()));

	TArray<TSharedPtr<FJsonValue>> PhysicalMaterialValues;
	for (const FPhysicalMaterialChannel& Channel : Definition->GetPhysicalMaterialChannels())
	{
		TSharedRef<FJsonObject> ChannelObject = MakeShared<FJsonObject>();
		ChannelObject->SetStringField(TEXT("channelName"), Channel.ChannelName.GetName().ToString());
		ChannelObject->SetStringField(TEXT("material"), ObjectPath(Channel.Material));
		ChannelObject->SetNumberField(TEXT("minimumCollisionRelevanceWeight"), Channel.MinimumCollisionRelevanceWeight);
		PhysicalMaterialValues.Add(MakeShared<FJsonValueObject>(ChannelObject));
	}
	Object->SetArrayField(TEXT("physicalMaterialChannels"), PhysicalMaterialValues);

	TArray<TSharedPtr<FJsonValue>> BuildVariantValues;
	for (const FCompiledSectionBuildVariant& Variant : Definition->GetCompiledSectionBuildVariants())
	{
		TSharedRef<FJsonObject> VariantObject = MakeShared<FJsonObject>();
		VariantObject->SetStringField(TEXT("name"), Variant.Name.ToString());
		VariantObject->SetBoolField(TEXT("bSplitSectionsToMatchWorldPartitionRuntimeGrid"), Variant.bSplitSectionsToMatchWorldPartitionRuntimeGrid);
		VariantObject->SetNumberField(TEXT("maxSectionComplexity"), Variant.MaxSectionComplexity);
#if WITH_EDITORONLY_DATA
		VariantObject->SetStringField(TEXT("transformerPipeline"), ObjectPath(Variant.TransformerPipeline));
#else
		VariantObject->SetStringField(TEXT("transformerPipeline"), TEXT("EditorOnlyDataUnavailable"));
#endif
		BuildVariantValues.Add(MakeShared<FJsonValueObject>(VariantObject));
	}
	Object->SetArrayField(TEXT("compiledSectionBuildVariants"), BuildVariantValues);

#if WITH_EDITOR
	Object->SetArrayField(TEXT("allCompiledSectionBuildVariantNames"), NameArray(Definition->GetAllCompiledSectionBuildVariantNames()));
#endif

	return Object;
}

static TSharedRef<FJsonObject> MeshPartitionObject(const AMeshPartition* MeshPartition)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), ObjectPath(MeshPartition));
	Object->SetStringField(TEXT("class"), ClassPath(MeshPartition));

	if (!MeshPartition)
	{
		return Object;
	}

	Object->SetStringField(TEXT("label"), MeshPartition->GetActorLabel());
	Object->SetStringField(TEXT("definition"), ObjectPath(MeshPartition->GetMeshPartitionDefinition()));
	Object->SetStringField(TEXT("component"), ObjectPath(MeshPartition->GetMeshPartitionComponent()));
	Object->SetStringField(TEXT("componentClass"), ClassPath(MeshPartition->GetMeshPartitionComponent()));
	Object->SetStringField(TEXT("runtimeGrid"), MeshPartition->GetRuntimeGrid().ToString());
	Object->SetBoolField(TEXT("bIsSpatiallyLoaded"), MeshPartition->GetIsSpatiallyLoaded());
	Object->SetObjectField(TEXT("bounds"), BoxObject(MeshPartition->GetComponentsBoundingBox(true)));
	return Object;
}

static TSharedRef<FJsonObject> StaticMeshComponentObject(const UStaticMeshComponent* Component)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), ObjectPath(Component));
	Object->SetStringField(TEXT("class"), ClassPath(Component));

	if (!Component)
	{
		return Object;
	}

	Object->SetStringField(TEXT("staticMesh"), ObjectPath(Component->GetStaticMesh()));
	Object->SetStringField(TEXT("mobility"), StaticEnum<EComponentMobility::Type>()->GetNameStringByValue(static_cast<int64>(Component->Mobility)));
	Object->SetStringField(TEXT("collisionProfileName"), Component->GetCollisionProfileName().ToString());
	Object->SetBoolField(TEXT("visible"), Component->IsVisible());
	Object->SetBoolField(TEXT("castShadow"), Component->CastShadow);
	Object->SetObjectField(TEXT("bounds"), BoxObject(Component->Bounds.GetBox()));

	TArray<TSharedPtr<FJsonValue>> OverrideMaterialValues;
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		TSharedRef<FJsonObject> MaterialObject = MakeShared<FJsonObject>();
		MaterialObject->SetNumberField(TEXT("index"), Index);
		MaterialObject->SetStringField(TEXT("material"), ObjectPath(Component->GetMaterial(Index)));
		OverrideMaterialValues.Add(MakeShared<FJsonValueObject>(MaterialObject));
	}
	Object->SetArrayField(TEXT("materials"), OverrideMaterialValues);
	Object->SetObjectField(TEXT("staticMeshSnapshot"), StaticMeshObject(Component->GetStaticMesh()));

	return Object;
}

static TSharedRef<FJsonObject> CompiledSectionObject(ACompiledSection* Section)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), ObjectPath(Section));
	Object->SetStringField(TEXT("class"), ClassPath(Section));

	if (!Section)
	{
		return Object;
	}

	Object->SetStringField(TEXT("label"), Section->GetActorLabel());
	Object->SetStringField(TEXT("parentMegaMesh"), ObjectPath(Section->GetParentMegaMesh()));
	Object->SetStringField(TEXT("materialInstance"), ObjectPath(Section->GetMaterialInstance()));
	Object->SetStringField(TEXT("channelTexture"), ObjectPath(Section->GetChannelTexture()));
	Object->SetNumberField(TEXT("channelTableLength"), Section->GetChannelTable().Num());

	TArray<TSharedPtr<FJsonValue>> ChannelTableValues;
	for (const uint8 Value : Section->GetChannelTable())
	{
		ChannelTableValues.Add(MakeShared<FJsonValueNumber>(Value));
	}
	Object->SetArrayField(TEXT("channelTable"), ChannelTableValues);

	FVector2f ChannelTexcoordDesc = FVector2f::ZeroVector;
	if (ReadPrivateVector2fProperty(Section, TEXT("ChannelTexcoordDesc"), ChannelTexcoordDesc))
	{
		Object->SetObjectField(TEXT("channelTexcoordDesc"), Vector2fObject(ChannelTexcoordDesc));
	}
	else
	{
		Object->SetStringField(TEXT("channelTexcoordDesc"), TEXT("Unavailable"));
	}

	Object->SetStringField(TEXT("runtimeGrid"), Section->GetRuntimeGrid().ToString());
	Object->SetBoolField(TEXT("bIsSpatiallyLoaded"), Section->GetIsSpatiallyLoaded());
	Object->SetObjectField(TEXT("bounds"), BoxObject(Section->GetComponentsBoundingBox(true)));
	Object->SetObjectField(TEXT("buildInfo"), BuildInfoObject(Section->GetBuildInfo()));
	Object->SetStringField(TEXT("farFieldMesh"), ObjectPath(Section->GetFarFieldMesh()));

	TArray<TObjectPtr<UStaticMesh>> StaticMeshes = Section->GetStaticMeshes();
	Object->SetArrayField(TEXT("staticMeshes"), TypedObjectPathArray<UStaticMesh>(StaticMeshes));

	TArray<TSharedPtr<FJsonValue>> MeshComponentValues;
	for (const UMeshPartitionStaticMeshComponent* Component : Section->GetMeshComponents())
	{
		MeshComponentValues.Add(MakeShared<FJsonValueObject>(StaticMeshComponentObject(Component)));
	}
	Object->SetArrayField(TEXT("meshComponents"), MeshComponentValues);

	TArray<TSharedPtr<FJsonValue>> CollisionComponentValues;
	for (const UMeshPartitionCollisionComponent* Component : Section->GetCollisionComponents())
	{
		TSharedRef<FJsonObject> CollisionObject = MakeShared<FJsonObject>();
		CollisionObject->SetStringField(TEXT("path"), ObjectPath(Component));
		CollisionObject->SetStringField(TEXT("class"), ClassPath(Component));
		CollisionObject->SetStringField(TEXT("collisionProfileName"), Component ? Component->GetCollisionProfileName().ToString() : TEXT("None"));
		CollisionComponentValues.Add(MakeShared<FJsonValueObject>(CollisionObject));
	}
	Object->SetArrayField(TEXT("collisionComponents"), CollisionComponentValues);

	return Object;
}

static FMeshData CreateFlatMeshData(
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY)
{
	const int32 ClampedQuadsX = FMath::Max(1, QuadsX);
	const int32 ClampedQuadsY = FMath::Max(1, QuadsY);
	const double ClampedSizeX = FMath::Max(1.0, SizeX);
	const double ClampedSizeY = FMath::Max(1.0, SizeY);
	const double ClampedUVTilingX = FMath::Max(0.001, UVTilingX);
	const double ClampedUVTilingY = FMath::Max(0.001, UVTilingY);

	FMeshData MeshData;
	MeshData.SetNumSourceUVChannels(1);
	MeshData.ReserveAdditionalVertices((ClampedQuadsX + 1) * (ClampedQuadsY + 1));
	MeshData.ReserveAdditionalTriangles(ClampedQuadsX * ClampedQuadsY * 2);

	TArray<int32> Vertices;
	Vertices.SetNum((ClampedQuadsX + 1) * (ClampedQuadsY + 1));

	const auto GetVertexIndex = [ClampedQuadsX](int32 X, int32 Y)
	{
		return Y * (ClampedQuadsX + 1) + X;
	};

	for (int32 Y = 0; Y <= ClampedQuadsY; ++Y)
	{
		for (int32 X = 0; X <= ClampedQuadsX; ++X)
		{
			const double U = static_cast<double>(X) / static_cast<double>(ClampedQuadsX);
			const double V = static_cast<double>(Y) / static_cast<double>(ClampedQuadsY);
			const FVector3d Position(
				(U - 0.5) * ClampedSizeX,
				(V - 0.5) * ClampedSizeY,
				0.0);

			const int32 VertexID = MeshData.AppendVertex(Position);
			const FVector2f UV(
				static_cast<float>(U * ClampedUVTilingX),
				static_cast<float>(V * ClampedUVTilingY));

			Vertices[GetVertexIndex(X, Y)] = VertexID;
			MeshData.SetChannelUV(VertexID, UV);
			MeshData.SetVertexUV(VertexID, UV, 0);
			MeshData.SetVertexNormal(VertexID, FVector3f::UpVector);
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

static TSharedRef<FJsonObject> BuildTransientReferenceCompiledSectionObject(
	AMeshPartition* MeshPartition,
	const FName& BuildVariantName,
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY)
{
	if (!MeshPartition)
	{
		return StatusObject(false, TEXT("Actor is not an AMeshPartition."));
	}

	UMeshPartitionEditorComponent* EditorComponent = Cast<UMeshPartitionEditorComponent>(MeshPartition->GetMeshPartitionComponent());
	if (!EditorComponent)
	{
		return StatusObject(false, TEXT("MeshPartition does not have a UMeshPartitionEditorComponent."));
	}

	UMeshPartitionDefinition* Definition = MeshPartition->GetMeshPartitionDefinition();
	if (!Definition)
	{
		Definition = UMeshPartitionDefinition::StaticClass()->GetDefaultObject<UMeshPartitionDefinition>();
	}

	FMeshData MeshData = CreateFlatMeshData(QuadsX, QuadsY, SizeX, SizeY, UVTilingX, UVTilingY);
	ACompiledSection* Section = EditorComponent->SpawnTransientActor<ACompiledSection>(FTransform::Identity);
	if (!Section)
	{
		return StatusObject(false, TEXT("Failed to spawn transient ACompiledSection."));
	}

	Section->SetActorLabel(TEXT("MTR_TransientReferenceCompiledSection"));
	Section->SetParent(MeshPartition);

	FCompiledSectionBuildInfo BuildInfo;
	BuildInfo.BuildKey = FGuid::NewGuid();
	BuildInfo.BuildVariantName = BuildVariantName.IsNone() ? NAME_Default : BuildVariantName;
	BuildInfo.MegaMeshGUID = MeshPartition->GetActorGuid().IsValid() ? MeshPartition->GetActorGuid() : FGuid::NewGuid();
	BuildInfo.MegaMeshPath = MeshPartition;
	BuildInfo.ModifiersHash = FGuid::NewGuid();
	BuildInfo.ModifierSetHash = FGuid::NewGuid();
	BuildInfo.PackageHash = FGuid::NewGuid();
	BuildInfo.ClassHash = FGuid::NewGuid();
	BuildInfo.BuildVariantHash = FGuid::NewGuid();
	BuildInfo.SetMegaMeshDefinition(Definition);
	Section->SetBuildInfo(BuildInfo);

	const FCompiledSectionBuildVariant& BuildVariant = Definition->GetCompiledSectionBuildVariantByName(BuildInfo.BuildVariantName);
	FPrepareCompiledSectionsParams Params{.FullMesh = MeshData, .ReuseSection = Section, .bUseStaticMobility = true};
	TArray<ACompiledSection*> PreparedSections = EditorComponent->PrepareCompiledSections(BuildInfo, BuildVariant, Params);
	if (PreparedSections.IsEmpty() || PreparedSections[0] != Section)
	{
		const bool bDestroyed = Section->Destroy();
		TSharedRef<FJsonObject> Object = StatusObject(false, TEXT("PrepareCompiledSections failed to reuse transient section."));
		Object->SetBoolField(TEXT("transientSectionDestroyed"), bDestroyed);
		return Object;
	}

	FSectionChannels Channels = FChannelTextureRenderer::BuildTextureForSection(
		MeshData,
		Section,
		false,
		Definition->GetChannelMap(),
		Definition->GetChannelTexelSize()).GetResult();

	ApplyChannels(Section, Channels, Definition->GetMaterialCacheTexelSize());

	TArray<TWeakObjectPtr<UModifierComponent>> EmptyModifiers;
	TSharedPtr<const FMeshData> SharedMesh = MakeShared<const FMeshData>(MoveTemp(MeshData));
	EditorComponent->PostBuildSectionMesh(Section, *SharedMesh, EmptyModifiers);

	TArray<FTransformerUnit> Units;
	Units.Add(MakeTransformerUnit(Section, SharedMesh));

	TUniquePtr<FTransformerContext> TransformerContext = EditorComponent->LaunchTransformers(
		MoveTemp(Units),
		Definition,
		BuildVariant);

	if (TransformerContext.IsValid() && TransformerContext->JoinTask.IsValid() && !TransformerContext->JoinTask.IsCompleted())
	{
		WaitOnGameThread(*TransformerContext);
	}

	EditorComponent->PostProcessSection(Section, EmptyModifiers);

	TSharedRef<FJsonObject> Object = StatusObject(true, TEXT("Built transient reference compiled section through UMeshPartitionEditorComponent."));
	Object->SetStringField(TEXT("sourceMeshPartition"), ObjectPath(MeshPartition));
	Object->SetStringField(TEXT("definition"), ObjectPath(Definition));
	Object->SetStringField(TEXT("buildVariantName"), BuildInfo.BuildVariantName.ToString());
	Object->SetNumberField(TEXT("quadsX"), FMath::Max(1, QuadsX));
	Object->SetNumberField(TEXT("quadsY"), FMath::Max(1, QuadsY));
	Object->SetBoolField(TEXT("channelTextureDownloadedToAsset"), false);
	Object->SetObjectField(TEXT("compiledSection"), CompiledSectionObject(Section));

	const bool bDestroyed = Section->Destroy();
	Object->SetBoolField(TEXT("transientSectionDestroyed"), bDestroyed);

	return Object;
}

static TSharedRef<FJsonObject> BuildTransientReferenceCompiledSectionForDefinitionObject(
	UMeshPartitionDefinition* Definition,
	const FName& BuildVariantName,
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY)
{
	if (!Definition)
	{
		return StatusObject(false, TEXT("Definition path did not resolve to a UMeshPartitionDefinition."));
	}

	if (!GEditor)
	{
		return StatusObject(false, TEXT("GEditor is not available."));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->PersistentLevel)
	{
		return StatusObject(false, TEXT("Editor world or persistent level is not available."));
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.OverrideLevel = World->PersistentLevel;

	AMeshPartition* MeshPartition = World->SpawnActor<AMeshPartition>(
		AMeshPartition::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!MeshPartition)
	{
		return StatusObject(false, TEXT("Failed to spawn transient AMeshPartition."));
	}

	MeshPartition->SetActorLabel(TEXT("MTR_TransientReferenceMeshPartition"));

	UMeshPartitionEditorComponent* EditorComponent = NewObject<UMeshPartitionEditorComponent>(
		MeshPartition,
		UMeshPartitionEditorComponent::StaticClass(),
		TEXT("MegaMeshEditorComponent"),
		RF_Transient);

	if (!EditorComponent)
	{
		const bool bDestroyed = MeshPartition->Destroy();
		TSharedRef<FJsonObject> Object = StatusObject(false, TEXT("Failed to create transient UMeshPartitionEditorComponent."));
		Object->SetBoolField(TEXT("transientMeshPartitionDestroyed"), bDestroyed);
		return Object;
	}

	MeshPartition->SetMeshPartitionComponent(EditorComponent);
	MeshPartition->SetMeshPartitionDefinition(Definition);

	TSharedRef<FJsonObject> Object = BuildTransientReferenceCompiledSectionObject(
		MeshPartition,
		BuildVariantName,
		QuadsX,
		QuadsY,
		SizeX,
		SizeY,
		UVTilingX,
		UVTilingY);

	const bool bDestroyed = MeshPartition->Destroy();
	Object->SetBoolField(TEXT("transientMeshPartitionDestroyed"), bDestroyed);
	return Object;
}

static TSharedRef<FJsonObject> ComparisonObject(const UMeshPartitionDefinition* Left, const UMeshPartitionDefinition* Right)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("left"), ObjectPath(Left));
	Object->SetStringField(TEXT("right"), ObjectPath(Right));
	Object->SetBoolField(TEXT("bothValid"), Left != nullptr && Right != nullptr);

	if (!Left || !Right)
	{
		return Object;
	}

	const TArray<FName> LeftChannels = Left->GetChannelNames();
	const TArray<FName> RightChannels = Right->GetChannelNames();
	const TArray<FName> LeftPriorities = Left->GetModifierTypePriorities();
	const TArray<FName> RightPriorities = Right->GetModifierTypePriorities();

	Object->SetBoolField(TEXT("sameMaterial"), Left->GetMaterial() == Right->GetMaterial());
	Object->SetBoolField(TEXT("sameChannelNames"), LeftChannels == RightChannels);
	Object->SetBoolField(TEXT("sameModifierTypePriorities"), LeftPriorities == RightPriorities);
	Object->SetBoolField(TEXT("sameChannelTexelSize"), FMath::IsNearlyEqual(Left->GetChannelTexelSize(), Right->GetChannelTexelSize()));
	Object->SetBoolField(TEXT("sameMaterialCacheTexelSize"), FMath::IsNearlyEqual(Left->GetMaterialCacheTexelSize(), Right->GetMaterialCacheTexelSize()));
	Object->SetBoolField(TEXT("sameChannelUVLayoutMethod"), Left->GetChannelUVLayoutMethod() == Right->GetChannelUVLayoutMethod());
	Object->SetObjectField(TEXT("leftSnapshot"), DefinitionObject(Left));
	Object->SetObjectField(TEXT("rightSnapshot"), DefinitionObject(Right));

	return Object;
}
} // namespace MeshTerrainRuntimeLab::Parity

FString UMeshTerrainRuntimeLabParityToolset::SnapshotMeshPartition(AActor* Actor)
{
	return MeshTerrainRuntimeLab::Parity::WriteJson(
		MeshTerrainRuntimeLab::Parity::MeshPartitionObject(Cast<UE::MeshPartition::AMeshPartition>(Actor)));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotMeshPartitionByPath(const FString& ActorPath)
{
	return SnapshotMeshPartition(MeshTerrainRuntimeLab::Parity::FindOrLoadObject<AActor>(ActorPath));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotCompiledSection(AActor* Actor)
{
	return MeshTerrainRuntimeLab::Parity::WriteJson(
		MeshTerrainRuntimeLab::Parity::CompiledSectionObject(Cast<UE::MeshPartition::ACompiledSection>(Actor)));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotCompiledSectionByPath(const FString& ActorPath)
{
	return SnapshotCompiledSection(MeshTerrainRuntimeLab::Parity::FindOrLoadObject<AActor>(ActorPath));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotMeshPartitionDefinition(UE::MeshPartition::UMeshPartitionDefinition* Definition)
{
	return MeshTerrainRuntimeLab::Parity::WriteJson(MeshTerrainRuntimeLab::Parity::DefinitionObject(Definition));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotMeshPartitionDefinitionByPath(const FString& DefinitionPath)
{
	return SnapshotMeshPartitionDefinition(
		MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UE::MeshPartition::UMeshPartitionDefinition>(DefinitionPath));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotStaticMesh(UStaticMesh* StaticMesh)
{
	return MeshTerrainRuntimeLab::Parity::WriteJson(MeshTerrainRuntimeLab::Parity::StaticMeshObject(StaticMesh));
}

FString UMeshTerrainRuntimeLabParityToolset::SnapshotStaticMeshByPath(const FString& StaticMeshPath)
{
	return SnapshotStaticMesh(MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UStaticMesh>(StaticMeshPath));
}

FString UMeshTerrainRuntimeLabParityToolset::CompareMeshPartitionDefinitionsByPath(
	const FString& LeftDefinitionPath,
	const FString& RightDefinitionPath)
{
	const UE::MeshPartition::UMeshPartitionDefinition* Left =
		MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UE::MeshPartition::UMeshPartitionDefinition>(LeftDefinitionPath);
	const UE::MeshPartition::UMeshPartitionDefinition* Right =
		MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UE::MeshPartition::UMeshPartitionDefinition>(RightDefinitionPath);
	return MeshTerrainRuntimeLab::Parity::WriteJson(MeshTerrainRuntimeLab::Parity::ComparisonObject(Left, Right));
}

FString UMeshTerrainRuntimeLabParityToolset::BuildTransientReferenceCompiledSectionByPath(
	const FString& ActorPath,
	const FString& BuildVariantName,
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY)
{
	UE::MeshPartition::AMeshPartition* MeshPartition =
		MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UE::MeshPartition::AMeshPartition>(ActorPath);
	const FName VariantName = BuildVariantName.IsEmpty() ? NAME_Default : FName(*BuildVariantName);
	return MeshTerrainRuntimeLab::Parity::WriteJson(
		MeshTerrainRuntimeLab::Parity::BuildTransientReferenceCompiledSectionObject(
			MeshPartition,
			VariantName,
			QuadsX,
			QuadsY,
			SizeX,
			SizeY,
			UVTilingX,
			UVTilingY));
}

FString UMeshTerrainRuntimeLabParityToolset::BuildTransientReferenceCompiledSectionForDefinitionByPath(
	const FString& DefinitionPath,
	const FString& BuildVariantName,
	int32 QuadsX,
	int32 QuadsY,
	double SizeX,
	double SizeY,
	double UVTilingX,
	double UVTilingY)
{
	UE::MeshPartition::UMeshPartitionDefinition* Definition =
		MeshTerrainRuntimeLab::Parity::FindOrLoadObject<UE::MeshPartition::UMeshPartitionDefinition>(DefinitionPath);
	const FName VariantName = BuildVariantName.IsEmpty() ? NAME_Default : FName(*BuildVariantName);
	return MeshTerrainRuntimeLab::Parity::WriteJson(
		MeshTerrainRuntimeLab::Parity::BuildTransientReferenceCompiledSectionForDefinitionObject(
			Definition,
			VariantName,
			QuadsX,
			QuadsY,
			SizeX,
			SizeY,
			UVTilingX,
			UVTilingY));
}
