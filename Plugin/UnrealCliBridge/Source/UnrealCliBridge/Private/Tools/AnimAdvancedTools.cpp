// Motion Matching (PoseSearch) assets. The PoseSearch plugin is optional, so its classes are reached through reflection.

#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	/** A new asset of Class at Path (a plain UObject asset, no factory), or nullptr with Error set. */
	UObject* NewAsset(const FString& Path, UClass* Class, FString& Error)
	{
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(Path, false, &Reason))
		{
			Error = FString::Printf(TEXT("'%s' is not a valid package path: %s"), *Path, *Reason.ToString());
			return nullptr;
		}
		if (FPackageName::DoesPackageExist(Path) || FindObject<UPackage>(nullptr, *Path))
		{
			Error = FString::Printf(TEXT("'%s' already exists"), *Path);
			return nullptr;
		}
		UPackage* Package = CreatePackage(*Path);
		UObject* O = NewObject<UObject>(Package, Class, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(O);
		Package->MarkPackageDirty();
		return O;
	}

	bool Import(UObject* O, const TCHAR* Name, const FString& Text, FString& Error)
	{
		FProperty* P = O->GetClass()->FindPropertyByName(Name);
		if (!P || !P->ImportText_InContainer(*Text, O, O, PPF_None, GLog))
		{
			Error = FString::Printf(TEXT("cannot set %s.%s"), *O->GetClass()->GetName(), Name);
			return false;
		}
		return true;
	}
}

void UCli::RegisterAnimAdvancedTools(FToolRegistry& R)
{
	R.Add(TEXT("posesearch_create"), TEXT("anim"), TEXT("Create Motion Matching assets: <path>_Schema (Pose Search Schema for the skeleton) and the Pose Search Database at path holding the animations. Needs the PoseSearch plugin (posesearch.plugin_disabled otherwise). Tune the schema's channels in its editor."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"skeleton":{"type":"string"},"animations":{"type":"array","items":{"type":"string"}}},"required":["path","skeleton","animations"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UClass* SchemaClass = FindObject<UClass>(nullptr, TEXT("/Script/PoseSearch.PoseSearchSchema"));
			UClass* DbClass = FindObject<UClass>(nullptr, TEXT("/Script/PoseSearch.PoseSearchDatabase"));
			if (!SchemaClass || !DbClass)
			{
				Call->Fail(TEXT("posesearch.plugin_disabled"), TEXT("the PoseSearch (Motion Matching) plugin is not enabled"), TEXT("add { \"Name\": \"PoseSearch\", \"Enabled\": true } to the .uproject Plugins, then rebuild and restart"));
				return;
			}
			const FString Skeleton = Call->GetString(TEXT("skeleton"));
			if (!LoadObject<USkeleton>(nullptr, *Skeleton))
			{
				Call->Fail(TEXT("asset.not_found"), FString::Printf(TEXT("no skeleton at '%s'"), *Skeleton));
				return;
			}
			TArray<FString> Items, Anims;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Call->Args->TryGetArrayField(TEXT("animations"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					UObject* A = LoadObject<UObject>(nullptr, *V->AsString());
					if (!A || !A->GetClass()->GetName().StartsWith(TEXT("Anim")) && !A->GetClass()->GetName().Contains(TEXT("BlendSpace")))
					{
						Call->Fail(TEXT("asset.not_found"), FString::Printf(TEXT("no animation asset at '%s'"), *V->AsString()));
						return;
					}
					Anims.Add(A->GetPathName());
					Items.Add(FString::Printf(TEXT("(AnimAsset=\"%s\")"), *A->GetPathName()));
				}
			}
			const FString Path = Call->GetString(TEXT("path"));
			FString Error;
			UObject* Schema = NewAsset(Path + TEXT("_Schema"), SchemaClass, Error);
			UObject* Db = Schema ? NewAsset(Path, DbClass, Error) : nullptr;
			if (!Schema || !Db)
			{
				Call->Fail(TEXT("asset.create_failed"), Error);
				return;
			}
			bool bOk = Import(Schema, TEXT("Skeletons"), FString::Printf(TEXT("((Skeleton=\"%s\"))"), *Skeleton), Error)
				&& Import(Db, TEXT("Schema"), FString::Printf(TEXT("\"%s\""), *Schema->GetPathName()), Error)
				&& (Items.Num() == 0 || Import(Db, TEXT("DatabaseAnimationAssets"), FString::Printf(TEXT("(%s)"), *FString::Join(Items, TEXT(","))), Error));
			if (!bOk)
			{
				Call->Fail(TEXT("engine.unsupported"), Error, TEXT("this engine's PoseSearch assets differ; set them up in the editor"));
				return;
			}
			Schema->PostEditChange();
			Db->PostEditChange();
			UEditorLoadingAndSavingUtils::SavePackages({ Schema->GetOutermost(), Db->GetOutermost() }, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Db->GetPathName());
			O->SetStringField(TEXT("schema"), Schema->GetPathName());
			O->SetArrayField(TEXT("animations"), StrArray(Anims));
			Call->Succeed(O);
		});
}
