// Chaos destruction: Geometry Collection assets built from static meshes.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

void UCli::RegisterPhysicsTools(FToolRegistry& R)
{
	R.Add(TEXT("geometry_collection_create"), TEXT("physics"), TEXT("Create a Chaos Geometry Collection (destructible) asset from one or more static meshes, each piece a bone. Fracture it further in the Fracture editor mode (Fracture plugin); place it in a level with actor_spawn (GeometryCollectionActor) and set its RestCollection."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"meshes":{"type":"array","items":{"type":"string"}}},"required":["path","meshes"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString Path = Call->GetString(TEXT("path"));
			FText Reason;
			if (!FPackageName::IsValidLongPackageName(Path, false, &Reason))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' is not a valid package path: %s"), *Path, *Reason.ToString()));
				return;
			}
			if (FPackageName::DoesPackageExist(Path) || FindObject<UPackage>(nullptr, *Path))
			{
				Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("'%s' already exists"), *Path));
				return;
			}
			TArray<UStaticMesh*> Meshes;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Call->Args->TryGetArrayField(TEXT("meshes"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *V->AsString());
					if (!M)
					{
						Call->Fail(TEXT("asset.not_found"), FString::Printf(TEXT("no static mesh at '%s'"), *V->AsString()));
						return;
					}
					Meshes.Add(M);
				}
			}
			if (Meshes.Num() == 0)
			{
				Call->Fail(TEXT("args.invalid"), TEXT("meshes is empty"));
				return;
			}
			UPackage* Package = CreatePackage(*Path);
			UGeometryCollection* GC = NewObject<UGeometryCollection>(Package, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
			int32 Appended = 0;
			for (int32 i = 0; i < Meshes.Num(); ++i)
			{
				TArray<UMaterialInterface*> Materials;
				for (const FStaticMaterial& Mat : Meshes[i]->GetStaticMaterials()) Materials.Add(Mat.MaterialInterface);
				// Pieces side by side along X so several meshes do not overlap.
				const FTransform At(FVector(i * Meshes[i]->GetBounds().BoxExtent.X * 2.2, 0, 0));
				Appended += FGeometryCollectionEngineConversion::AppendStaticMesh(Meshes[i], Materials, At, GC, false) ? 1 : 0;
			}
			if (Appended == 0)
			{
				Call->Fail(TEXT("physics.create_failed"), TEXT("no mesh could be converted (no render data?)"));
				return;
			}
			GC->InitializeMaterials();
			GC->InvalidateCollection();
#if WITH_EDITOR
			GC->RebuildRenderData();
#endif
			FAssetRegistryModule::AssetCreated(GC);
			Package->MarkPackageDirty();
			UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), GC->GetPathName());
			O->SetNumberField(TEXT("meshes"), Appended);
			O->SetNumberField(TEXT("bones"), GC->NumElements(FGeometryCollection::TransformGroup));
			Call->Succeed(O);
		});
}
