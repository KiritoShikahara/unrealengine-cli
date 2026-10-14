#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "FileHelpers.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "UObject/UObjectIterator.h"

using namespace UCli;

namespace UCli::BP
{
	FString ObjectPath(const FString& Path)
	{
		if (Path.Contains(TEXT(".")))
		{
			return Path;
		}
		return Path + TEXT(".") + FPackageName::GetShortName(Path);
	}

	UBlueprint* Load(const FString& Path)
	{
		return LoadObject<UBlueprint>(nullptr, *ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UClass* ResolveClass(const FString& InName)
	{
		FString Name = InName.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		if (Name.StartsWith(TEXT("/")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return C;
			}
			if (UBlueprint* B = Load(Name))
			{
				return B->GeneratedClass;
			}
			if (UClass* C = LoadObject<UClass>(nullptr, *(ObjectPath(Name) + TEXT("_C")), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return C;
			}
			return nullptr;
		}
		if (UClass* C = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst))
		{
			return C;
		}
		if (Name.Len() > 1 && (Name[0] == TEXT('A') || Name[0] == TEXT('U')) && FChar::IsUpper(Name[1]))
		{
			return FindFirstObject<UClass>(*Name.Mid(1), EFindFirstObjectOptions::NativeFirst);
		}
		return nullptr;
	}

	UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Name)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		const FString Wanted = Name.IsEmpty() ? TEXT("EventGraph") : Name;
		for (UEdGraph* G : Graphs)
		{
			if (G && G->GetName() == Wanted)
			{
				return G;
			}
		}
		return nullptr;
	}

	FString TypeString(const FEdGraphPinType& T)
	{
		FString S = T.PinCategory.ToString();
		if (T.PinSubCategoryObject.IsValid())
		{
			S = T.PinSubCategoryObject->GetName();
		}
		else if (!T.PinSubCategory.IsNone())
		{
			S = T.PinSubCategory.ToString();
		}
		if (T.ContainerType == EPinContainerType::Array) S += TEXT("[]");
		if (T.ContainerType == EPinContainerType::Set) S = TEXT("set<") + S + TEXT(">");
		if (T.ContainerType == EPinContainerType::Map) S = TEXT("map<") + S + TEXT(">");
		return S;
	}

	bool ParseType(const FString& InSpec, FEdGraphPinType& Out, FString& Error)
	{
		FString Spec = InSpec.TrimStartAndEnd();
		Out = FEdGraphPinType();
		if (Spec.EndsWith(TEXT("[]")))
		{
			Out.ContainerType = EPinContainerType::Array;
			Spec.LeftChopInline(2);
		}
		const FString L = Spec.ToLower();
		if (L == TEXT("bool")) { Out.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true; }
		if (L == TEXT("int")) { Out.PinCategory = UEdGraphSchema_K2::PC_Int; return true; }
		if (L == TEXT("int64")) { Out.PinCategory = UEdGraphSchema_K2::PC_Int64; return true; }
		if (L == TEXT("byte")) { Out.PinCategory = UEdGraphSchema_K2::PC_Byte; return true; }
		if (L == TEXT("float")) { Out.PinCategory = UEdGraphSchema_K2::PC_Real; Out.PinSubCategory = UEdGraphSchema_K2::PC_Float; return true; }
		if (L == TEXT("double")) { Out.PinCategory = UEdGraphSchema_K2::PC_Real; Out.PinSubCategory = UEdGraphSchema_K2::PC_Double; return true; }
		if (L == TEXT("string")) { Out.PinCategory = UEdGraphSchema_K2::PC_String; return true; }
		if (L == TEXT("name")) { Out.PinCategory = UEdGraphSchema_K2::PC_Name; return true; }
		if (L == TEXT("text")) { Out.PinCategory = UEdGraphSchema_K2::PC_Text; return true; }
		auto Struct = [&Out](UScriptStruct* S) { Out.PinCategory = UEdGraphSchema_K2::PC_Struct; Out.PinSubCategoryObject = S; return true; };
		if (L == TEXT("vector")) return Struct(TBaseStructure<FVector>::Get());
		if (L == TEXT("rotator")) return Struct(TBaseStructure<FRotator>::Get());
		if (L == TEXT("transform")) return Struct(TBaseStructure<FTransform>::Get());
		if (L == TEXT("color")) return Struct(TBaseStructure<FLinearColor>::Get());
		if (UScriptStruct* S = FindFirstObject<UScriptStruct>(*Spec, EFindFirstObjectOptions::NativeFirst))
		{
			return Struct(S);
		}
		if (UClass* C = ResolveClass(Spec))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Object;
			Out.PinSubCategoryObject = C;
			return true;
		}
		Error = FString::Printf(TEXT("unknown type '%s' (bool|int|int64|byte|float|double|string|name|text|vector|rotator|transform|color|<class>|<struct>, optional [])"), *InSpec);
		return false;
	}

	FString NodeId(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		return FString::Printf(TEXT("N%d"), Graph->Nodes.IndexOfByKey(Node) + 1);
	}

	TSharedPtr<FJsonObject> NodeJson(const UEdGraph* Graph, UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), NodeId(Graph, Node));
		O->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		O->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		O->SetArrayField(TEXT("position"), {Num(Node->NodePosX), Num(Node->NodePosY)});
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden)
			{
				continue;
			}
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Pin->PinName.ToString());
			P->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
			P->SetStringField(TEXT("type"), TypeString(Pin->PinType));
			if (!Pin->DefaultValue.IsEmpty() || Pin->DefaultObject || !Pin->DefaultTextValue.IsEmpty())
			{
				P->SetStringField(TEXT("default"), Pin->GetDefaultAsString());
			}
			TArray<FString> Links;
			for (UEdGraphPin* L : Pin->LinkedTo)
			{
				if (L && L->GetOwningNode())
				{
					Links.Add(NodeId(Graph, L->GetOwningNode()) + TEXT(".") + L->PinName.ToString());
				}
			}
			if (Links.Num() > 0)
			{
				P->SetArrayField(TEXT("linkedTo"), StrArray(Links));
			}
			Pins.Add(Obj(P));
		}
		O->SetArrayField(TEXT("pins"), Pins);
		return O;
	}

	UEdGraphNode* ResolveNode(UEdGraph* Graph, const FString& Ref)
	{
		if (Ref.Len() > 1 && (Ref[0] == TEXT('N') || Ref[0] == TEXT('n')) && Ref.Mid(1).IsNumeric())
		{
			const int32 Index = FCString::Atoi(*Ref.Mid(1)) - 1;
			return Graph->Nodes.IsValidIndex(Index) ? Graph->Nodes[Index] : nullptr;
		}
		FGuid Guid;
		if (FGuid::Parse(Ref, Guid))
		{
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (N && N->NodeGuid == Guid)
				{
					return N;
				}
			}
		}
		return nullptr;
	}

	UEdGraphPin* ResolvePin(UEdGraph* Graph, const FString& Ref, FString& Error)
	{
		FString NodeRef, PinName;
		if (!Ref.Split(TEXT("."), &NodeRef, &PinName))
		{
			Error = FString::Printf(TEXT("'%s' must be <node>.<pin>"), *Ref);
			return nullptr;
		}
		UEdGraphNode* Node = ResolveNode(Graph, NodeRef);
		if (!Node)
		{
			Error = FString::Printf(TEXT("no node '%s' in graph %s"), *NodeRef, *Graph->GetName());
			return nullptr;
		}
		TArray<FString> Names;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && (P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) || P->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				return P;
			}
			if (P && !P->bHidden)
			{
				Names.Add(P->PinName.ToString());
			}
		}
		Error = FString::Printf(TEXT("node %s has no pin '%s' (pins: %s)"), *NodeRef, *PinName, *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}

	UFunction* ResolveFunction(UBlueprint* Blueprint, const FString& Name, const FString& Parent)
	{
		const FName FName_(*Name);
		if (!Parent.IsEmpty())
		{
			UClass* C = ResolveClass(Parent);
			return C ? C->FindFunctionByName(FName_) : nullptr;
		}
		for (UClass* C : {Blueprint->SkeletonGeneratedClass.Get(), Blueprint->GeneratedClass.Get(), Blueprint->ParentClass.Get()})
		{
			if (C)
			{
				if (UFunction* F = C->FindFunctionByName(FName_))
				{
					return F;
				}
			}
		}
		for (const TCHAR* Lib : {TEXT("KismetSystemLibrary"), TEXT("KismetMathLibrary"), TEXT("KismetStringLibrary"), TEXT("GameplayStatics"), TEXT("KismetArrayLibrary")})
		{
			if (UClass* C = FindFirstObject<UClass>(Lib, EFindFirstObjectOptions::NativeFirst))
			{
				if (UFunction* F = C->FindFunctionByName(FName_))
				{
					return F;
				}
			}
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UBlueprintFunctionLibrary::StaticClass()) && !It->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				if (UFunction* F = It->FindFunctionByName(FName_))
				{
					return F;
				}
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> CompileJson(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const TSharedRef<FTokenizedMessage>& M : Results.Messages)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			const EMessageSeverity::Type Sev = M->GetSeverity();
			O->SetStringField(TEXT("severity"), Sev == EMessageSeverity::Error ? TEXT("error") : (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning ? TEXT("warning") : TEXT("info")));
			O->SetStringField(TEXT("text"), M->ToText().ToString());
			Messages.Add(Obj(O));
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const bool bOk = Blueprint->Status != BS_Error && Results.NumErrors == 0;
		O->SetStringField(TEXT("status"), bOk ? (Results.NumWarnings > 0 ? TEXT("warnings") : TEXT("ok")) : TEXT("error"));
		O->SetBoolField(TEXT("succeeded"), bOk);
		O->SetNumberField(TEXT("errors"), Results.NumErrors);
		O->SetNumberField(TEXT("warnings"), Results.NumWarnings);
		O->SetArrayField(TEXT("messages"), Messages);
		return O;
	}
}

using namespace UCli::BP;

namespace
{
	/** Loads args.path as a Blueprint or fails the call; returns nullptr on failure. */
	UBlueprint* RequireBP(const FToolCallRef& Call)
	{
		const FString Path = Call->GetString(TEXT("path"));
		UBlueprint* B = Load(Path);
		if (!B)
		{
			Call->Fail(TEXT("blueprint.not_found"), FString::Printf(TEXT("no Blueprint at '%s'"), *Path), TEXT("search: asset_search query=<name> class=Blueprint"));
		}
		return B;
	}

	UEdGraph* RequireGraph(const FToolCallRef& Call, UBlueprint* B)
	{
		const FString Name = Call->GetString(TEXT("graph"), TEXT("EventGraph"));
		UEdGraph* G = FindGraph(B, Name);
		if (!G)
		{
			TArray<UEdGraph*> Graphs;
			B->GetAllGraphs(Graphs);
			TArray<FString> Names;
			for (UEdGraph* X : Graphs)
			{
				Names.Add(X->GetName());
			}
			Call->Fail(TEXT("blueprint.graph_not_found"), FString::Printf(TEXT("no graph '%s' (graphs: %s)"), *Name, *FString::Join(Names, TEXT(", "))));
		}
		return G;
	}

	void Touch(UBlueprint* B, bool bStructural)
	{
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(B);
		}
	}

	TSharedPtr<FJsonObject> GraphJson(UEdGraph* G)
	{
		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				Nodes.Add(Obj(NodeJson(G, N)));
			}
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), G->GetName());
		O->SetArrayField(TEXT("nodes"), Nodes);
		return O;
	}

	template <typename T>
	T* Place(UEdGraph* G, const FToolCallRef& Call, TFunctionRef<void(T*)> Setup)
	{
		FGraphNodeCreator<T> Creator(*G);
		T* Node = Creator.CreateNode(false);
		Setup(Node);
		const TArray<TSharedPtr<FJsonValue>> Pos = Call->GetArray(TEXT("position"));
		if (Pos.Num() == 2)
		{
			Node->NodePosX = static_cast<int32>(Pos[0]->AsNumber());
			Node->NodePosY = static_cast<int32>(Pos[1]->AsNumber());
		}
		else
		{
			int32 MaxY = 0;
			for (UEdGraphNode* N : G->Nodes)
			{
				if (N && N != Node) MaxY = FMath::Max(MaxY, N->NodePosY + 200);
			}
			Node->NodePosY = MaxY;
		}
		Creator.Finalize();
		return Node;
	}

	TSharedPtr<FJsonObject> FunctionJson(UFunction* F, const TCHAR* Kind)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), F->GetName());
		O->SetStringField(TEXT("kind"), Kind);
		O->SetStringField(TEXT("class"), F->GetOwnerClass() ? F->GetOwnerClass()->GetName() : FString());
		O->SetBoolField(TEXT("pure"), F->HasAnyFunctionFlags(FUNC_BlueprintPure));
		O->SetBoolField(TEXT("static"), F->HasAnyFunctionFlags(FUNC_Static));
		O->SetStringField(TEXT("tooltip"), F->GetToolTipText().ToString().Left(200));
		TArray<TSharedPtr<FJsonValue>> Params;
		for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), It->GetName());
			P->SetStringField(TEXT("type"), It->GetCPPType());
			P->SetBoolField(TEXT("out"), It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm));
			P->SetBoolField(TEXT("return"), It->HasAnyPropertyFlags(CPF_ReturnParm));
			Params.Add(Obj(P));
		}
		O->SetArrayField(TEXT("params"), Params);
		return O;
	}

	const TCHAR* StandardMacros = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros");
}

void UCli::RegisterBlueprintTools(FToolRegistry& R)
{
	R.Add(TEXT("bp_create"), TEXT("blueprint"), TEXT("Create a Blueprint asset (parent: class name, /Script path or Blueprint path)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"parent":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString Path = Call->GetString(TEXT("path"));
			FText Reason;
			if (!FPackageName::IsValidLongPackageName(Path, false, &Reason))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' is not a valid package path: %s"), *Path, *Reason.ToString()), TEXT("use a path like /Game/Blueprints/BP_Name"));
				return;
			}
			if (Load(Path) || FPackageName::DoesPackageExist(Path))
			{
				Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("'%s' already exists"), *Path));
				return;
			}
			UClass* Parent = ResolveClass(Call->GetString(TEXT("parent"), TEXT("Actor")));
			if (!Parent || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))
			{
				Call->Fail(TEXT("blueprint.bad_parent"), FString::Printf(TEXT("cannot create a Blueprint of '%s'"), *Call->GetString(TEXT("parent"), TEXT("Actor"))));
				return;
			}
			UPackage* Package = CreatePackage(*Path);
			UBlueprint* B = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*FPackageName::GetShortName(Path)), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			FAssetRegistryModule::AssetCreated(B);
			Package->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Path);
			O->SetStringField(TEXT("parent"), Parent->GetPathName());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_get"), TEXT("blueprint"), TEXT("Variables, graphs, components and interfaces of a Blueprint."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), B->GetPathName());
			O->SetStringField(TEXT("parent"), B->ParentClass ? B->ParentClass->GetPathName() : FString());
			TArray<TSharedPtr<FJsonValue>> Vars;
			for (const FBPVariableDescription& V : B->NewVariables)
			{
				TSharedPtr<FJsonObject> VO = MakeShared<FJsonObject>();
				VO->SetStringField(TEXT("name"), V.VarName.ToString());
				VO->SetStringField(TEXT("type"), TypeString(V.VarType));
				VO->SetStringField(TEXT("category"), V.Category.ToString());
				VO->SetStringField(TEXT("default"), V.DefaultValue);
				VO->SetBoolField(TEXT("instanceEditable"), (V.PropertyFlags & CPF_DisableEditOnInstance) == 0);
				Vars.Add(Obj(VO));
			}
			O->SetArrayField(TEXT("variables"), Vars);
			TArray<TSharedPtr<FJsonValue>> Graphs;
			auto AddGraphs = [&Graphs](const TArray<TObjectPtr<UEdGraph>>& List, const TCHAR* Type)
			{
				for (UEdGraph* G : List)
				{
					if (!G) continue;
					TSharedPtr<FJsonObject> GO = MakeShared<FJsonObject>();
					GO->SetStringField(TEXT("name"), G->GetName());
					GO->SetStringField(TEXT("type"), Type);
					GO->SetNumberField(TEXT("nodes"), G->Nodes.Num());
					Graphs.Add(Obj(GO));
				}
			};
			AddGraphs(B->UbergraphPages, TEXT("event"));
			AddGraphs(B->FunctionGraphs, TEXT("function"));
			AddGraphs(B->MacroGraphs, TEXT("macro"));
			O->SetArrayField(TEXT("graphs"), Graphs);
			TArray<FString> Components;
			if (B->SimpleConstructionScript)
			{
				for (USCS_Node* N : B->SimpleConstructionScript->GetAllNodes())
				{
					Components.Add(FString::Printf(TEXT("%s (%s)"), *N->GetVariableName().ToString(), N->ComponentClass ? *N->ComponentClass->GetName() : TEXT("?")));
				}
			}
			O->SetArrayField(TEXT("components"), StrArray(Components));
			TArray<FString> Interfaces;
			for (const FBPInterfaceDescription& I : B->ImplementedInterfaces)
			{
				Interfaces.Add(I.Interface ? I.Interface->GetName() : FString());
			}
			O->SetArrayField(TEXT("interfaces"), StrArray(Interfaces));
			O->SetStringField(TEXT("status"), B->Status == BS_Error ? TEXT("error") : (B->Status == BS_Dirty ? TEXT("dirty") : TEXT("ok")));
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_graph_get"), TEXT("blueprint"), TEXT("Nodes, pins and links of one graph. Node ids N<k> are stable until nodes are removed."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			Call->Succeed(GraphJson(G));
		});

	R.Add(TEXT("bp_add_node"), TEXT("blueprint"), TEXT("Add a node: Event | CustomEvent | CallFunction | VariableGet | VariableSet | Branch | Sequence | Self | Cast | Macro."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"type":{"type":"string","enum":["Event","CustomEvent","CallFunction","VariableGet","VariableSet","Branch","Sequence","Self","Cast","Macro"]},"memberName":{"type":"string"},"memberParent":{"type":"string"},"position":{"type":"array","items":{"type":"number"}}},"required":["path","type"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			B->Modify();
			G->Modify();
			const FString Type = Call->GetString(TEXT("type"));
			const FString Member = Call->GetString(TEXT("memberName"));
			const FString Parent = Call->GetString(TEXT("memberParent"));
			auto NeedMember = [&]() -> bool
			{
				if (Member.IsEmpty())
				{
					Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("type %s needs memberName"), *Type));
					return false;
				}
				return true;
			};
			UEdGraphNode* Node = nullptr;
			if (Type == TEXT("Event"))
			{
				if (!NeedMember()) return;
				UClass* Owner = Parent.IsEmpty() ? B->ParentClass.Get() : ResolveClass(Parent);
				UFunction* F = nullptr;
				for (const FString& Candidate : {Member, TEXT("Receive") + Member})
				{
					if (!F && Owner) F = Owner->FindFunctionByName(*Candidate);
				}
				if (!F || !F->HasAnyFunctionFlags(FUNC_BlueprintEvent))
				{
					Call->Fail(TEXT("blueprint.event_not_found"), FString::Printf(TEXT("'%s' is not an overridable event of %s"), *Member, Owner ? *Owner->GetName() : TEXT("?")), TEXT("find events: bp_search_nodes kind=event"));
					return;
				}
				UClass* FuncOwner = F->GetOwnerClass();
				if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(B, FuncOwner, F->GetFName()))
				{
					TSharedPtr<FJsonObject> O = NodeJson(Existing->GetGraph(), Existing);
					O->SetBoolField(TEXT("existing"), true);
					Call->Succeed(O);
					return;
				}
				Node = Place<UK2Node_Event>(G, Call, [F, FuncOwner](UK2Node_Event* N)
				{
					N->EventReference.SetExternalMember(F->GetFName(), FuncOwner);
					N->bOverrideFunction = true;
				});
			}
			else if (Type == TEXT("CustomEvent"))
			{
				if (!NeedMember()) return;
				Node = Place<UK2Node_CustomEvent>(G, Call, [&](UK2Node_CustomEvent* N) { N->CustomFunctionName = FName(*Member); });
			}
			else if (Type == TEXT("CallFunction"))
			{
				if (!NeedMember()) return;
				UFunction* F = ResolveFunction(B, Member, Parent);
				if (!F)
				{
					Call->Fail(TEXT("blueprint.function_not_found"), FString::Printf(TEXT("no callable function '%s'"), *Member), TEXT("find it: bp_search_nodes query=<name>"));
					return;
				}
				Node = Place<UK2Node_CallFunction>(G, Call, [F](UK2Node_CallFunction* N) { N->SetFromFunction(F); });
			}
			else if (Type == TEXT("VariableGet") || Type == TEXT("VariableSet"))
			{
				if (!NeedMember()) return;
				const bool bExists = FBlueprintEditorUtils::FindNewVariableIndex(B, *Member) != INDEX_NONE
					|| (B->ParentClass && B->ParentClass->FindPropertyByName(*Member));
				if (!bExists)
				{
					Call->Fail(TEXT("blueprint.variable_not_found"), FString::Printf(TEXT("no variable '%s'"), *Member), TEXT("add it: bp_add_variable"));
					return;
				}
				if (Type == TEXT("VariableGet"))
					Node = Place<UK2Node_VariableGet>(G, Call, [&](UK2Node_VariableGet* N) { N->VariableReference.SetSelfMember(*Member); });
				else
					Node = Place<UK2Node_VariableSet>(G, Call, [&](UK2Node_VariableSet* N) { N->VariableReference.SetSelfMember(*Member); });
			}
			else if (Type == TEXT("Branch"))
			{
				Node = Place<UK2Node_IfThenElse>(G, Call, [](UK2Node_IfThenElse*) {});
			}
			else if (Type == TEXT("Sequence"))
			{
				Node = Place<UK2Node_ExecutionSequence>(G, Call, [](UK2Node_ExecutionSequence*) {});
			}
			else if (Type == TEXT("Self"))
			{
				Node = Place<UK2Node_Self>(G, Call, [](UK2Node_Self*) {});
			}
			else if (Type == TEXT("Cast"))
			{
				if (!NeedMember()) return;
				UClass* Target = ResolveClass(Member);
				if (!Target)
				{
					Call->Fail(TEXT("blueprint.class_not_found"), FString::Printf(TEXT("no class '%s'"), *Member));
					return;
				}
				Node = Place<UK2Node_DynamicCast>(G, Call, [Target](UK2Node_DynamicCast* N) { N->TargetType = Target; });
			}
			else if (Type == TEXT("Macro"))
			{
				if (!NeedMember()) return;
				UBlueprint* Lib = LoadObject<UBlueprint>(nullptr, Parent.IsEmpty() ? StandardMacros : *ObjectPath(Parent));
				UEdGraph* Macro = Lib ? FindGraph(Lib, Member) : nullptr;
				if (!Macro)
				{
					Call->Fail(TEXT("blueprint.macro_not_found"), FString::Printf(TEXT("no macro '%s'"), *Member), TEXT("standard macros: ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate, FlipFlop, IsValid"));
					return;
				}
				Node = Place<UK2Node_MacroInstance>(G, Call, [Macro](UK2Node_MacroInstance* N) { N->SetMacroGraph(Macro); });
			}
			Touch(B, true);
			Call->Succeed(NodeJson(G, Node));
		});

	R.Add(TEXT("bp_remove_node"), TEXT("blueprint"), TEXT("Remove a node by N<k> id or guid."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"node":{"type":"string"}},"required":["path","node"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			UEdGraphNode* N = ResolveNode(G, Call->GetString(TEXT("node")));
			if (!N)
			{
				Call->Fail(TEXT("blueprint.node_not_found"), FString::Printf(TEXT("no node '%s'"), *Call->GetString(TEXT("node"))));
				return;
			}
			B->Modify();
			G->Modify();
			const FString Guid = N->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FBlueprintEditorUtils::RemoveNode(B, N, true);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("removed"), Guid);
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_connect"), TEXT("blueprint"), TEXT("Connect pins: from/to are '<node>.<pin>' (node = N<k> or guid)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"from":{"type":"string"},"to":{"type":"string"}},"required":["path","from","to"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			FString Error;
			UEdGraphPin* A = ResolvePin(G, Call->GetString(TEXT("from")), Error);
			UEdGraphPin* C = A ? ResolvePin(G, Call->GetString(TEXT("to")), Error) : nullptr;
			if (!A || !C)
			{
				Call->Fail(TEXT("blueprint.pin_not_found"), Error);
				return;
			}
			const UEdGraphSchema* Schema = G->GetSchema();
			const FPinConnectionResponse Resp = Schema->CanCreateConnection(A, C);
			if (Resp.Response == CONNECT_RESPONSE_DISALLOW)
			{
				Call->Fail(TEXT("blueprint.connect_refused"), Resp.Message.ToString());
				return;
			}
			B->Modify();
			G->Modify();
			A->GetOwningNode()->Modify();
			C->GetOwningNode()->Modify();
			if (!Schema->TryCreateConnection(A, C))
			{
				Call->Fail(TEXT("blueprint.connect_refused"), TEXT("the schema refused the connection"));
				return;
			}
			Touch(B, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("connected"), true);
			O->SetStringField(TEXT("note"), Resp.Message.ToString());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_disconnect"), TEXT("blueprint"), TEXT("Break one link (from+to) or every link of a pin (from only)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"from":{"type":"string"},"to":{"type":"string"}},"required":["path","from"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			FString Error;
			UEdGraphPin* A = ResolvePin(G, Call->GetString(TEXT("from")), Error);
			if (!A)
			{
				Call->Fail(TEXT("blueprint.pin_not_found"), Error);
				return;
			}
			B->Modify();
			A->GetOwningNode()->Modify();
			if (Call->Has(TEXT("to")))
			{
				UEdGraphPin* C = ResolvePin(G, Call->GetString(TEXT("to")), Error);
				if (!C)
				{
					Call->Fail(TEXT("blueprint.pin_not_found"), Error);
					return;
				}
				G->GetSchema()->BreakSinglePinLink(A, C);
			}
			else
			{
				G->GetSchema()->BreakPinLinks(*A, true);
			}
			Touch(B, false);
			Call->Succeed(MakeShared<FJsonObject>());
		});

	R.Add(TEXT("bp_set_pin"), TEXT("blueprint"), TEXT("Set an input pin's literal default value."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"node":{"type":"string"},"pin":{"type":"string"},"value":{"type":"string"}},"required":["path","node","pin","value"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = RequireGraph(Call, B);
			if (!G) return;
			FString Error;
			UEdGraphPin* P = ResolvePin(G, Call->GetString(TEXT("node")) + TEXT(".") + Call->GetString(TEXT("pin")), Error);
			if (!P)
			{
				Call->Fail(TEXT("blueprint.pin_not_found"), Error);
				return;
			}
			if (P->Direction != EGPD_Input)
			{
				Call->Fail(TEXT("blueprint.pin_not_input"), TEXT("only input pins take a default value"));
				return;
			}
			B->Modify();
			P->GetOwningNode()->Modify();
			const UEdGraphSchema_K2* K2 = Cast<UEdGraphSchema_K2>(G->GetSchema());
			const FString Value = Call->GetString(TEXT("value"));
			if (K2)
			{
				const FString Invalid = K2->IsPinDefaultValid(P, Value, nullptr, FText::GetEmpty());
				if (!Invalid.IsEmpty())
				{
					Call->Fail(TEXT("blueprint.bad_value"), Invalid);
					return;
				}
			}
			G->GetSchema()->TrySetDefaultValue(*P, Value);
			Touch(B, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("value"), P->GetDefaultAsString());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_add_variable"), TEXT("blueprint"), TEXT("Add a member variable. type: bool|int|int64|byte|float|double|string|name|text|vector|rotator|transform|color|<class>|<struct>, optional []."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"type":{"type":"string"},"defaultValue":{"type":"string"},"instanceEditable":{"type":"boolean"},"category":{"type":"string"}},"required":["path","name","type"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			FEdGraphPinType Type;
			FString Error;
			if (!ParseType(Call->GetString(TEXT("type")), Type, Error))
			{
				Call->Fail(TEXT("args.invalid"), Error);
				return;
			}
			const FName Name(*Call->GetString(TEXT("name")));
			if (FBlueprintEditorUtils::FindNewVariableIndex(B, Name) != INDEX_NONE)
			{
				Call->Fail(TEXT("blueprint.variable_exists"), FString::Printf(TEXT("variable '%s' already exists"), *Name.ToString()));
				return;
			}
			B->Modify();
			if (!FBlueprintEditorUtils::AddMemberVariable(B, Name, Type, Call->GetString(TEXT("defaultValue"))))
			{
				Call->Fail(TEXT("blueprint.variable_failed"), TEXT("the name is invalid or already used"));
				return;
			}
			if (Call->Has(TEXT("instanceEditable")))
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(B, Name, !Call->GetBool(TEXT("instanceEditable")));
			}
			if (Call->Has(TEXT("category")))
			{
				FBlueprintEditorUtils::SetBlueprintVariableCategory(B, Name, nullptr, FText::FromString(Call->GetString(TEXT("category"))));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Name.ToString());
			O->SetStringField(TEXT("type"), TypeString(Type));
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_remove_variable"), TEXT("blueprint"), TEXT("Remove a member variable and its nodes."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"}},"required":["path","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			const FName Name(*Call->GetString(TEXT("name")));
			if (FBlueprintEditorUtils::FindNewVariableIndex(B, Name) == INDEX_NONE)
			{
				Call->Fail(TEXT("blueprint.variable_not_found"), FString::Printf(TEXT("no variable '%s'"), *Name.ToString()));
				return;
			}
			B->Modify();
			FBlueprintEditorUtils::RemoveMemberVariable(B, Name);
			Call->Succeed(MakeShared<FJsonObject>());
		});

	R.Add(TEXT("bp_add_function"), TEXT("blueprint"), TEXT("Add a function graph with optional inputs/outputs [{name,type}]."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"inputs":{"type":"array"},"outputs":{"type":"array"},"pure":{"type":"boolean"}},"required":["path","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			const FName Name(*Call->GetString(TEXT("name")));
			if (FindGraph(B, Name.ToString()))
			{
				Call->Fail(TEXT("blueprint.graph_exists"), FString::Printf(TEXT("graph '%s' already exists"), *Name.ToString()));
				return;
			}
			B->Modify();
			UEdGraph* G = FBlueprintEditorUtils::CreateNewGraph(B, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(B, G, true, nullptr);
			TArray<UK2Node_FunctionEntry*> Entries;
			G->GetNodesOfClass(Entries);
			UK2Node_FunctionEntry* Entry = Entries.Num() > 0 ? Entries[0] : nullptr;
			FString Error;
			if (Entry)
			{
				if (Call->GetBool(TEXT("pure")))
				{
					Entry->AddExtraFlags(FUNC_BlueprintPure);
				}
				for (const TSharedPtr<FJsonValue>& V : Call->GetArray(TEXT("inputs")))
				{
					FEdGraphPinType T;
					if (!ParseType(V->AsObject()->GetStringField(TEXT("type")), T, Error))
					{
						Call->Fail(TEXT("args.invalid"), Error);
						return;
					}
					Entry->CreateUserDefinedPin(*V->AsObject()->GetStringField(TEXT("name")), T, EGPD_Output);
				}
			}
			const TArray<TSharedPtr<FJsonValue>> Outputs = Call->GetArray(TEXT("outputs"));
			if (Outputs.Num() > 0)
			{
				FGraphNodeCreator<UK2Node_FunctionResult> Creator(*G);
				UK2Node_FunctionResult* Result = Creator.CreateNode(false);
				Result->NodePosX = 600;
				Creator.Finalize();
				for (const TSharedPtr<FJsonValue>& V : Outputs)
				{
					FEdGraphPinType T;
					if (!ParseType(V->AsObject()->GetStringField(TEXT("type")), T, Error))
					{
						Call->Fail(TEXT("args.invalid"), Error);
						return;
					}
					Result->CreateUserDefinedPin(*V->AsObject()->GetStringField(TEXT("name")), T, EGPD_Input);
				}
			}
			Touch(B, true);
			Call->Succeed(GraphJson(G));
		});

	R.Add(TEXT("bp_add_component"), TEXT("blueprint"), TEXT("Add a component to the Blueprint's construction script (optionally under a parent component)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"class":{"type":"string"},"name":{"type":"string"},"parent":{"type":"string"}},"required":["path","class"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			if (!B->SimpleConstructionScript)
			{
				Call->Fail(TEXT("blueprint.no_components"), TEXT("this Blueprint type has no components"));
				return;
			}
			UClass* C = ResolveClass(Call->GetString(TEXT("class")));
			if (!C || !C->IsChildOf(UActorComponent::StaticClass()))
			{
				Call->Fail(TEXT("blueprint.class_not_found"), FString::Printf(TEXT("'%s' is not a component class"), *Call->GetString(TEXT("class"))));
				return;
			}
			USimpleConstructionScript* SCS = B->SimpleConstructionScript;
			SCS->Modify();
			const FString Name = Call->GetString(TEXT("name"));
			USCS_Node* Node = SCS->CreateNode(C, Name.IsEmpty() ? NAME_None : FName(*Name));
			if (Call->Has(TEXT("parent")))
			{
				USCS_Node* Parent = SCS->FindSCSNode(FName(*Call->GetString(TEXT("parent"))));
				if (!Parent)
				{
					Call->Fail(TEXT("blueprint.component_not_found"), FString::Printf(TEXT("no component '%s'"), *Call->GetString(TEXT("parent"))));
					return;
				}
				Parent->AddChildNode(Node);
			}
			else
			{
				SCS->AddNode(Node);
			}
			Touch(B, true);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			O->SetStringField(TEXT("class"), C->GetName());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_compile"), TEXT("blueprint"), TEXT("Compile and return status, error/warning counts and messages."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			Call->Succeed(CompileJson(B));
		});

	R.Add(TEXT("bp_save"), TEXT("blueprint"), TEXT("Save the Blueprint package. Refused when it does not compile unless force=true."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"force":{"type":"boolean"}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			if (!Call->GetBool(TEXT("force")))
			{
				const TSharedPtr<FJsonObject> C = CompileJson(B);
				if (!C->GetBoolField(TEXT("succeeded")))
				{
					Call->Fail(TEXT("blueprint.compile_errors"), FString::Printf(TEXT("%d compile error(s); not saved"), static_cast<int32>(C->GetNumberField(TEXT("errors")))),
						TEXT("fix the errors or pass force=true"), Obj(C));
					return;
				}
			}
			if (!UEditorLoadingAndSavingUtils::SavePackages({B->GetOutermost()}, false))
			{
				Call->Fail(TEXT("asset.save_failed"), TEXT("the package could not be saved"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("saved"), B->GetOutermost()->GetName());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_search_nodes"), TEXT("blueprint"), TEXT("Find callable functions / overridable events / macros by name for bp_add_node."),
		TEXT(R"({"type":"object","properties":{"query":{"type":"string"},"forClass":{"type":"string"},"kind":{"type":"string","enum":["function","event","macro","all"]},"limit":{"type":"integer"}}})"),
		EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FString Query = Call->GetString(TEXT("query"));
			const FString Kind = Call->GetString(TEXT("kind"), TEXT("all"));
			const int32 Limit = FMath::Max(1, static_cast<int32>(Call->GetNumber(TEXT("limit"), 50)));
			TArray<TSharedPtr<FJsonValue>> Hits;
			TSet<FString> Seen;
			auto Consider = [&](UClass* C)
			{
				for (TFieldIterator<UFunction> It(C, EFieldIteratorFlags::IncludeSuper); It && Hits.Num() < Limit; ++It)
				{
					UFunction* F = *It;
					const bool bEvent = F->HasAnyFunctionFlags(FUNC_BlueprintEvent) && !F->HasAnyFunctionFlags(FUNC_Static);
					const bool bCallable = F->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure);
					if ((bEvent && Kind != TEXT("function")) || (bCallable && !bEvent && Kind != TEXT("event")))
					{
						const FString Key = F->GetPathName();
						if ((Query.IsEmpty() || F->GetName().Contains(Query)) && !Seen.Contains(Key) && Kind != TEXT("macro"))
						{
							Seen.Add(Key);
							Hits.Add(Obj(FunctionJson(F, bEvent ? TEXT("event") : TEXT("function"))));
						}
					}
				}
			};
			if (Call->Has(TEXT("forClass")))
			{
				UClass* C = ResolveClass(Call->GetString(TEXT("forClass")));
				if (!C)
				{
					Call->Fail(TEXT("blueprint.class_not_found"), FString::Printf(TEXT("no class '%s'"), *Call->GetString(TEXT("forClass"))));
					return;
				}
				Consider(C);
			}
			else
			{
				Consider(AActor::StaticClass());
				for (TObjectIterator<UClass> It; It && Hits.Num() < Limit; ++It)
				{
					if (It->IsChildOf(UBlueprintFunctionLibrary::StaticClass()) && !It->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract) && It->IsNative())
					{
						Consider(*It);
					}
				}
			}
			if ((Kind == TEXT("macro") || Kind == TEXT("all")) && Hits.Num() < Limit)
			{
				if (UBlueprint* Lib = LoadObject<UBlueprint>(nullptr, StandardMacros))
				{
					for (UEdGraph* G : Lib->MacroGraphs)
					{
						if (G && (Query.IsEmpty() || G->GetName().Contains(Query)) && Hits.Num() < Limit)
						{
							TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
							O->SetStringField(TEXT("name"), G->GetName());
							O->SetStringField(TEXT("kind"), TEXT("macro"));
							O->SetStringField(TEXT("class"), StandardMacros);
							Hits.Add(Obj(O));
						}
					}
				}
			}
			Call->Succeed(Arr(Hits));
		});

	R.Add(TEXT("bp_reparent"), TEXT("blueprint"), TEXT("Change a Blueprint's parent class and recompile."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"parent":{"type":"string"}},"required":["path","parent"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UClass* Parent = ResolveClass(Call->GetString(TEXT("parent")));
			if (!Parent || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))
			{
				Call->Fail(TEXT("blueprint.bad_parent"), FString::Printf(TEXT("cannot reparent to '%s'"), *Call->GetString(TEXT("parent"))));
				return;
			}
			B->Modify();
			B->ParentClass = Parent;
			FBlueprintEditorUtils::RefreshAllNodes(B);
			Touch(B, true);
			Call->Succeed(CompileJson(B));
		});

	R.Add(TEXT("bp_implement_interface"), TEXT("blueprint"), TEXT("Add a Blueprint interface (class name or path) to the Blueprint."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"interface":{"type":"string"}},"required":["path","interface"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UClass* I = ResolveClass(Call->GetString(TEXT("interface")));
			if (!I || !I->HasAnyClassFlags(CLASS_Interface))
			{
				Call->Fail(TEXT("blueprint.class_not_found"), FString::Printf(TEXT("'%s' is not an interface"), *Call->GetString(TEXT("interface"))));
				return;
			}
			B->Modify();
			if (!FBlueprintEditorUtils::ImplementNewInterface(B, I->GetClassPathName()))
			{
				Call->Fail(TEXT("blueprint.interface_failed"), TEXT("interface could not be added (already implemented?)"));
				return;
			}
			Touch(B, true);
			Call->Succeed(MakeShared<FJsonObject>());
		});
}
