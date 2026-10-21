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
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "EngineUtils.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraCommon.h"
#include "EdGraphSchema_Niagara.h"
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

	bool IsParameterMapPin(const UEdGraphPin* P);

	FString Squash(const FString& S)
	{
		return S.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT("")).ToLower();
	}

	// An enum entry from its name, display name or number ("Cylinder", "ENiagara::Cylinder", 2).
	bool EnumValue(const UEnum* En, const TSharedPtr<FJsonValue>& V, int32& Out)
	{
		if (V->Type == EJson::Number) { Out = static_cast<int32>(V->AsNumber()); return true; }
		FString Wanted = V->AsString();
		Wanted.Split(TEXT("::"), nullptr, &Wanted, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		const FString Key = Squash(Wanted.IsEmpty() ? V->AsString() : Wanted);
		for (int32 I = 0; I < En->NumEnums() - 1; ++I)
		{
			if (Squash(En->GetNameStringByIndex(I)) == Key || Squash(En->GetDisplayNameTextByIndex(I).ToString()) == Key)
			{
				Out = static_cast<int32>(En->GetValueByIndex(I));
				return true;
			}
		}
		return false;
	}

	TArray<FString> EnumNames(const UEnum* En)
	{
		TArray<FString> Names;
		for (int32 I = 0; En && I < En->NumEnums() - 1; ++I) Names.Add(En->GetDisplayNameTextByIndex(I).ToString());
		return Names;
	}

	// A Niagara value as JSON (null when the type is not decoded).
	TSharedPtr<FJsonValue> DecodeValue(const FNiagaraTypeDefinition& Type, const uint8* D)
	{
		if (!D) return MakeShared<FJsonValueNull>();
		auto Nums = [](std::initializer_list<float> L) { TArray<TSharedPtr<FJsonValue>> A; for (float F : L) A.Add(MakeShared<FJsonValueNumber>(F)); return MakeShared<FJsonValueArray>(A); };
		if (Type == FNiagaraTypeDefinition::GetFloatDef()) return MakeShared<FJsonValueNumber>(*reinterpret_cast<const float*>(D));
		if (Type == FNiagaraTypeDefinition::GetIntDef()) return MakeShared<FJsonValueNumber>(*reinterpret_cast<const int32*>(D));
		if (Type == FNiagaraTypeDefinition::GetBoolDef()) return MakeShared<FJsonValueBoolean>(reinterpret_cast<const FNiagaraBool*>(D)->GetValue());
		if (Type.IsEnum() && Type.GetEnum()) return MakeShared<FJsonValueString>(Type.GetEnum()->GetDisplayNameTextByValue(*reinterpret_cast<const int32*>(D)).ToString());
		if (Type == FNiagaraTypeDefinition::GetVec2Def()) { const FVector2f& X = *reinterpret_cast<const FVector2f*>(D); return Nums({X.X, X.Y}); }
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef()) { const FVector3f& X = *reinterpret_cast<const FVector3f*>(D); return Nums({X.X, X.Y, X.Z}); }
		if (Type == FNiagaraTypeDefinition::GetVec4Def()) { const FVector4f& X = *reinterpret_cast<const FVector4f*>(D); return Nums({X.X, X.Y, X.Z, X.W}); }
		if (Type == FNiagaraTypeDefinition::GetColorDef()) { const FLinearColor& X = *reinterpret_cast<const FLinearColor*>(D); return Nums({X.R, X.G, X.B, X.A}); }
		return MakeShared<FJsonValueNull>();
	}

	// Module nodes of one stage in stack order, found by walking the parameter-map chain back from the stage output.
	TArray<UNiagaraNodeFunctionCall*> StageModules(UNiagaraNodeOutput* Output)
	{
		TArray<UNiagaraNodeFunctionCall*> Out;
		UEdGraphNode* Node = Output;
		TSet<UEdGraphNode*> Seen;
		while (Node && !Seen.Contains(Node))
		{
			Seen.Add(Node);
			UEdGraphNode* Next = nullptr;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P->Direction == EGPD_Input && IsParameterMapPin(P) && P->LinkedTo.Num() > 0)
				{
					Next = P->LinkedTo[0]->GetOwningNode();
					break;
				}
			}
			if (UNiagaraNodeFunctionCall* Fn = Cast<UNiagaraNodeFunctionCall>(Next))
			{
				Out.Insert(Fn, 0);
			}
			Node = Next;
		}
		return Out;
	}

	struct FEmitterRef
	{
		UNiagaraSystem* System = nullptr;
		FNiagaraEmitterHandle* Handle = nullptr;
		FVersionedNiagaraEmitterData* Data = nullptr;
		UNiagaraScriptSource* Source = nullptr;
	};

	bool RequireEmitter(const FToolCallRef& Call, FEmitterRef& R)
	{
		R.System = RequireSystem(Call);
		if (!R.System) return false;
		const FString Name = Call->GetString(TEXT("emitter"));
		R.Handle = R.System->GetEmitterHandles().FindByPredicate([&Name](const FNiagaraEmitterHandle& H) { return H.GetName().ToString() == Name; });
		R.Data = R.Handle ? R.Handle->GetEmitterData() : nullptr;
		R.Source = R.Data ? Cast<UNiagaraScriptSource>(R.Data->GraphSource) : nullptr;
		if (!R.Source || !R.Source->NodeGraph)
		{
			Call->Fail(TEXT("niagara.emitter_not_found"), FString::Printf(TEXT("system has no emitter '%s'"), *Name), TEXT("list: niagara_emitters_list"));
			return false;
		}
		return true;
	}

	TArray<UNiagaraScript*> StageScripts(UNiagaraSystem* S, FVersionedNiagaraEmitterData* Data, ENiagaraScriptUsage Usage)
	{
		TArray<UNiagaraScript*> Scripts;
		if (Usage == ENiagaraScriptUsage::EmitterSpawnScript || Usage == ENiagaraScriptUsage::EmitterUpdateScript)
		{
			Scripts.Add(Usage == ENiagaraScriptUsage::EmitterSpawnScript ? S->GetSystemSpawnScript() : S->GetSystemUpdateScript());
			return Scripts;
		}
		TArray<UNiagaraScript*> All;
		Data->GetScripts(All, false);
		for (UNiagaraScript* Script : All)
		{
			if (Script && UNiagaraScript::IsEquivalentUsage(Script->GetUsage(), Usage)) Scripts.Add(Script);
		}
		return Scripts;
	}

	TArray<UEdGraphPin*> StaticSwitchPins(UNiagaraNodeFunctionCall& Fn)
	{
		TArray<UEdGraphPin*> Pins;
		TSet<UEdGraphPin*> Hidden;
		FCompileConstantResolver Resolver;
		FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(Fn, Pins, Hidden, Resolver);
		return Pins;
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
		if (Type.IsEnum() && Type.GetEnum())
		{
			int32 I = 0;
			if (!EnumValue(Type.GetEnum(), V, I)) return false;
			Put(&I, sizeof I);
			TypeName = Type.GetEnum()->GetName();
			return true;
		}
		if (V->Type != EJson::Array) return false;
		if (Type == FNiagaraTypeDefinition::GetVec2Def()) { const FVector2f X(At(0), At(1)); Put(&X, sizeof X); TypeName = TEXT("vec2"); return true; }
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef()) { const FVector3f X(At(0), At(1), At(2)); Put(&X, sizeof X); TypeName = TEXT("vec3"); return true; }
		if (Type == FNiagaraTypeDefinition::GetVec4Def()) { const FVector4f X(At(0), At(1), At(2), At(3)); Put(&X, sizeof X); TypeName = TEXT("vec4"); return true; }
		if (Type == FNiagaraTypeDefinition::GetColorDef()) { const FLinearColor X(At(0), At(1), At(2), N.Num() > 3 ? At(3) : 1.f); Put(&X, sizeof X); TypeName = TEXT("color"); return true; }
		return false;
	}

	bool IsParameterMapPin(const UEdGraphPin* P)
	{
		return UEdGraphSchema_Niagara::PinToTypeDefinition(P) == FNiagaraTypeDefinition::GetParameterMapDef();
	}

	// The override pin for a module input lives on the map-set node feeding the module's parameter map input.
	UEdGraphPin* FindOverridePin(UNiagaraNodeFunctionCall& Fn, const FName PinName)
	{
		for (UEdGraphPin* In : Fn.Pins)
		{
			if (In->Direction != EGPD_Input || !IsParameterMapPin(In)) continue;
			for (UEdGraphPin* L : In->LinkedTo)
			{
				for (UEdGraphPin* P : L->GetOwningNode()->Pins)
				{
					if (P->Direction == EGPD_Input && P->PinName == PinName) return P;
				}
			}
		}
		return nullptr;
	}

	// Removes a node that no longer feeds anything, and then its now-unused inputs (never the shared parameter map chain).
	void DestroyIfOrphan(UEdGraphNode* Node)
	{
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P->Direction == EGPD_Output && P->LinkedTo.Num() > 0) return;
		}
		TArray<UEdGraphNode*> Upstream;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P->Direction != EGPD_Input || IsParameterMapPin(P)) continue;
			for (UEdGraphPin* L : P->LinkedTo) Upstream.AddUnique(L->GetOwningNode());
		}
		Node->Modify();
		Node->BreakAllNodeLinks();
		Node->DestroyNode();
		for (UEdGraphNode* U : Upstream) DestroyIfOrphan(U);
	}

	// Drops whatever currently drives an override pin (a link or a dynamic input).
	void ClearOverride(UEdGraphPin& Pin)
	{
		TArray<UEdGraphNode*> Upstream;
		for (UEdGraphPin* L : Pin.LinkedTo) Upstream.AddUnique(L->GetOwningNode());
		Pin.BreakAllPinLinks(true);
		for (UEdGraphNode* U : Upstream) DestroyIfOrphan(U);
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

	R.Add(TEXT("niagara_input_set"), TEXT("niagara"), TEXT("Set a module input: a local value (float, int, bool, or [x,y(,z(,w))] for vectors/colors), a link to a parameter (e.g. Particles.NormalizedAge, User.Speed) or a dynamic input script. module is the name niagara_module_add returned; a dynamic input's returned node name works as module for its own inputs."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"stage":{"type":"string","enum":["EmitterSpawn","EmitterUpdate","ParticleSpawn","ParticleUpdate"]},"module":{"type":"string"},"input":{"type":"string"},"value":{},"link":{"type":"string"},"dynamicInput":{"type":"string"}},"required":["system","emitter","stage","module","input"]})"),
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
			FString Wanted = TEXT("Module.") + Call->GetString(TEXT("input"));
			const FNiagaraVariable* Input = Inputs.FindByPredicate([&Wanted](const FNiagaraVariable& V) { return V.GetName().ToString() == Wanted; });
			if (!Input)
			{
				Input = Inputs.FindByPredicate([&Wanted](const FNiagaraVariable& V) { return Squash(V.GetName().ToString()) == Squash(Wanted); });
				if (Input) Wanted = Input->GetName().ToString();
			}
			const TArray<UEdGraphPin*> Switches = StaticSwitchPins(*Fn);
			if (!Input)
			{
				const FString Key = Squash(Call->GetString(TEXT("input")));
				if (UEdGraphPin* const* Found = Switches.FindByPredicate([&Key](const UEdGraphPin* P) { return Squash(P->PinName.ToString()) == Key; }))
				{
					// A static switch: its value is the default of the pin on the module node; changing it recompiles the stage.
					UEdGraphPin* Pin = *Found;
					const FNiagaraTypeDefinition Type = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
					const TSharedPtr<FJsonValue> V = Call->Args->TryGetField(TEXT("value"));
					if (!V.IsValid())
					{
						Call->Fail(TEXT("niagara.no_value"), TEXT("a static switch input takes value"));
						return;
					}
					int32 I = 0;
					if (Type.IsEnum() && Type.GetEnum())
					{
						if (!EnumValue(Type.GetEnum(), V, I))
						{
							Call->Fail(TEXT("niagara.bad_value"), FString::Printf(TEXT("'%s' is not a %s value"), *V->AsString(), *Type.GetEnum()->GetName()), FString::Printf(TEXT("values: %s"), *FString::Join(EnumNames(Type.GetEnum()), TEXT(", "))));
							return;
						}
					}
					else if (Type.GetStruct() == FNiagaraTypeDefinition::GetBoolStruct())
					{
						FNiagaraBool B;
						B.SetValue(V->AsBool());
						FMemory::Memcpy(&I, &B, sizeof I);
					}
					else
					{
						I = static_cast<int32>(V->AsNumber());
					}
					// The pin text form differs per type (enum entry names, not numbers): let the Niagara schema write it.
					FNiagaraVariable Var(Type, NAME_None);
					Var.SetData(reinterpret_cast<const uint8*>(&I));
					FString PinValue;
					if (!GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(Var, PinValue))
					{
						Call->Fail(TEXT("niagara.unsupported_input_type"), FString::Printf(TEXT("static switch '%s' has type %s, which this tool cannot set"), *Pin->PinName.ToString(), *Type.GetName()));
						return;
					}
					S->Modify();
					Source->NodeGraph->Modify();
					Pin->Modify();
					Pin->DefaultValue = PinValue;
					Fn->MarkNodeRequiresSynchronization(TEXT("unrealengine-cli static switch"), true);
					S->RequestCompile(false);
					S->MarkPackageDirty();
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("mode"), TEXT("staticSwitch"));
					O->SetStringField(TEXT("input"), Pin->PinName.ToString());
					O->SetStringField(TEXT("value"), PinValue);
					Call->Succeed(O);
					return;
				}
				TArray<FString> Names;
				for (const FNiagaraVariable& V : Inputs) Names.Add(V.GetName().ToString().RightChop(7));
				for (const UEdGraphPin* P : Switches) Names.Add(P->PinName.ToString());
				Call->Fail(TEXT("niagara.input_not_found"), FString::Printf(TEXT("module '%s' has no input '%s'"), *ModuleName, *Call->GetString(TEXT("input"))), FString::Printf(TEXT("inputs: %s (all with types and values: niagara_modules_list)"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Input->GetName()), Fn);
			if (Call->Has(TEXT("link")) || Call->Has(TEXT("dynamicInput")))
			{
				UNiagaraScript* Dynamic = nullptr;
				if (Call->Has(TEXT("dynamicInput")))
				{
					Dynamic = LoadObject<UNiagaraScript>(nullptr, *BP::ObjectPath(Call->GetString(TEXT("dynamicInput"))), nullptr, LOAD_NoWarn | LOAD_Quiet);
					if (!Dynamic)
					{
						Call->Fail(TEXT("niagara.dynamic_input_not_found"), FString::Printf(TEXT("no dynamic input script at '%s'"), *Call->GetString(TEXT("dynamicInput"))), TEXT("search: asset_search class=NiagaraScript pathPrefix=/Niagara/DynamicInputs"));
						return;
					}
				}
				S->Modify();
				Source->NodeGraph->Modify();
				UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Fn, Aliased, Input->GetType(), FGuid(), FGuid());
				ClearOverride(Pin);
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				if (Dynamic)
				{
					UNiagaraNodeFunctionCall* DynamicNode = nullptr;
					FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(Pin, Dynamic, DynamicNode);
					O->SetStringField(TEXT("mode"), TEXT("dynamicInput"));
					O->SetStringField(TEXT("node"), DynamicNode ? DynamicNode->GetFunctionName() : FString());
				}
				else
				{
					const FNiagaraVariableBase Linked(Input->GetType(), FName(*Call->GetString(TEXT("link"))));
					FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Linked, TSet<FNiagaraVariableBase>());
					O->SetStringField(TEXT("mode"), TEXT("link"));
					O->SetStringField(TEXT("link"), Linked.GetName().ToString());
				}
				Source->NodeGraph->NotifyGraphChanged();
				S->RequestCompile(false);
				S->MarkPackageDirty();
				Call->Succeed(O);
				return;
			}
			if (!Call->Has(TEXT("value")))
			{
				Call->Fail(TEXT("niagara.no_value"), TEXT("pass one of value, link or dynamicInput"));
				return;
			}
			TArray<uint8> Bytes;
			FString TypeName;
			if (!EncodeValue(Input->GetType(), Call->Args->TryGetField(TEXT("value")), Bytes, TypeName))
			{
				Call->Fail(TEXT("niagara.unsupported_input_type"), FString::Printf(TEXT("input '%s' has type %s, which this tool cannot set (or the value has the wrong shape)"), *Wanted, *Input->GetType().GetName()), TEXT("use python with the Niagara editor API"));
				return;
			}
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
			// A link or dynamic input set earlier would win over the local value: remove that override.
			if (UEdGraphPin* Override = FindOverridePin(*Fn, Aliased.GetParameterHandleString()))
			{
				Source->NodeGraph->Modify();
				ClearOverride(*Override);
				UEdGraphNode* Owner = Override->GetOwningNode();
				Owner->Modify();
				Owner->RemovePin(Override);
				Source->NodeGraph->NotifyGraphChanged();
			}
			for (UNiagaraScript* Script : Scripts)
			{
				if (!Script) continue;
				Script->Modify();
				Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), Rapid, true);
			}
			S->RequestCompile(false);
			S->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("mode"), TEXT("value"));
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

	R.Add(TEXT("niagara_modules_list"), TEXT("niagara"), TEXT("Modules of an emitter per stage in stack order, each with enabled and its inputs (name, type, kind input|staticSwitch, current local value, enum options), plus the emitter's renderers. Use the names with niagara_input_set, niagara_module_enable and niagara_renderer_set."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"stage":{"type":"string","enum":["EmitterSpawn","EmitterUpdate","ParticleSpawn","ParticleUpdate"]}},"required":["system","emitter"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			FEmitterRef E;
			if (!RequireEmitter(Call, E)) return;
			TArray<FString> Stages = {TEXT("EmitterSpawn"), TEXT("EmitterUpdate"), TEXT("ParticleSpawn"), TEXT("ParticleUpdate")};
			if (Call->Has(TEXT("stage"))) Stages = {Call->GetString(TEXT("stage"))};
			TArray<TSharedPtr<FJsonValue>> StageItems;
			for (const FString& Stage : Stages)
			{
				const ENiagaraScriptUsage Usage = StageUsage(Stage);
				UNiagaraNodeOutput* Output = E.Source->NodeGraph->FindEquivalentOutputNode(Usage);
				if (!Output) continue;
				const TArray<UNiagaraScript*> Scripts = StageScripts(E.System, E.Data, Usage);
				TArray<TSharedPtr<FJsonValue>> Modules;
				for (UNiagaraNodeFunctionCall* Fn : StageModules(Output))
				{
					TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
					M->SetStringField(TEXT("name"), Fn->GetFunctionName());
					M->SetBoolField(TEXT("enabled"), Fn->IsNodeEnabled());
					if (Fn->FunctionScript) M->SetStringField(TEXT("script"), Fn->FunctionScript->GetPathName());
					TArray<TSharedPtr<FJsonValue>> InputItems;
					TArray<FNiagaraVariable> Inputs;
					FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Fn, Inputs, FCompileConstantResolver(), FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
					for (const FNiagaraVariable& V : Inputs)
					{
						TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
						I->SetStringField(TEXT("name"), V.GetName().ToString().RightChop(7));
						I->SetStringField(TEXT("type"), V.GetType().GetName());
						I->SetStringField(TEXT("kind"), TEXT("input"));
						const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(V.GetName()), Fn);
						UEdGraphPin* Override = FindOverridePin(*Fn, Aliased.GetParameterHandleString());
						if (Override && Override->LinkedTo.Num() > 0)
						{
							I->SetStringField(TEXT("mode"), TEXT("linkedOrDynamic"));
						}
						else if (Scripts.Num() > 0 && Scripts[0])
						{
							const FNiagaraVariable Rapid(V.GetType(), FName(*FNiagaraUtilities::CreateRapidIterationConstantName(Aliased.GetParameterHandleString(), *E.Handle->GetUniqueInstanceName(), Usage)));
							I->SetField(TEXT("value"), DecodeValue(V.GetType(), Scripts[0]->RapidIterationParameters.GetParameterData(Rapid)));
						}
						if (V.GetType().IsEnum()) I->SetArrayField(TEXT("options"), StrArray(EnumNames(V.GetType().GetEnum())));
						InputItems.Add(Obj(I));
					}
					for (UEdGraphPin* P : StaticSwitchPins(*Fn))
					{
						const FNiagaraTypeDefinition Type = UEdGraphSchema_Niagara::PinToTypeDefinition(P);
						TSharedPtr<FJsonObject> I = MakeShared<FJsonObject>();
						I->SetStringField(TEXT("name"), P->PinName.ToString());
						I->SetStringField(TEXT("type"), Type.GetName());
						I->SetStringField(TEXT("kind"), TEXT("staticSwitch"));
						if (Type.IsEnum() && Type.GetEnum())
						{
							const UEnum* En = Type.GetEnum();
							int64 Value = En->GetValueByNameString(P->DefaultValue);
							if (Value == INDEX_NONE) Value = FCString::Atoi(*P->DefaultValue);
							I->SetStringField(TEXT("value"), En->GetDisplayNameTextByValue(Value).ToString());
							I->SetArrayField(TEXT("options"), StrArray(EnumNames(Type.GetEnum())));
						}
						else
						{
							I->SetStringField(TEXT("value"), P->DefaultValue);
						}
						InputItems.Add(Obj(I));
					}
					M->SetArrayField(TEXT("inputs"), InputItems);
					Modules.Add(Obj(M));
				}
				TSharedPtr<FJsonObject> SO = MakeShared<FJsonObject>();
				SO->SetStringField(TEXT("stage"), Stage);
				SO->SetArrayField(TEXT("modules"), Modules);
				StageItems.Add(Obj(SO));
			}
			TArray<TSharedPtr<FJsonValue>> Renderers;
			const TArray<UNiagaraRendererProperties*>& List = E.Data->GetRenderers();
			for (int32 Index = 0; Index < List.Num(); ++Index)
			{
				if (!List[Index]) continue;
				TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
				RO->SetNumberField(TEXT("index"), Index);
				RO->SetStringField(TEXT("class"), List[Index]->GetClass()->GetName());
				RO->SetBoolField(TEXT("enabled"), List[Index]->GetIsEnabled());
				if (const FObjectPropertyBase* Mat = CastField<FObjectPropertyBase>(List[Index]->GetClass()->FindPropertyByName(TEXT("Material"))))
				{
					const UObject* M = Mat->GetObjectPropertyValue_InContainer(List[Index]);
					RO->SetStringField(TEXT("material"), M ? M->GetPathName() : FString());
				}
				Renderers.Add(Obj(RO));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("emitter"), Call->GetString(TEXT("emitter")));
			O->SetArrayField(TEXT("stages"), StageItems);
			O->SetArrayField(TEXT("renderers"), Renderers);
			Call->Succeed(O);
		});

	R.Add(TEXT("niagara_module_enable"), TEXT("niagara"), TEXT("Enable or disable a module in an emitter stage, as the checkbox in the Niagara stack does."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"stage":{"type":"string","enum":["EmitterSpawn","EmitterUpdate","ParticleSpawn","ParticleUpdate"]},"module":{"type":"string"},"enabled":{"type":"boolean"}},"required":["system","emitter","stage","module","enabled"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			FEmitterRef E;
			if (!RequireEmitter(Call, E)) return;
			const FString Stage = Call->GetString(TEXT("stage"));
			UNiagaraNodeOutput* Output = E.Source->NodeGraph->FindEquivalentOutputNode(StageUsage(Stage));
			const FString ModuleName = Call->GetString(TEXT("module"));
			TArray<UNiagaraNodeFunctionCall*> Modules = Output ? StageModules(Output) : TArray<UNiagaraNodeFunctionCall*>();
			UNiagaraNodeFunctionCall* const* Fn = Modules.FindByPredicate([&ModuleName](const UNiagaraNodeFunctionCall* M) { return M->GetFunctionName() == ModuleName; });
			if (!Fn)
			{
				TArray<FString> Names;
				for (const UNiagaraNodeFunctionCall* M : Modules) Names.Add(M->GetFunctionName());
				Call->Fail(TEXT("niagara.module_not_found"), FString::Printf(TEXT("no module '%s' in stage %s"), *ModuleName, *Stage), FString::Printf(TEXT("modules: %s"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			E.System->Modify();
			E.Source->NodeGraph->Modify();
			FNiagaraStackGraphUtilities::SetModuleIsEnabled(**Fn, Call->GetBool(TEXT("enabled")));
			E.Source->NodeGraph->NotifyGraphChanged();
			E.System->RequestCompile(false);
			E.System->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("module"), ModuleName);
			O->SetBoolField(TEXT("enabled"), (*Fn)->IsNodeEnabled());
			Call->Succeed(O);
		});

	R.Add(TEXT("niagara_renderer_set"), TEXT("niagara"), TEXT("Set a property of an emitter's renderer (index or class, e.g. SpriteRenderer; see niagara_modules_list), e.g. property=Material value=/Game/M_Glow, or bIsEnabled, SubImageSize, Alignment."),
		TEXT(R"({"type":"object","properties":{"system":{"type":"string"},"emitter":{"type":"string"},"renderer":{"description":"index or class name: SpriteRenderer, MeshRenderer, RibbonRenderer..."},"property":{"type":"string"},"value":{}},"required":["system","emitter","property","value"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			FEmitterRef E;
			if (!RequireEmitter(Call, E)) return;
			const TArray<UNiagaraRendererProperties*>& List = E.Data->GetRenderers();
			UNiagaraRendererProperties* Target = nullptr;
			const TSharedPtr<FJsonValue> Sel = Call->Args->TryGetField(TEXT("renderer"));
			if (!Sel.IsValid())
			{
				Target = List.Num() > 0 ? List[0] : nullptr;
			}
			else if (Sel->Type == EJson::Number)
			{
				const int32 Index = static_cast<int32>(Sel->AsNumber());
				Target = List.IsValidIndex(Index) ? List[Index] : nullptr;
			}
			else
			{
				const FString Key = Squash(Sel->AsString());
				for (UNiagaraRendererProperties* P : List)
				{
					if (P && Squash(P->GetClass()->GetName()).Contains(Key)) { Target = P; break; }
				}
			}
			if (!Target)
			{
				TArray<FString> Names;
				for (int32 I = 0; I < List.Num(); ++I) if (List[I]) Names.Add(FString::Printf(TEXT("%d %s"), I, *List[I]->GetClass()->GetName()));
				Call->Fail(TEXT("niagara.renderer_not_found"), TEXT("no such renderer on the emitter"), FString::Printf(TEXT("renderers: %s"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			const FString PropName = Call->GetString(TEXT("property"));
			FProperty* Prop = nullptr;
			for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
			{
				if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase) || Squash(It->GetDisplayNameText().ToString()) == Squash(PropName)) { Prop = *It; break; }
			}
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit))
			{
				Call->Fail(TEXT("niagara.property_not_found"), FString::Printf(TEXT("%s has no editable property '%s'"), *Target->GetClass()->GetName(), *PropName), TEXT("examples: Material, bIsEnabled, Alignment, FacingMode, SubImageSize, SortMode"));
				return;
			}
			const TSharedPtr<FJsonValue> V = Call->Args->TryGetField(TEXT("value"));
			Target->Modify();
			Target->PreEditChange(Prop);
			bool bSet = false;
			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				const FString Path = V->AsString();
				UObject* Value = Path.IsEmpty() || Path == TEXT("None") ? nullptr : LoadObject<UObject>(nullptr, *BP::ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
				if ((Value && Value->IsA(ObjProp->PropertyClass)) || (!Value && (Path.IsEmpty() || Path == TEXT("None"))))
				{
					ObjProp->SetObjectPropertyValue_InContainer(Target, Value);
					bSet = true;
				}
			}
			else
			{
				const FString Text = V->Type == EJson::String ? V->AsString() : V->Type == EJson::Boolean ? (V->AsBool() ? TEXT("true") : TEXT("false")) : V->Type == EJson::Number ? FString::SanitizeFloat(V->AsNumber()) : FString();
				bSet = Prop->ImportText_InContainer(*Text, Target, Target, PPF_None) != nullptr;
			}
			if (!bSet)
			{
				Call->Fail(TEXT("niagara.bad_value"), FString::Printf(TEXT("cannot set %s to %s"), *Prop->GetName(), *V->AsString()), CastField<FObjectPropertyBase>(Prop) ? FString::Printf(TEXT("pass the path of a %s asset"), *CastField<FObjectPropertyBase>(Prop)->PropertyClass->GetName()) : TEXT("pass the value as the Details panel shows it"));
				return;
			}
			FPropertyChangedEvent Changed(Prop);
			Target->PostEditChangeProperty(Changed);
			E.System->RequestCompile(false);
			E.System->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("renderer"), Target->GetClass()->GetName());
			O->SetStringField(TEXT("property"), Prop->GetName());
			FString Now;
			Prop->ExportTextItem_InContainer(Now, Target, nullptr, Target, PPF_None);
			O->SetStringField(TEXT("value"), Now);
			Call->Succeed(O);
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
