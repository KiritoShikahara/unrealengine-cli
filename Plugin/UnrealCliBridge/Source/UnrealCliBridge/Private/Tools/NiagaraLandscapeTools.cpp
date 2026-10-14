#include "Editor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "EngineUtils.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterMapHistory.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

using namespace UCli;

namespace
{
	UNiagaraSystem* RequireSystem(const FToolCallRef& Call)
	{
		UNiagaraSystem* S = LoadObject<UNiagaraSystem>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("system"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!S)
		{
			Call->Fail(TEXT("niagara.not_found"), FString::Printf(TEXT("no Niagara System at '%s'"), *Call->GetString(TEXT("system"))), TEXT("create one: niagara_system_create"));
		}
		return S;
	}

	TSharedPtr<FJsonObject> EmittersJson(UNiagaraSystem* S)
	{
		TArray<FString> Names;
		for (const FNiagaraEmitterHandle& H : S->GetEmitterHandles())
		{
			Names.Add(H.GetName().ToString());
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("system"), S->GetPathName());
		O->SetArrayField(TEXT("emitters"), StrArray(Names));
		return O;
	}

	ENiagaraScriptUsage StageUsage(const FString& Stage)
	{
		return Stage == TEXT("EmitterSpawn") ? ENiagaraScriptUsage::EmitterSpawnScript
			: Stage == TEXT("EmitterUpdate") ? ENiagaraScriptUsage::EmitterUpdateScript
			: Stage == TEXT("ParticleSpawn") ? ENiagaraScriptUsage::ParticleSpawnScript
			: ENiagaraScriptUsage::ParticleUpdateScript;
	}

	FNiagaraEmitterHandle* FindEmitter(UNiagaraSystem* S, const FString& Name)
	{
		return S->GetEmitterHandles().FindByPredicate([&Name](const FNiagaraEmitterHandle& H) { return H.GetName().ToString() == Name; });
	}

	// Encodes a JSON value as the bytes of a Niagara type; false when the type is not supported.
	bool EncodeValue(const FNiagaraTypeDefinition& Type, const TSharedPtr<FJsonValue>& V, TArray<uint8>& Out, FString& TypeName)
	{
		auto Put = [&Out](const void* P, int32 N) { Out.SetNumUninitialized(N); FMemory::Memcpy(Out.GetData(), P, N); };
		TArray<double> N;
		if (V->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& E : V->AsArray()) N.Add(E->AsNumber());
		}
		auto At = [&N](int32 I) { return static_cast<float>(N.IsValidIndex(I) ? N[I] : 0.0); };
		if (Type == FNiagaraTypeDefinition::GetFloatDef()) { const float F = static_cast<float>(V->AsNumber()); Put(&F, sizeof F); TypeName = TEXT("float"); return true; }
		if (Type == FNiagaraTypeDefinition::GetIntDef()) { const int32 I = static_cast<int32>(V->AsNumber()); Put(&I, sizeof I); TypeName = TEXT("int"); return true; }
		if (Type == FNiagaraTypeDefinition::GetBoolDef()) { FNiagaraBool B; B.SetValue(V->AsBool()); Put(&B, sizeof B); TypeName = TEXT("bool"); return true; }
		if (V->Type != EJson::Array) return false;
		if (Type == FNiagaraTypeDefinition::GetVec2Def()) { const FVector2f X(At(0), At(1)); Put(&X, sizeof X); TypeName = TEXT("vec2"); return true; }
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef()) { const FVector3f X(At(0), At(1), At(2)); Put(&X, sizeof X); TypeName = TEXT("vec3"); return true; }
		if (Type == FNiagaraTypeDefinition::GetVec4Def()) { const FVector4f X(At(0), At(1), At(2), At(3)); Put(&X, sizeof X); TypeName = TEXT("vec4"); return true; }
		if (Type == FNiagaraTypeDefinition::GetColorDef()) { const FLinearColor X(At(0), At(1), At(2), N.Num() > 3 ? At(3) : 1.f); Put(&X, sizeof X); TypeName = TEXT("color"); return true; }
		return false;
	}

	ALandscape* FindLandscape(const FString& Label)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			if (Label.IsEmpty() || It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void UCli::RegisterNiagaraLandscapeTools(FToolRegistry& R)
{
	R.Add(TEXT("emitter_add"), TEXT("niagara"), TEXT("Add an emitter asset (e.g. a template under /Niagara/DefaultAssets/Templates/Emitters) to a Niagara System."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"}},"required":["system","emitter"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			UNiagaraEmitter* E = LoadObject<UNiagaraEmitter>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("emitter"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!E)
			{
				Call->Fail(TEXT("niagara.emitter_not_found"), FString::Printf(TEXT("no emitter at '%s'"), *Call->GetString(TEXT("emitter"))), TEXT("search: asset_search class=NiagaraEmitter pathPrefix=/Niagara"));
				return;
			}
			S->Modify();
			FNiagaraEditorUtilities::AddEmitterToSystem(*S, *E, E->GetExposedVersion().VersionGuid);
			S->RequestCompile(false);
			S->MarkPackageDirty();
			Call->Succeed(EmittersJson(S));
		});

	R.Add(TEXT("niagara_module_add"), TEXT("niagara"), TEXT("Add a module script (e.g. /Niagara/Modules/Update/Forces/Drag) to an emitter stage: EmitterSpawn | EmitterUpdate | ParticleSpawn | ParticleUpdate."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"stage":{"type":"string","enum":["EmitterSpawn","EmitterUpdate","ParticleSpawn","ParticleUpdate"]},"module":{"type":"string"}},"required":["system","emitter","stage","module"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			const FString EmitterName = Call->GetString(TEXT("emitter"));
			FNiagaraEmitterHandle* Handle = S->GetEmitterHandles().FindByPredicate([&EmitterName](const FNiagaraEmitterHandle& H) { return H.GetName().ToString() == EmitterName; });
			FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
			UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Source || !Source->NodeGraph)
			{
				Call->Fail(TEXT("niagara.emitter_not_found"), FString::Printf(TEXT("system has no emitter '%s'"), *EmitterName), TEXT("list: niagara_emitters_list"));
				return;
			}
			const FString Stage = Call->GetString(TEXT("stage"));
			const ENiagaraScriptUsage Usage = Stage == TEXT("EmitterSpawn") ? ENiagaraScriptUsage::EmitterSpawnScript
				: Stage == TEXT("EmitterUpdate") ? ENiagaraScriptUsage::EmitterUpdateScript
				: Stage == TEXT("ParticleSpawn") ? ENiagaraScriptUsage::ParticleSpawnScript
				: ENiagaraScriptUsage::ParticleUpdateScript;
			UNiagaraNodeOutput* Output = Source->NodeGraph->FindEquivalentOutputNode(Usage);
			UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("module"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Output || !Module)
			{
				Call->Fail(TEXT("niagara.module_not_found"), FString::Printf(TEXT("no module script at '%s' (or no %s stage)"), *Call->GetString(TEXT("module")), *Stage), TEXT("search: asset_search class=NiagaraScript pathPrefix=/Niagara/Modules"));
				return;
			}
			S->Modify();
			Source->NodeGraph->Modify();
			UNiagaraNodeFunctionCall* Node = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output);
			if (!Node)
			{
				Call->Fail(TEXT("niagara.module_add_failed"), TEXT("the module could not be added to the stage"));
				return;
			}
			S->RequestCompile(false);
			S->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("emitter"), EmitterName);
			O->SetStringField(TEXT("stage"), Stage);
			O->SetStringField(TEXT("module"), Node->GetFunctionName());
			Call->Succeed(O);
		});

	R.Add(TEXT("niagara_input_set"), TEXT("niagara"), TEXT("Set a module input's local value (float, int, bool, or [x,y(,z(,w))] for vectors/colors). module is the name niagara_module_add returned."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"stage":{"type":"string","enum":["EmitterSpawn","EmitterUpdate","ParticleSpawn","ParticleUpdate"]},"module":{"type":"string"},"input":{"type":"string"},"value":{}},"required":["system","emitter","stage","module","input","value"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			const FString EmitterName = Call->GetString(TEXT("emitter"));
			FNiagaraEmitterHandle* Handle = FindEmitter(S, EmitterName);
			FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
			UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Source || !Source->NodeGraph)
			{
				Call->Fail(TEXT("niagara.emitter_not_found"), FString::Printf(TEXT("system has no emitter '%s'"), *EmitterName), TEXT("list: niagara_emitters_list"));
				return;
			}
			const ENiagaraScriptUsage Usage = StageUsage(Call->GetString(TEXT("stage")));
			const FString ModuleName = Call->GetString(TEXT("module"));
			UNiagaraNodeOutput* Output = Source->NodeGraph->FindEquivalentOutputNode(Usage);
			UNiagaraNodeFunctionCall* Fn = nullptr;
			if (Output)
			{
				TArray<UNiagaraNodeFunctionCall*> Modules;
				Source->NodeGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(Modules);
				for (UNiagaraNodeFunctionCall* M : Modules)
				{
					if (M->GetFunctionName() == ModuleName) { Fn = M; break; }
				}
			}
			if (!Fn)
			{
				Call->Fail(TEXT("niagara.module_not_found"), FString::Printf(TEXT("no module '%s' in stage %s"), *ModuleName, *Call->GetString(TEXT("stage"))), TEXT("add one: niagara_module_add"));
				return;
			}
			TArray<FNiagaraVariable> Inputs;
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Fn, Inputs, FCompileConstantResolver(), FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
			const FString Wanted = TEXT("Module.") + Call->GetString(TEXT("input"));
			const FNiagaraVariable* Input = Inputs.FindByPredicate([&Wanted](const FNiagaraVariable& V) { return V.GetName().ToString() == Wanted; });
			if (!Input)
			{
				TArray<FString> Names;
				for (const FNiagaraVariable& V : Inputs) Names.Add(V.GetName().ToString().RightChop(7));
				Call->Fail(TEXT("niagara.input_not_found"), FString::Printf(TEXT("module '%s' has no input '%s'"), *ModuleName, *Call->GetString(TEXT("input"))), FString::Printf(TEXT("inputs: %s"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			TArray<uint8> Bytes;
			FString TypeName;
			if (!EncodeValue(Input->GetType(), Call->Args->TryGetField(TEXT("value")), Bytes, TypeName))
			{
				Call->Fail(TEXT("niagara.unsupported_input_type"), FString::Printf(TEXT("input '%s' has type %s, which this tool cannot set (or the value has the wrong shape)"), *Wanted, *Input->GetType().GetName()), TEXT("use python with the Niagara editor API"));
				return;
			}
			const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input->GetName()), Fn);
			const FNiagaraVariable Rapid(Input->GetType(), FName(*FNiagaraUtilities::CreateRapidIterationConstantName(Aliased.GetParameterHandleString(), *Handle->GetUniqueInstanceName(), Usage)));
			TArray<UNiagaraScript*> Scripts;
			if (Usage == ENiagaraScriptUsage::EmitterSpawnScript || Usage == ENiagaraScriptUsage::EmitterUpdateScript)
			{
				Scripts.Add(Usage == ENiagaraScriptUsage::EmitterSpawnScript ? S->GetSystemSpawnScript() : S->GetSystemUpdateScript());
			}
			else
			{
				TArray<UNiagaraScript*> All;
				Data->GetScripts(All, false);
				for (UNiagaraScript* Script : All)
				{
					if (Script && UNiagaraScript::IsEquivalentUsage(Script->GetUsage(), Usage)) Scripts.Add(Script);
				}
			}
			S->Modify();
			for (UNiagaraScript* Script : Scripts)
			{
				if (!Script) continue;
				Script->Modify();
				Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), Rapid, true);
			}
			S->RequestCompile(false);
			S->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("parameter"), Rapid.GetName().ToString());
			O->SetStringField(TEXT("type"), TypeName);
			O->SetNumberField(TEXT("scripts"), Scripts.Num());
			Call->Succeed(O);
		});

	R.Add(TEXT("niagara_emitters_list"), TEXT("niagara"), TEXT("Emitters of a Niagara System."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"}},"required":["system"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UNiagaraSystem* S = RequireSystem(Call);
			if (!S) return;
			Call->Succeed(EmittersJson(S));
		});

	R.Add(TEXT("landscape_create"), TEXT("landscape"), TEXT("Create a flat landscape: components x components, each 63 quads, at location with scale."),
		TEXT(R"({"type":"object","properties":{"components":{"type":"integer"},"location":{"type":"array"},"scale":{"type":"array"},"label":{"type":"string"},"material":{"type":"string"}}})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const int32 Components = FMath::Clamp(static_cast<int32>(Call->GetNumber(TEXT("components"), 2)), 1, 32);
			const int32 Quads = 63;
			const int32 Size = Components * Quads + 1;
			auto Vec = [&Call](const TCHAR* Key, const FVector& Default)
			{
				const TArray<TSharedPtr<FJsonValue>> A = Call->GetArray(Key);
				return A.Num() == 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : Default;
			};
			UWorld* World = GEditor->GetEditorWorldContext().World();
			ALandscape* L = World->SpawnActor<ALandscape>(Vec(TEXT("location"), FVector::ZeroVector), FRotator::ZeroRotator);
			L->SetActorScale3D(Vec(TEXT("scale"), FVector(100, 100, 100)));
			if (Call->Has(TEXT("material")))
			{
				L->LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("material"))));
			}
			TArray<uint16> Heights;
			Heights.Init(32768, Size * Size);
			TMap<FGuid, TArray<uint16>> HeightLayers;
			HeightLayers.Add(FGuid(), MoveTemp(Heights));
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
			MaterialLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());
			L->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, 1, Quads, HeightLayers, nullptr, MaterialLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
			if (Call->Has(TEXT("label")))
			{
				L->SetActorLabel(Call->GetString(TEXT("label")));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("label"), L->GetActorLabel());
			O->SetNumberField(TEXT("size"), Size);
			O->SetNumberField(TEXT("components"), L->LandscapeComponents.Num());
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_sculpt"), TEXT("landscape"), TEXT("Raise (positive height, cm) or lower a circular area of a landscape with smooth falloff."),
		TEXT(R"({"type":"object","properties":{"label":{"type":"string"},"center":{"type":"array"},"radius":{"type":"number"},"height":{"type":"number"}},"required":["center","radius","height"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			ALandscape* L = FindLandscape(Call->GetString(TEXT("label")));
			ULandscapeInfo* Info = L ? L->GetLandscapeInfo() : nullptr;
			const TArray<TSharedPtr<FJsonValue>> C = Call->GetArray(TEXT("center"));
			if (!Info || C.Num() < 2)
			{
				Call->Fail(TEXT("landscape.not_found"), TEXT("no landscape (or center is not [x, y])"), TEXT("create one: landscape_create"));
				return;
			}
			const FTransform T = L->GetActorTransform();
			const FVector Local = T.InverseTransformPosition(FVector(C[0]->AsNumber(), C[1]->AsNumber(), 0));
			const double RadiusQ = Call->GetNumber(TEXT("radius")) / T.GetScale3D().X;
			const double DeltaUnits = Call->GetNumber(TEXT("height")) / (T.GetScale3D().Z / 128.0);
			int32 MinX, MinY, MaxX, MaxY;
			if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
			{
				Call->Fail(TEXT("landscape.empty"), TEXT("the landscape has no components"));
				return;
			}
			const int32 X1 = FMath::Clamp(FMath::FloorToInt(Local.X - RadiusQ), MinX, MaxX);
			const int32 Y1 = FMath::Clamp(FMath::FloorToInt(Local.Y - RadiusQ), MinY, MaxY);
			const int32 X2 = FMath::Clamp(FMath::CeilToInt(Local.X + RadiusQ), MinX, MaxX);
			const int32 Y2 = FMath::Clamp(FMath::CeilToInt(Local.Y + RadiusQ), MinY, MaxY);
			FLandscapeEditDataInterface Edit(Info);
			TArray<uint16> Data;
			Data.SetNumZeroed((X2 - X1 + 1) * (Y2 - Y1 + 1));
			int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
			Edit.GetHeightData(GX1, GY1, GX2, GY2, Data.GetData(), 0);
			int32 Changed = 0;
			for (int32 Y = Y1; Y <= Y2; ++Y)
			{
				for (int32 X = X1; X <= X2; ++X)
				{
					const double D = FVector2D::Distance(FVector2D(X, Y), FVector2D(Local.X, Local.Y));
					if (D > RadiusQ) continue;
					const double Falloff = 0.5 * (1.0 + FMath::Cos(PI * D / FMath::Max(RadiusQ, 1.0)));
					uint16& H = Data[(Y - Y1) * (X2 - X1 + 1) + (X - X1)];
					H = static_cast<uint16>(FMath::Clamp(H + DeltaUnits * Falloff, 0.0, 65535.0));
					++Changed;
				}
			}
			Edit.SetHeightData(X1, Y1, X2, Y2, Data.GetData(), 0, true);
			Edit.Flush();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("vertices"), Changed);
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_layer_add"), TEXT("landscape"), TEXT("Add a paint layer to a landscape, creating its LayerInfo asset (default /Game/LandscapeLayers/<layer>_LayerInfo) or reusing an existing one."),
		TEXT(R"({"type":"object","properties":{"label":{"type":"string"},"layer":{"type":"string"},"path":{"type":"string"}},"required":["layer"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			ALandscape* L = FindLandscape(Call->GetString(TEXT("label")));
			ULandscapeInfo* Info = L ? L->GetLandscapeInfo() : nullptr;
			if (!Info)
			{
				Call->Fail(TEXT("landscape.not_found"), TEXT("no landscape"), TEXT("create one: landscape_create"));
				return;
			}
			const FString Layer = Call->GetString(TEXT("layer"));
			const FString Path = Call->Has(TEXT("path")) ? Call->GetString(TEXT("path")) : FString::Printf(TEXT("/Game/LandscapeLayers/%s_LayerInfo"), *Layer);
			ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *BP::ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!LayerInfo)
			{
				UPackage* Pkg = CreatePackage(*Path);
				LayerInfo = NewObject<ULandscapeLayerInfoObject>(Pkg, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
				LayerInfo->SetLayerName(FName(*Layer), false);
				FAssetRegistryModule::AssetCreated(LayerInfo);
				Pkg->MarkPackageDirty();
			}
			L->Modify();
			if (!L->HasTargetLayer(LayerInfo))
			{
				L->AddTargetLayer(LayerInfo->GetLayerName(), FLandscapeTargetLayerSettings(LayerInfo, Path));
			}
			if (Info->GetLayerInfoIndex(LayerInfo) == INDEX_NONE)
			{
				Info->CreateTargetLayerSettingsFor(LayerInfo);
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("layer"), LayerInfo->GetLayerName().ToString());
			O->SetStringField(TEXT("layerInfo"), LayerInfo->GetPathName());
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_paint"), TEXT("landscape"), TEXT("Paint a layer (added with landscape_layer_add) over a circular area; strength 0..1 with smooth falloff."),
		TEXT(R"({"type":"object","properties":{"label":{"type":"string"},"layer":{"type":"string"},"center":{"type":"array"},"radius":{"type":"number"},"strength":{"type":"number"}},"required":["layer","center","radius"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			ALandscape* L = FindLandscape(Call->GetString(TEXT("label")));
			ULandscapeInfo* Info = L ? L->GetLandscapeInfo() : nullptr;
			const TArray<TSharedPtr<FJsonValue>> C = Call->GetArray(TEXT("center"));
			if (!Info || C.Num() < 2)
			{
				Call->Fail(TEXT("landscape.not_found"), TEXT("no landscape (or center is not [x, y])"), TEXT("create one: landscape_create"));
				return;
			}
			const FString Layer = Call->GetString(TEXT("layer"));
			const int32 Index = Info->GetLayerInfoIndex(FName(*Layer));
			ULandscapeLayerInfoObject* LayerInfo = Index != INDEX_NONE ? Info->Layers[Index].LayerInfoObj.Get() : nullptr;
			if (!LayerInfo)
			{
				Call->Fail(TEXT("landscape.layer_not_found"), FString::Printf(TEXT("the landscape has no paint layer '%s'"), *Layer), TEXT("add it: landscape_layer_add"));
				return;
			}
			const FTransform T = L->GetActorTransform();
			const FVector Local = T.InverseTransformPosition(FVector(C[0]->AsNumber(), C[1]->AsNumber(), 0));
			const double RadiusQ = Call->GetNumber(TEXT("radius")) / T.GetScale3D().X;
			const double Strength = FMath::Clamp(Call->GetNumber(TEXT("strength"), 1.0), 0.0, 1.0);
			int32 MinX, MinY, MaxX, MaxY;
			if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
			{
				Call->Fail(TEXT("landscape.empty"), TEXT("the landscape has no components"));
				return;
			}
			const int32 X1 = FMath::Clamp(FMath::FloorToInt(Local.X - RadiusQ), MinX, MaxX);
			const int32 Y1 = FMath::Clamp(FMath::FloorToInt(Local.Y - RadiusQ), MinY, MaxY);
			const int32 X2 = FMath::Clamp(FMath::CeilToInt(Local.X + RadiusQ), MinX, MaxX);
			const int32 Y2 = FMath::Clamp(FMath::CeilToInt(Local.Y + RadiusQ), MinY, MaxY);
			const int32 W = X2 - X1 + 1;
			FLandscapeEditDataInterface Edit(Info);
			TArray<uint8> Data;
			Data.SetNumZeroed(W * (Y2 - Y1 + 1));
			int32 GX1 = X1, GY1 = Y1, GX2 = X2, GY2 = Y2;
			Edit.GetWeightData(LayerInfo, GX1, GY1, GX2, GY2, Data.GetData(), 0);
			int32 Changed = 0;
			for (int32 Y = Y1; Y <= Y2; ++Y)
			{
				for (int32 X = X1; X <= X2; ++X)
				{
					const double D = FVector2D::Distance(FVector2D(X, Y), FVector2D(Local.X, Local.Y));
					if (D > RadiusQ) continue;
					const double Falloff = 0.5 * (1.0 + FMath::Cos(PI * D / FMath::Max(RadiusQ, 1.0)));
					uint8& V = Data[(Y - Y1) * W + (X - X1)];
					V = static_cast<uint8>(FMath::Max<double>(V, FMath::Clamp(255.0 * Strength * Falloff, 0.0, 255.0)));
					++Changed;
				}
			}
			Edit.SetAlphaData(LayerInfo, X1, Y1, X2, Y2, Data.GetData(), 0, ELandscapeLayerPaintingRestriction::None);
			Edit.Flush();
			const int32 CX = FMath::Clamp(FMath::RoundToInt(Local.X), MinX, MaxX);
			const int32 CY = FMath::Clamp(FMath::RoundToInt(Local.Y), MinY, MaxY);
			uint8 Center = 0;
			int32 PX1 = CX, PY1 = CY, PX2 = CX, PY2 = CY;
			Edit.GetWeightData(LayerInfo, PX1, PY1, PX2, PY2, &Center, 0);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("vertices"), Changed);
			O->SetNumberField(TEXT("centerWeight"), Center);
			Call->Succeed(O);
		});

	R.Add(TEXT("landscape_info"), TEXT("landscape"), TEXT("Landscapes in the level with size, component count and material."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			UWorld* World = GEditor->GetEditorWorldContext().World();
			for (TActorIterator<ALandscape> It(World); It; ++It)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("label"), It->GetActorLabel());
				O->SetNumberField(TEXT("components"), It->LandscapeComponents.Num());
				O->SetStringField(TEXT("material"), It->LandscapeMaterial ? It->LandscapeMaterial->GetPathName() : FString());
				int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
				if (ULandscapeInfo* Info = It->GetLandscapeInfo())
				{
					Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
				}
				O->SetNumberField(TEXT("sizeX"), MaxX - MinX + 1);
				O->SetNumberField(TEXT("sizeY"), MaxY - MinY + 1);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});
}
