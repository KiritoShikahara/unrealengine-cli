// Lighting builds and project rendering settings.

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/RendererSettings.h"
#include "LightingBuildOptions.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	struct FLightBuild
	{
		bool bDone = false;
		bool bOk = false;
		double Start = 0;
		FDelegateHandle Ok, Failed;
	};

	/** Friendly names for the most used rendering settings: name -> (property, value for true, value for false). */
	struct FAlias { const TCHAR* Name; const TCHAR* Property; const TCHAR* OnValue; const TCHAR* OffValue; };
	const FAlias Aliases[] = {
		{ TEXT("lumen"), TEXT("DynamicGlobalIllumination"), TEXT("Lumen"), TEXT("None") },
		{ TEXT("lumenReflections"), TEXT("Reflections"), TEXT("Lumen"), TEXT("ScreenSpace") },
		{ TEXT("virtualShadowMaps"), TEXT("ShadowMapMethod"), TEXT("VirtualShadowMaps"), TEXT("ShadowMaps") },
		{ TEXT("nanite"), TEXT("bNanite"), TEXT("True"), TEXT("False") },
		{ TEXT("rayTracing"), TEXT("bEnableRayTracing"), TEXT("True"), TEXT("False") },
		{ TEXT("staticLighting"), TEXT("bAllowStaticLighting"), TEXT("True"), TEXT("False") },
		{ TEXT("antiAliasing"), TEXT("DefaultFeatureAntiAliasing"), nullptr, nullptr },
	};

	FString Export(FProperty* P, const UObject* O)
	{
		FString S;
		P->ExportTextItem_InContainer(S, O, nullptr, nullptr, PPF_None);
		return S;
	}
}

void UCli::RegisterLightTools(FToolRegistry& R)
{
	R.Add(TEXT("light_build"), TEXT("lighting"), TEXT("Build static lighting (Lightmass) for the open level as a job: quality preview|medium|high|production (default preview). Fails with lighting.build_in_progress while another build runs; projects without static lighting (r.AllowStaticLighting=0, Lumen-only) have nothing to build."),
		TEXT(R"({"type":"object","properties":{"quality":{"type":"string","enum":["preview","medium","high","production"]}}})"),
		EToolFlags::Mutating | EToolFlags::NoUndo | EToolFlags::LongRunning,
		[](const FToolCallRef& Call)
		{
			if (GEditor->IsLightingBuildCurrentlyRunning())
			{
				Call->Fail(TEXT("lighting.build_in_progress"), TEXT("a lighting build is already running"), TEXT("wait for it, or cancel it in the editor"));
				return;
			}
			if (GEditor->PlayWorld)
			{
				Call->Fail(TEXT("lighting.pie_running"), TEXT("lighting cannot be built during Play In Editor"), TEXT("unrealengine-cli pie stop"));
				return;
			}
			const FString Q = Call->GetString(TEXT("quality"), TEXT("preview"));
			FLightingBuildOptions Options;
			Options.QualityLevel = Q == TEXT("production") ? Quality_Production : Q == TEXT("high") ? Quality_High : Q == TEXT("medium") ? Quality_Medium : Quality_Preview;
			TSharedRef<FLightBuild> B = MakeShared<FLightBuild>();
			B->Start = FPlatformTime::Seconds();
			B->Ok = FEditorDelegates::OnLightingBuildSucceeded.AddLambda([B]() { B->bDone = true; B->bOk = true; });
			B->Failed = FEditorDelegates::OnLightingBuildFailed.AddLambda([B]() { B->bDone = true; B->bOk = false; });
			GEditor->BuildLighting(Options);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, B, Q](float)
			{
				const bool bRunning = GEditor->IsLightingBuildCurrentlyRunning();
				if (!B->bDone && (bRunning || FPlatformTime::Seconds() - B->Start < 2.0) && !Call->bCancelled) return true;
				FEditorDelegates::OnLightingBuildSucceeded.Remove(B->Ok);
				FEditorDelegates::OnLightingBuildFailed.Remove(B->Failed);
				if (!B->bOk)
				{
					Call->Fail(TEXT("lighting.build_failed"), TEXT("the lighting build failed or had nothing to build"),
						TEXT("static lighting needs r.AllowStaticLighting (render_settings staticLighting=true) and static or stationary lights; check: unrealengine-cli logs --severity warning"));
					return false;
				}
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("quality"), Q);
				O->SetNumberField(TEXT("seconds"), FMath::RoundToDouble((FPlatformTime::Seconds() - B->Start) * 10.0) / 10.0);
				Call->Succeed(O);
				return false;
			}), 0.2f);
		});

	R.Add(TEXT("render_settings"), TEXT("lighting"), TEXT("Read or change project rendering settings (DefaultEngine.ini). values: {name: value} where name is lumen, lumenReflections, virtualShadowMaps, nanite, rayTracing, staticLighting (true/false), antiAliasing (None, FXAA, TemporalAA, MSAA, TSR) or any URendererSettings property name with its UE text value. Returns each setting's before and after value and whether an editor restart is needed."),
		TEXT(R"({"type":"object","properties":{"values":{"type":"object"}}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			URendererSettings* S = GetMutableDefault<URendererSettings>();
			const TSharedPtr<FJsonObject>* Values = nullptr;
			TArray<TSharedPtr<FJsonValue>> Out;
			bool bRestart = false;
			if (!Call->Args->TryGetObjectField(TEXT("values"), Values) || (*Values)->Values.Num() == 0)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				for (const FAlias& A : Aliases)
				{
					if (FProperty* P = S->GetClass()->FindPropertyByName(A.Property)) O->SetStringField(A.Name, Export(P, S));
				}
				TSharedPtr<FJsonObject> R2 = MakeShared<FJsonObject>();
				R2->SetObjectField(TEXT("settings"), O);
				Call->Succeed(R2);
				return;
			}
			// Validate every value before changing anything.
			struct FChange { FProperty* P; FString Name, Text; };
			TArray<FChange> Changes;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& V : (*Values)->Values)
			{
				const FAlias* Alias = nullptr;
				for (const FAlias& A : Aliases) if (V.Key.Equals(A.Name, ESearchCase::IgnoreCase)) Alias = &A;
				FProperty* P = S->GetClass()->FindPropertyByName(Alias ? Alias->Property : *V.Key);
				if (!P || !P->HasAnyPropertyFlags(CPF_Config))
				{
					Call->Fail(TEXT("render.unknown_setting"), FString::Printf(TEXT("'%s' is not a rendering setting"), *V.Key), TEXT("names: lumen, lumenReflections, virtualShadowMaps, nanite, rayTracing, staticLighting, antiAliasing, or a URendererSettings property"));
					return;
				}
				FString Text;
				if (V.Value->Type == EJson::Boolean && Alias && Alias->OnValue) Text = V.Value->AsBool() ? Alias->OnValue : Alias->OffValue;
				else if (V.Value->Type == EJson::Boolean) Text = V.Value->AsBool() ? TEXT("True") : TEXT("False");
				else Text = V.Value->AsString();
				if (Alias && FString(Alias->Property) == TEXT("DefaultFeatureAntiAliasing") && !Text.StartsWith(TEXT("AAM_"))) Text = TEXT("AAM_") + Text;
				Changes.Add({ P, V.Key, Text });
			}
			URendererSettings* Probe = NewObject<URendererSettings>(GetTransientPackage());
			for (const FChange& C : Changes)
			{
				if (!C.P->ImportText_InContainer(*C.Text, Probe, Probe, PPF_None, GLog))
				{
					Call->Fail(TEXT("render.bad_value"), FString::Printf(TEXT("'%s' is not a valid value for %s"), *C.Text, *C.Name));
					return;
				}
			}
			for (const FChange& C : Changes)
			{
				const FString Before = Export(C.P, S);
				S->PreEditChange(C.P);
				C.P->ImportText_InContainer(*C.Text, S, S, PPF_None, GLog);
				FPropertyChangedEvent Event(C.P, EPropertyChangeType::ValueSet);
				S->PostEditChangeProperty(Event);
				S->UpdateSinglePropertyInConfigFile(C.P, S->GetDefaultConfigFilename());
				bRestart |= C.P->HasMetaData(TEXT("ConfigRestartRequired"));
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), C.Name);
				O->SetStringField(TEXT("property"), C.P->GetName());
				O->SetStringField(TEXT("before"), Before);
				O->SetStringField(TEXT("after"), Export(C.P, S));
				Out.Add(Obj(O));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("changed"), Out);
			O->SetBoolField(TEXT("restartRequired"), bRestart);
			Call->Succeed(O);
		});
}
