// Localization targets: the settings the Localization Dashboard edits, and the commandlet configs generated from them.

#include "Containers/Ticker.h"
#include "Engine/Blueprint.h"
#include "Internationalization/TextLocalizationManager.h"
#include "LocalizationConfigurationScript.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "Misc/Paths.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

void UCli::RegisterLocTools(FToolRegistry& R)
{
	R.Add(TEXT("loc_preview"), TEXT("localization"), TEXT("Preview the game's compiled translations in the editor: culture (e.g. ja) switches the game text to it, empty or off goes back to the native text. With asset and property, returns how that text property (on the asset, or a Blueprint's defaults) reads now."),
		TEXT(R"({"type":"object","properties":{"culture":{"type":"string"},"asset":{"type":"string"},"property":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FString Culture = Call->GetString(TEXT("culture"));
			// Reload the .locres files first (a compile since startup is otherwise not seen), then switch.
			TSharedRef<bool> Loaded = MakeShared<bool>(false);
			FTextLocalizationManager::Get().RefreshResources([Loaded]() { *Loaded = true; });
			// The text refresh happens on the following frames; read afterwards.
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Culture, Frames, Loaded](float)
			{
				if (!*Loaded) return true;
				if (*Frames == 0)
				{
					if (Culture.IsEmpty() || Culture == TEXT("off")) FTextLocalizationManager::Get().DisableGameLocalizationPreview();
					else FTextLocalizationManager::Get().EnableGameLocalizationPreview(Culture);
				}
				if (++*Frames < 4) return true;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("culture"), Culture.IsEmpty() ? TEXT("off") : Culture);
				if (Call->Has(TEXT("asset")))
				{
					UObject* Obj = LoadObject<UObject>(nullptr, *Call->GetString(TEXT("asset")));
					if (UBlueprint* B = Cast<UBlueprint>(Obj)) Obj = B->GeneratedClass ? B->GeneratedClass->GetDefaultObject() : nullptr;
					const FTextProperty* P = Obj ? CastField<FTextProperty>(Obj->GetClass()->FindPropertyByName(*Call->GetString(TEXT("property")))) : nullptr;
					if (!P)
					{
						Call->Fail(TEXT("prop.not_found"), FString::Printf(TEXT("no text property '%s' on '%s'"), *Call->GetString(TEXT("property")), *Call->GetString(TEXT("asset"))));
						return false;
					}
					O->SetStringField(TEXT("text"), P->GetPropertyValue_InContainer(Obj).ToString());
				}
				Call->Succeed(O);
				return false;
			}), 0.0f);
		});

	R.Add(TEXT("loc_target"), TEXT("localization"), TEXT("Create or update a game localization target (default Game, which the game loads): native culture (default en), cultures to translate into (e.g. ja), and the content paths to gather text from (default Content/*). Writes the target settings and its Gather/Export/Import/Compile configs under Config/Localization."),
		TEXT(R"({"type":"object","properties":{"target":{"type":"string"},"native":{"type":"string"},"cultures":{"type":"array","items":{"type":"string"}},"paths":{"type":"array","items":{"type":"string"}}}})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			ULocalizationTargetSet* Set = ULocalizationSettings::GetGameTargetSet();
			const FString Name = Call->GetString(TEXT("target"), TEXT("Game"));
			ULocalizationTarget* Target = nullptr;
			for (ULocalizationTarget* T : Set->TargetObjects)
			{
				if (T && T->Settings.Name == Name) Target = T;
			}
			const bool bCreated = Target == nullptr;
			if (!Target)
			{
				Target = NewObject<ULocalizationTarget>(Set);
				Target->Settings.Name = Name;
				Target->Settings.Guid = FGuid::NewGuid();
				Set->TargetObjects.Add(Target);
			}
			FLocalizationTargetSettings& S = Target->Settings;

			auto CultureIndex = [&S](const FString& Culture)
			{
				for (int32 i = 0; i < S.SupportedCulturesStatistics.Num(); ++i)
				{
					if (S.SupportedCulturesStatistics[i].CultureName == Culture) return i;
				}
				FCultureStatistics Stats;
				Stats.CultureName = Culture;
				return S.SupportedCulturesStatistics.Add(Stats);
			};
			const FString Native = Call->GetString(TEXT("native"), S.SupportedCulturesStatistics.IsValidIndex(S.NativeCultureIndex) ? S.SupportedCulturesStatistics[S.NativeCultureIndex].CultureName : TEXT("en"));
			S.NativeCultureIndex = CultureIndex(Native);
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Call->Args->TryGetArrayField(TEXT("cultures"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr) CultureIndex(V->AsString());
			}

			S.GatherFromPackages.IsEnabled = true;
			if (Call->Args->TryGetArrayField(TEXT("paths"), Arr) || S.GatherFromPackages.IncludePathWildcards.Num() == 0)
			{
				S.GatherFromPackages.IncludePathWildcards.Reset();
				TArray<FString> Paths;
				if (Arr) for (const TSharedPtr<FJsonValue>& V : *Arr) Paths.Add(V->AsString());
				if (Paths.Num() == 0) Paths.Add(TEXT("Content/*"));
				for (FString P : Paths)
				{
					if (!P.EndsWith(TEXT("*"))) P = P / TEXT("*");
					FGatherTextIncludePath Include;
					Include.Pattern = P;
					S.GatherFromPackages.IncludePathWildcards.Add(Include);
				}
			}
			if (S.GatherFromPackages.FileExtensions.Num() == 0)
			{
				for (const TCHAR* Ext : { TEXT("*.umap"), TEXT("*.uasset") })
				{
					FGatherTextFileExtension E;
					E.Pattern = Ext;
					S.GatherFromPackages.FileExtensions.Add(E);
				}
			}
			GetMutableDefault<ULocalizationSettings>()->PostEditChange();
			LocalizationConfigurationScript::GenerateAllConfigFiles(Target);

			TArray<FString> Cultures;
			for (const FCultureStatistics& C : S.SupportedCulturesStatistics) Cultures.Add(C.CultureName);
			TArray<FString> Paths;
			for (const FGatherTextIncludePath& P : S.GatherFromPackages.IncludePathWildcards) Paths.Add(P.Pattern);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("target"), Name);
			O->SetBoolField(TEXT("created"), bCreated);
			O->SetStringField(TEXT("native"), Native);
			O->SetArrayField(TEXT("cultures"), StrArray(Cultures));
			O->SetArrayField(TEXT("paths"), StrArray(Paths));
			TSharedPtr<FJsonObject> Configs = MakeShared<FJsonObject>();
			Configs->SetStringField(TEXT("gather"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetGatherTextConfigPath(Target)));
			Configs->SetStringField(TEXT("export"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetExportTextConfigPath(Target)));
			Configs->SetStringField(TEXT("import"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetImportTextConfigPath(Target)));
			Configs->SetStringField(TEXT("compile"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetCompileTextConfigPath(Target)));
			O->SetObjectField(TEXT("configs"), Configs);
			O->SetStringField(TEXT("data"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetDataDirectory(Target)));
			Call->Succeed(O);
		});
}
