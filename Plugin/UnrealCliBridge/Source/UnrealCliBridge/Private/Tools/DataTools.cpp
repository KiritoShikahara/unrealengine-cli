#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

using namespace UCli;

namespace
{
	UPackage* NewPackage(const FToolCallRef& Call, const FString& Path)
	{
		if (!FPackageName::IsValidLongPackageName(Path))
		{
			Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' is not a package path"), *Path));
			return nullptr;
		}
		if (FPackageName::DoesPackageExist(Path) || FindObject<UPackage>(nullptr, *Path))
		{
			Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("'%s' already exists"), *Path));
			return nullptr;
		}
		return CreatePackage(*Path);
	}
}

void UCli::RegisterDataTools(FToolRegistry& R)
{
	R.Add(TEXT("struct_create"), TEXT("data"), TEXT("Create a User Defined Struct with fields [{name, type}] (types as in bp_add_variable)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"fields":{"type":"array"}},"required":["path","fields"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const TArray<TSharedPtr<FJsonValue>> Fields = Call->GetArray(TEXT("fields"));
			TArray<TPair<FString, FEdGraphPinType>> Parsed;
			for (const TSharedPtr<FJsonValue>& F : Fields)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				FEdGraphPinType T;
				FString Error;
				if (!F->TryGetObject(O) || !BP::ParseType((*O)->GetStringField(TEXT("type")), T, Error))
				{
					Call->Fail(TEXT("args.invalid"), Error.IsEmpty() ? TEXT("each field needs name and type") : Error);
					return;
				}
				Parsed.Emplace((*O)->GetStringField(TEXT("name")), T);
			}
			if (Parsed.Num() == 0)
			{
				Call->Fail(TEXT("args.invalid"), TEXT("a struct needs at least one field"));
				return;
			}
			const FString Path = Call->GetString(TEXT("path"));
			UPackage* Package = NewPackage(Call, Path);
			if (!Package) return;
			UUserDefinedStruct* S = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
			const TArray<FGuid> Initial = [S]()
			{
				TArray<FGuid> G;
				for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(S)) G.Add(D.VarGuid);
				return G;
			}();
			for (const TPair<FString, FEdGraphPinType>& P : Parsed)
			{
				FStructureEditorUtils::AddVariable(S, P.Value);
				const FGuid NewGuid = FStructureEditorUtils::GetVarDesc(S).Last().VarGuid;
				FStructureEditorUtils::RenameVariable(S, NewGuid, P.Key);
			}
			for (const FGuid& G : Initial)
			{
				FStructureEditorUtils::RemoveVariable(S, G);
			}
			FAssetRegistryModule::AssetCreated(S);
			Package->MarkPackageDirty();
			TArray<FString> Names;
			for (const FStructVariableDescription& D : FStructureEditorUtils::GetVarDesc(S))
			{
				Names.Add(D.FriendlyName);
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), S->GetPathName());
			O->SetArrayField(TEXT("fields"), StrArray(Names));
			Call->Succeed(O);
		});

	R.Add(TEXT("enum_create"), TEXT("data"), TEXT("Create a User Defined Enum with display names."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"values":{"type":"array","items":{"type":"string"}}},"required":["path","values"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const TArray<FString> Values = Call->GetStringArray(TEXT("values"));
			if (Values.Num() == 0)
			{
				Call->Fail(TEXT("args.invalid"), TEXT("an enum needs at least one value"));
				return;
			}
			const FString Path = Call->GetString(TEXT("path"));
			UPackage* Package = NewPackage(Call, Path);
			if (!Package) return;
			UUserDefinedEnum* E = CastChecked<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional));
			while (E->NumEnums() - 1 < Values.Num())
			{
				FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(E);
			}
			for (int32 I = 0; I < Values.Num(); ++I)
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(E, I, FText::FromString(Values[I]));
			}
			FAssetRegistryModule::AssetCreated(E);
			Package->MarkPackageDirty();
			TArray<FString> Names;
			for (int32 I = 0; I < E->NumEnums() - 1; ++I)
			{
				Names.Add(E->GetDisplayNameTextByIndex(I).ToString());
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), E->GetPathName());
			O->SetArrayField(TEXT("values"), StrArray(Names));
			Call->Succeed(O);
		});
}
