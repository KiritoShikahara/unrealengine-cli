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
#include "Net/UnrealNetwork.h"
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
#include "K2Node_AddDelegate.h"
#include "K2Node_Message.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_Timeline.h"
#include "Curves/CurveFloat.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
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
		// Probing a path that does not exist with LoadObject leaves an empty package behind; a Blueprint created in
		// it later can be garbage collected with it. Only load what exists on disk or in memory.
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath(Path));
		if (!FindPackage(nullptr, *PackageName) && !FPackageName::DoesPackageExist(PackageName))
		{
			return nullptr;
		}
		if (UBlueprint* B = LoadObject<UBlueprint>(nullptr, *ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return B;
		}
		// A map path means its Level Blueprint (created on first use).
		UWorld* World = LoadObject<UWorld>(nullptr, *ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
		return World && World->PersistentLevel ? static_cast<UBlueprint*>(World->PersistentLevel->GetLevelScriptBlueprint(false)) : nullptr;
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
		if (T.ContainerType == EPinContainerType::Map)
		{
			FString V = T.PinValueType.TerminalCategory.ToString();
			if (T.PinValueType.TerminalSubCategoryObject.IsValid()) V = T.PinValueType.TerminalSubCategoryObject->GetName();
			else if (!T.PinValueType.TerminalSubCategory.IsNone()) V = T.PinValueType.TerminalSubCategory.ToString();
			S = TEXT("map<") + S + TEXT(",") + V + TEXT(">");
		}
		return S;
	}

	bool ParseBaseType(const FString& InSpec, const FString& Spec, FEdGraphPinType& Out, FString& Error);

	bool ParseType(const FString& InSpec, FEdGraphPinType& Out, FString& Error)
	{
		FString Spec = InSpec.TrimStartAndEnd();
		Out = FEdGraphPinType();
		const FString Lower = Spec.ToLower();
		if ((Lower.StartsWith(TEXT("map<")) || Lower.StartsWith(TEXT("set<"))) && Spec.EndsWith(TEXT(">")))
		{
			const bool bMap = Lower.StartsWith(TEXT("map<"));
			const FString Inner = Spec.Mid(4, Spec.Len() - 5);
			FString Key = Inner, Value;
			if (bMap && !Inner.Split(TEXT(","), &Key, &Value))
			{
				Error = FString::Printf(TEXT("map type needs a key and a value, e.g. map<string,int>: '%s'"), *InSpec);
				return false;
			}
			if (!ParseBaseType(InSpec, Key.TrimStartAndEnd(), Out, Error))
			{
				return false;
			}
			Out.ContainerType = bMap ? EPinContainerType::Map : EPinContainerType::Set;
			if (bMap)
			{
				FEdGraphPinType V;
				if (!ParseBaseType(InSpec, Value.TrimStartAndEnd(), V, Error))
				{
					return false;
				}
				Out.PinValueType = FEdGraphTerminalType::FromPinType(V);
			}
			return true;
		}
		if (Spec.EndsWith(TEXT("[]")))
		{
			Spec.LeftChopInline(2);
			if (!ParseBaseType(InSpec, Spec.TrimStartAndEnd(), Out, Error))
			{
				return false;
			}
			Out.ContainerType = EPinContainerType::Array;
			return true;
		}
		return ParseBaseType(InSpec, Spec, Out, Error);
	}

	bool ParseBaseType(const FString& InSpec, const FString& Spec, FEdGraphPinType& Out, FString& Error)
	{
		Out = FEdGraphPinType();
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
		Error = FString::Printf(TEXT("unknown type '%s' (bool|int|int64|byte|float|double|string|name|text|vector|rotator|transform|color|<class>|<struct>, optional [] or map<K,V> / set<T>)"), *InSpec);
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

	// Direction picks between same-named pins (a variable Set node has input and output pins both named after the variable).
	UEdGraphPin* ResolvePin(UEdGraph* Graph, const FString& Ref, FString& Error, EEdGraphPinDirection Direction)
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
		auto Matches = [&PinName](const UEdGraphPin* P)
		{
			return P && (P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) || P->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase));
		};
		// Exact pin names on visible pins win over display names (a library call's hidden self pin is displayed as "Target").
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && !P->bHidden && P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) && (Direction == EGPD_MAX || P->Direction == Direction)) return P;
		}
		if (Direction != EGPD_MAX)
		{
			for (UEdGraphPin* P : Node->Pins)
			{
				if (Matches(P) && P->Direction == Direction) return P;
			}
			// A Set node's output is the pin "Output_Get"; accept the variable name for it.
			if (Direction == EGPD_Output && Node->IsA<UK2Node_VariableSet>() && Matches(Node->FindPin(PinName, EGPD_Input)))
			{
				if (UEdGraphPin* Out = Node->FindPin(TEXT("Output_Get"), EGPD_Output)) return Out;
			}
		}
		TArray<FString> Names;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (Matches(P))
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
			// Diagnostics for intermittent misses: is the package in memory, and what does it hold?
			TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
			const FString PackageName = Path.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Path) : Path;
			UPackage* Pkg = FindPackage(nullptr, *PackageName);
			D->SetBoolField(TEXT("packageInMemory"), Pkg != nullptr);
			D->SetBoolField(TEXT("packageOnDisk"), FPackageName::DoesPackageExist(PackageName));
			if (UObject* Found = Pkg ? StaticFindObject(UObject::StaticClass(), Pkg, *FPackageName::GetShortName(PackageName)) : nullptr)
			{
				D->SetStringField(TEXT("object"), Found->GetClass()->GetName());
				D->SetBoolField(TEXT("garbage"), !IsValid(Found));
			}
			Call->Fail(TEXT("blueprint.not_found"), FString::Printf(TEXT("no Blueprint or map at '%s'"), *Path), TEXT("search: asset_search query=<name> class=Blueprint (a map path edits its Level Blueprint)"), Obj(D));
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

	// Places a node of any graph-node class (for node classes whose headers are private or optional).
	// bPinsFirst: some nodes (SpawnActorFromClass) read their pins in PostPlacedNewNode.
	UEdGraphNode* PlaceClass(UEdGraph* G, UClass* C, const FToolCallRef& Call, TFunctionRef<void(UEdGraphNode*)> Setup, bool bPinsFirst = false)
	{
		UEdGraphNode* Node = NewObject<UEdGraphNode>(G, C, NAME_None, RF_Transactional);
		G->AddNode(Node, false, false);
		Node->CreateNewGuid();
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
		if (bPinsFirst)
		{
			Node->AllocateDefaultPins();
			Node->PostPlacedNewNode();
		}
		else
		{
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
		}
		return Node;
	}

	UClass* ResolveNodeClass(const FString& Name)
	{
		for (const FString& Candidate : {Name, TEXT("K2Node_") + Name})
		{
			UClass* C = Candidate.StartsWith(TEXT("/")) ? LoadObject<UClass>(nullptr, *Candidate) : FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
			if (C && C->IsChildOf(UEdGraphNode::StaticClass()) && !C->HasAnyClassFlags(CLASS_Abstract)) return C;
		}
		return nullptr;
	}

	UScriptStruct* ResolveStruct(const FString& Name)
	{
		if (Name.StartsWith(TEXT("/"))) return LoadObject<UScriptStruct>(nullptr, *ObjectPath(Name));
		UScriptStruct* S = FindFirstObject<UScriptStruct>(*Name, EFindFirstObjectOptions::NativeFirst);
		return S ? S : FindFirstObject<UScriptStruct>(*Name.RightChop(Name.StartsWith(TEXT("F")) ? 1 : 0), EFindFirstObjectOptions::NativeFirst);
	}

	UEnum* ResolveEnum(const FString& Name)
	{
		return Name.StartsWith(TEXT("/")) ? LoadObject<UEnum>(nullptr, *ObjectPath(Name)) : FindFirstObject<UEnum>(*Name, EFindFirstObjectOptions::NativeFirst);
	}

	// Class whose properties a Blueprint graph sees: the skeleton class has members added since the last compile.
	UClass* GraphClass(UBlueprint* B)
	{
		return B->SkeletonGeneratedClass ? B->SkeletonGeneratedClass.Get() : B->GeneratedClass.Get();
	}

	void SetClassPin(UEdGraphNode* Node, UClass* Cls)
	{
		if (UEdGraphPin* Pin = Node->FindPin(TEXT("Class")))
		{
			Pin->DefaultObject = Cls;
			Node->PinDefaultValueChanged(Pin);
		}
	}
}

void UCli::RegisterBlueprintTools(FToolRegistry& R)
{
	R.Add(TEXT("bp_create"), TEXT("blueprint"), TEXT("Create a Blueprint asset (parent: class name, /Script path or Blueprint path). kind=interface creates a Blueprint Interface (add its functions with bp_add_function)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"parent":{"type":"string"},"kind":{"type":"string","enum":["class","interface"]}},"required":["path"]})"),
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
			const bool bInterface = Call->GetString(TEXT("kind")) == TEXT("interface");
			UClass* Parent = bInterface ? UInterface::StaticClass() : ResolveClass(Call->GetString(TEXT("parent"), TEXT("Actor")));
			if (!Parent || (!bInterface && !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent)))
			{
				Call->Fail(TEXT("blueprint.bad_parent"), FString::Printf(TEXT("cannot create a Blueprint of '%s'"), *Call->GetString(TEXT("parent"), TEXT("Actor"))));
				return;
			}
			UPackage* Package = CreatePackage(*Path);
			UBlueprint* B = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*FPackageName::GetShortName(Path)), bInterface ? BPTYPE_Interface : BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
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
					// "Name (Class)" plus its parent: an SCS node, or an inherited component it is attached to.
					FString Entry = FString::Printf(TEXT("%s (%s)"), *N->GetVariableName().ToString(), N->ComponentClass ? *N->ComponentClass->GetName() : TEXT("?"));
					if (const USCS_Node* ParentNode = B->SimpleConstructionScript->FindParentNode(N))
						Entry += TEXT(" under ") + ParentNode->GetVariableName().ToString();
					else if (!N->ParentComponentOrVariableName.IsNone())
						Entry += TEXT(" under ") + N->ParentComponentOrVariableName.ToString();
					Components.Add(Entry);
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

	R.Add(TEXT("bp_add_node"), TEXT("blueprint"), TEXT("Add a node. type and memberName: Event (overridable event) | CustomEvent (name) | CallFunction (function; memberParent = class) | VariableGet/VariableSet (variable or component; memberParent = class for another object's variable) | Branch | Sequence | Self | Cast (class) | Macro (ForEachLoop, DoOnce, Gate...) | InputAction (Input Action asset) | ComponentEvent (component variable + delegate, e.g. OnComponentBeginOverlap) | SpawnActor (actor class) | CreateWidget (widget Blueprint) | CallDispatcher / BindDispatcher (event dispatcher; memberParent = class for another object's) | MakeStruct / BreakStruct (struct, e.g. Vector, HitResult) | SwitchEnum (enum) | Timeline (name from bp_add_timeline) | Node (any graph node class, e.g. K2Node_SwitchInteger, K2Node_Select, K2Node_MakeArray, K2Node_SwitchString)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"graph":{"type":"string"},"type":{"type":"string","enum":["Event","CustomEvent","CallFunction","VariableGet","VariableSet","Branch","Sequence","Self","Cast","Macro","InputAction","ComponentEvent","SpawnActor","CreateWidget","CallDispatcher","BindDispatcher","MakeStruct","BreakStruct","SwitchEnum","Timeline","Node"]},"memberName":{"type":"string"},"memberParent":{"type":"string"},"delegate":{"type":"string"},"properties":{"type":"object"},"position":{"type":"array","items":{"type":"number"}}},"required":["path","type"]})"),
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
				// Events of interfaces the Blueprint implements (functions without outputs become events).
				for (const FBPInterfaceDescription& I : B->ImplementedInterfaces)
				{
					if (!F && Parent.IsEmpty() && I.Interface) F = I.Interface->FindFunctionByName(*Member);
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
				// delegate = an event dispatcher name: the event gets its signature so it can be bound with BindDispatcher.
				const FString Signature = Call->GetString(TEXT("delegate"));
				UFunction* SigFn = nullptr;
				if (!Signature.IsEmpty())
				{
					UClass* Owner = Parent.IsEmpty() ? GraphClass(B) : ResolveClass(Parent);
					FMulticastDelegateProperty* Prop = Owner ? FindFProperty<FMulticastDelegateProperty>(Owner, *Signature) : nullptr;
					SigFn = Prop ? Prop->SignatureFunction.Get() : nullptr;
					if (!SigFn)
					{
						Call->Fail(TEXT("blueprint.dispatcher_not_found"), FString::Printf(TEXT("no event dispatcher '%s'"), *Signature), TEXT("add one: bp_add_event_dispatcher, then bp_compile"));
						return;
					}
				}
				Node = Place<UK2Node_CustomEvent>(G, Call, [&](UK2Node_CustomEvent* N) { N->CustomFunctionName = FName(*Member); });
				if (SigFn)
				{
					UK2Node_CustomEvent* Event = CastChecked<UK2Node_CustomEvent>(Node);
					const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
					for (TFieldIterator<FProperty> It(SigFn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
					{
						FEdGraphPinType PinType;
						if (K2->ConvertPropertyToPinType(*It, PinType))
						{
							Event->CreateUserDefinedPin(It->GetFName(), PinType, EGPD_Output);
						}
					}
				}
			}
			else if (Type == TEXT("CallFunction"))
			{
				if (!NeedMember()) return;
				UFunction* F = ResolveFunction(B, Member, Parent);
				// A function or custom event added to this Blueprint since its last compile is only in a fresh skeleton class.
				const UClass* ParentClass = Parent.IsEmpty() ? nullptr : ResolveClass(Parent);
				if (!F && (Parent.IsEmpty() || ParentClass == B->GeneratedClass || ParentClass == B->SkeletonGeneratedClass))
				{
					FKismetEditorUtilities::GenerateBlueprintSkeleton(B, true);
					F = ResolveFunction(B, Member, FString());
				}
				if (!F)
				{
					Call->Fail(TEXT("blueprint.function_not_found"), FString::Printf(TEXT("no callable function '%s'"), *Member), TEXT("find it: bp_search_nodes query=<name>"));
					return;
				}
				// Interface functions are called as messages so any object can be the target.
				if (F->GetOwnerClass() && F->GetOwnerClass()->HasAnyClassFlags(CLASS_Interface))
					Node = Place<UK2Node_Message>(G, Call, [F](UK2Node_Message* N) { N->SetFromFunction(F); });
				else
					Node = Place<UK2Node_CallFunction>(G, Call, [F](UK2Node_CallFunction* N) { N->SetFromFunction(F); });
			}
			else if (Type == TEXT("VariableGet") || Type == TEXT("VariableSet"))
			{
				if (!NeedMember()) return;
				UClass* External = Parent.IsEmpty() ? nullptr : ResolveClass(Parent);
				const bool bExists = External ? External->FindPropertyByName(*Member) != nullptr
					: FBlueprintEditorUtils::FindNewVariableIndex(B, *Member) != INDEX_NONE
						|| (GraphClass(B) && GraphClass(B)->FindPropertyByName(*Member))
						|| (B->ParentClass && B->ParentClass->FindPropertyByName(*Member));
				if (!bExists)
				{
					Call->Fail(TEXT("blueprint.variable_not_found"), FString::Printf(TEXT("no variable or component '%s'%s"), *Member, External ? *(TEXT(" on ") + External->GetName()) : TEXT("")), TEXT("add it: bp_add_variable / bp_add_component (compile first for new components)"));
					return;
				}
				auto SetRef = [&](FMemberReference& Ref) { if (External) Ref.SetExternalMember(*Member, External); else Ref.SetSelfMember(*Member); };
				if (Type == TEXT("VariableGet"))
					Node = Place<UK2Node_VariableGet>(G, Call, [&](UK2Node_VariableGet* N) { SetRef(N->VariableReference); });
				else
					Node = Place<UK2Node_VariableSet>(G, Call, [&](UK2Node_VariableSet* N) { SetRef(N->VariableReference); });
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
			else if (Type == TEXT("InputAction"))
			{
				if (!NeedMember()) return;
				UObject* Action = LoadObject<UObject>(nullptr, *ObjectPath(Member), nullptr, LOAD_NoWarn | LOAD_Quiet);
				UClass* NodeClass = ResolveNodeClass(TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
				FObjectPropertyBase* Prop = NodeClass ? FindFProperty<FObjectPropertyBase>(NodeClass, TEXT("InputAction")) : nullptr;
				if (!Action || !Action->GetClass()->GetName().Equals(TEXT("InputAction")) || !Prop)
				{
					Call->Fail(TEXT("blueprint.input_action_not_found"), FString::Printf(TEXT("no Input Action at '%s'"), *Member), TEXT("create one: input_action_create"));
					return;
				}
				Node = PlaceClass(G, NodeClass, Call, [Prop, Action](UEdGraphNode* N) { Prop->SetObjectPropertyValue_InContainer(N, Action); });
			}
			else if (Type == TEXT("ComponentEvent"))
			{
				if (!NeedMember()) return;
				const FString Delegate = Call->GetString(TEXT("delegate"));
				FObjectProperty* CompProp = GraphClass(B) ? FindFProperty<FObjectProperty>(GraphClass(B), *Member) : nullptr;
				FMulticastDelegateProperty* DelProp = CompProp && CompProp->PropertyClass ? FindFProperty<FMulticastDelegateProperty>(CompProp->PropertyClass, *Delegate) : nullptr;
				if (!DelProp)
				{
					Call->Fail(TEXT("blueprint.event_not_found"), FString::Printf(TEXT("no component '%s' with event '%s'"), *Member, *Delegate), TEXT("delegate examples: OnComponentBeginOverlap, OnComponentEndOverlap, OnComponentHit, OnClicked; compile after bp_add_component"));
					return;
				}
				Node = Place<UK2Node_ComponentBoundEvent>(G, Call, [CompProp, DelProp](UK2Node_ComponentBoundEvent* N) { N->InitializeComponentBoundEventParams(CompProp, DelProp); });
			}
			else if (Type == TEXT("SpawnActor") || Type == TEXT("CreateWidget"))
			{
				UClass* Cls = Member.IsEmpty() ? nullptr : ResolveClass(Member);
				if (!Member.IsEmpty() && !Cls)
				{
					Call->Fail(TEXT("blueprint.class_not_found"), FString::Printf(TEXT("no class '%s'"), *Member));
					return;
				}
				if (Type == TEXT("SpawnActor"))
					Node = PlaceClass(G, UK2Node_SpawnActorFromClass::StaticClass(), Call, [](UEdGraphNode*) {}, true);
				else
					Node = PlaceClass(G, ResolveNodeClass(TEXT("/Script/UMGEditor.K2Node_CreateWidget")), Call, [](UEdGraphNode*) {});
				if (Cls) SetClassPin(Node, Cls);
			}
			else if (Type == TEXT("CallDispatcher") || Type == TEXT("BindDispatcher"))
			{
				if (!NeedMember()) return;
				UClass* Owner = Parent.IsEmpty() ? GraphClass(B) : ResolveClass(Parent);
				FMulticastDelegateProperty* Prop = Owner ? FindFProperty<FMulticastDelegateProperty>(Owner, *Member) : nullptr;
				if (!Prop)
				{
					Call->Fail(TEXT("blueprint.dispatcher_not_found"), FString::Printf(TEXT("no event dispatcher '%s'"), *Member), TEXT("add one: bp_add_event_dispatcher, then bp_compile"));
					return;
				}
				const bool bSelf = Parent.IsEmpty();
				if (Type == TEXT("CallDispatcher"))
					Node = Place<UK2Node_CallDelegate>(G, Call, [Prop, bSelf, Owner](UK2Node_CallDelegate* N) { N->SetFromProperty(Prop, bSelf, Owner); });
				else
					Node = Place<UK2Node_AddDelegate>(G, Call, [Prop, bSelf, Owner](UK2Node_AddDelegate* N) { N->SetFromProperty(Prop, bSelf, Owner); });
			}
			else if (Type == TEXT("MakeStruct") || Type == TEXT("BreakStruct"))
			{
				if (!NeedMember()) return;
				UScriptStruct* S = ResolveStruct(Member);
				if (!S)
				{
					Call->Fail(TEXT("blueprint.struct_not_found"), FString::Printf(TEXT("no struct '%s'"), *Member), TEXT("examples: Vector, Rotator, Transform, HitResult, or a user struct path"));
					return;
				}
				// Structs with a native make/break function (HitResult, ...) use that function, like the editor does.
				const FString Native = S->GetMetaData(Type == TEXT("MakeStruct") ? TEXT("HasNativeMake") : TEXT("HasNativeBreak"));
				UFunction* NativeFn = Native.IsEmpty() ? nullptr : FindObject<UFunction>(nullptr, *Native);
				if (NativeFn)
					Node = Place<UK2Node_CallFunction>(G, Call, [NativeFn](UK2Node_CallFunction* N) { N->SetFromFunction(NativeFn); });
				else if (Type == TEXT("MakeStruct"))
					Node = Place<UK2Node_MakeStruct>(G, Call, [S](UK2Node_MakeStruct* N) { N->StructType = S; });
				else
					Node = Place<UK2Node_BreakStruct>(G, Call, [S](UK2Node_BreakStruct* N) { N->StructType = S; });
			}
			else if (Type == TEXT("SwitchEnum"))
			{
				if (!NeedMember()) return;
				UEnum* E = ResolveEnum(Member);
				if (!E)
				{
					Call->Fail(TEXT("blueprint.enum_not_found"), FString::Printf(TEXT("no enum '%s'"), *Member));
					return;
				}
				Node = Place<UK2Node_SwitchEnum>(G, Call, [E](UK2Node_SwitchEnum* N) { N->SetEnum(E); });
			}
			else if (Type == TEXT("Timeline"))
			{
				if (!NeedMember()) return;
				if (!B->FindTimelineTemplateByVariableName(FName(*Member)))
				{
					Call->Fail(TEXT("blueprint.timeline_not_found"), FString::Printf(TEXT("no timeline '%s'"), *Member), TEXT("add one: bp_add_timeline"));
					return;
				}
				Node = Place<UK2Node_Timeline>(G, Call, [&](UK2Node_Timeline* N) { N->TimelineName = FName(*Member); });
			}
			else if (Type == TEXT("Node"))
			{
				if (!NeedMember()) return;
				UClass* C = ResolveNodeClass(Member);
				if (!C)
				{
					Call->Fail(TEXT("blueprint.node_class_not_found"), FString::Printf(TEXT("no graph node class '%s'"), *Member), TEXT("examples: K2Node_SwitchInteger, K2Node_SwitchString, K2Node_Select, K2Node_MakeArray, K2Node_GetArrayItem"));
					return;
				}
				// properties: node settings applied before pins are built (e.g. CustomClass of K2Node_GetSubsystemFromPC).
				const TSharedPtr<FJsonObject> Props = Call->GetObject(TEXT("properties"));
				FString PropError;
				Node = PlaceClass(G, C, Call, [&Props, &PropError](UEdGraphNode* N)
				{
					if (!Props.IsValid()) return;
					for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Props->Values)
					{
						FProperty* P = N->GetClass()->FindPropertyByName(FName(*KV.Key));
						const FString Text = KV.Value->Type == EJson::String ? KV.Value->AsString() : (KV.Value->Type == EJson::Boolean ? (KV.Value->AsBool() ? TEXT("True") : TEXT("False")) : FString::SanitizeFloat(KV.Value->AsNumber()));
						if (!P || !P->ImportText_Direct(*Text, P->ContainerPtrToValuePtr<void>(N), N, PPF_None))
						{
							PropError = FString::Printf(TEXT("%s: cannot set '%s' to '%s'"), *N->GetClass()->GetName(), *KV.Key, *Text);
							return;
						}
					}
				});
				if (!PropError.IsEmpty())
				{
					Node->DestroyNode();
					Call->Fail(TEXT("args.invalid"), PropError);
					return;
				}
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
			UEdGraphPin* A = ResolvePin(G, Call->GetString(TEXT("from")), Error, EGPD_Output);
			UEdGraphPin* C = A ? ResolvePin(G, Call->GetString(TEXT("to")), Error, EGPD_Input) : nullptr;
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
			UEdGraphPin* P = ResolvePin(G, Call->GetString(TEXT("node")) + TEXT(".") + Call->GetString(TEXT("pin")), Error, EGPD_Input);
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
			const FName Category = P->PinType.PinCategory;
			const bool bObjectPin = Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
				|| Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass || Category == UEdGraphSchema_K2::PC_Interface;
			if (bObjectPin && K2)
			{
				// Object/class pins take an asset or class: a /Game path, a Blueprint (its class for class pins) or a native class name.
				const bool bClass = Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass;
				UObject* Obj = nullptr;
				if (Value.IsEmpty() || Value == TEXT("None")) {}
				else if (bClass) Obj = ResolveClass(Value);
				else Obj = LoadObject<UObject>(nullptr, *ObjectPath(Value), nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (!Obj && !(Value.IsEmpty() || Value == TEXT("None")))
				{
					Call->Fail(TEXT("blueprint.bad_value"), FString::Printf(TEXT("no %s '%s' for pin %s"), bClass ? TEXT("class") : TEXT("asset"), *Value, *P->PinName.ToString()));
					return;
				}
				const FString Invalid = K2->IsPinDefaultValid(P, FString(), Obj, FText::GetEmpty());
				if (!Invalid.IsEmpty())
				{
					Call->Fail(TEXT("blueprint.bad_value"), Invalid);
					return;
				}
				K2->TrySetDefaultObject(*P, Obj);
				Touch(B, false);
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("value"), Obj ? Obj->GetPathName() : TEXT("None"));
				Call->Succeed(O);
				return;
			}
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

	R.Add(TEXT("bp_add_variable"), TEXT("blueprint"), TEXT("Add a member variable. type: bool|int|int64|byte|float|double|string|name|text|vector|rotator|transform|color|<class>|<struct>, optional [], or a container map<K,V> / set<T> (e.g. map<string,int>)."),
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

	R.Add(TEXT("bp_add_event_dispatcher"), TEXT("blueprint"), TEXT("Add an event dispatcher with optional inputs [{name,type}]; then bp_compile and use CallDispatcher / BindDispatcher nodes."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"inputs":{"type":"array"}},"required":["path","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			const FName Name(*Call->GetString(TEXT("name")));
			if (FindGraph(B, Name.ToString()) || FBlueprintEditorUtils::FindNewVariableIndex(B, Name) != INDEX_NONE)
			{
				Call->Fail(TEXT("blueprint.name_taken"), FString::Printf(TEXT("'%s' is already used in this Blueprint"), *Name.ToString()));
				return;
			}
			TArray<TPair<FName, FEdGraphPinType>> Inputs;
			FString Error;
			for (const TSharedPtr<FJsonValue>& V : Call->GetArray(TEXT("inputs")))
			{
				FEdGraphPinType T;
				if (!ParseType(V->AsObject()->GetStringField(TEXT("type")), T, Error))
				{
					Call->Fail(TEXT("args.invalid"), Error);
					return;
				}
				Inputs.Emplace(FName(*V->AsObject()->GetStringField(TEXT("name"))), T);
			}
			B->Modify();
			FEdGraphPinType DelegateType;
			DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
			if (!FBlueprintEditorUtils::AddMemberVariable(B, Name, DelegateType))
			{
				Call->Fail(TEXT("blueprint.add_failed"), TEXT("could not add the event dispatcher"));
				return;
			}
			const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
			UEdGraph* G = FBlueprintEditorUtils::CreateNewGraph(B, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			G->bEditable = false;
			K2->CreateDefaultNodesForGraph(*G);
			K2->CreateFunctionGraphTerminators(*G, static_cast<UClass*>(nullptr));
			K2->AddExtraFunctionFlags(G, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
			K2->MarkFunctionEntryAsEditable(G, true);
			B->DelegateSignatureGraphs.Add(G);
			TArray<UK2Node_FunctionEntry*> Entries;
			G->GetNodesOfClass(Entries);
			if (Entries.Num() > 0)
			{
				for (const TPair<FName, FEdGraphPinType>& In : Inputs)
				{
					Entries[0]->CreateUserDefinedPin(In.Key, In.Value, EGPD_Output);
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("dispatcher"), Name.ToString());
			O->SetNumberField(TEXT("inputs"), Inputs.Num());
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_add_timeline"), TEXT("blueprint"), TEXT("Add a timeline with float tracks [{name, keys:[[time,value],...]}] and place its node in the event graph (outputs: Update, Finished and one pin per track)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"length":{"type":"number"},"loop":{"type":"boolean"},"autoPlay":{"type":"boolean"},"tracks":{"type":"array"},"position":{"type":"array","items":{"type":"number"}}},"required":["path","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			UEdGraph* G = FBlueprintEditorUtils::FindEventGraph(B);
			const FName Name(*Call->GetString(TEXT("name")));
			if (!G || !FBlueprintEditorUtils::DoesSupportTimelines(B))
			{
				Call->Fail(TEXT("blueprint.timeline_unsupported"), TEXT("this Blueprint has no event graph or does not support timelines (Actor-based Blueprints do)"));
				return;
			}
			if (B->FindTimelineTemplateByVariableName(Name))
			{
				Call->Fail(TEXT("blueprint.name_taken"), FString::Printf(TEXT("timeline '%s' already exists"), *Name.ToString()));
				return;
			}
			B->Modify();
			UTimelineTemplate* T = FBlueprintEditorUtils::AddNewTimeline(B, Name);
			if (!T)
			{
				Call->Fail(TEXT("blueprint.add_failed"), TEXT("could not add the timeline (name in use?)"));
				return;
			}
			T->Modify();
			T->TimelineLength = static_cast<float>(Call->GetNumber(TEXT("length"), 5.0));
			T->bLoop = Call->GetBool(TEXT("loop"));
			T->bAutoPlay = Call->GetBool(TEXT("autoPlay"));
			for (const TSharedPtr<FJsonValue>& V : Call->GetArray(TEXT("tracks")))
			{
				const TSharedPtr<FJsonObject> TrackJson = V->AsObject();
				FTTFloatTrack Track;
				Track.SetTrackName(FName(*TrackJson->GetStringField(TEXT("name"))), T);
				Track.CurveFloat = NewObject<UCurveFloat>(T->GetOuter(), NAME_None, RF_Public | RF_Transactional);
				const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
				if (TrackJson->TryGetArrayField(TEXT("keys"), Keys))
				{
					for (const TSharedPtr<FJsonValue>& K : *Keys)
					{
						const TArray<TSharedPtr<FJsonValue>>& P = K->AsArray();
						if (P.Num() == 2) Track.CurveFloat->FloatCurve.AddKey(static_cast<float>(P[0]->AsNumber()), static_cast<float>(P[1]->AsNumber()));
					}
				}
				const int32 Index = T->FloatTracks.Add(Track);
				T->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, Index));
			}
			G->Modify();
			UK2Node_Timeline* Node = Place<UK2Node_Timeline>(G, Call, [&](UK2Node_Timeline* N) { N->TimelineName = Name; });
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			Call->Succeed(NodeJson(G, Node));
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

	R.Add(TEXT("bp_set_replication"), TEXT("blueprint"), TEXT("Replicate a member variable as the Details panel does: mode replicated | notify (adds an OnRep_<Var> function graph, called on clients when the value arrives) | none; condition: None, InitialOnly, OwnerOnly, SkipOwner, SimulatedOnly, AutonomousOnly, SimulatedOrPhysics, InitialOrOwner, Custom, ReplayOrOwner, ReplayOnly, SkipReplay, Dynamic, Never. The actor itself must replicate (bReplicates on its class defaults)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"variable":{"type":"string"},"mode":{"type":"string","enum":["replicated","notify","none"]},"condition":{"type":"string"}},"required":["path","variable"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			const FName Var(*Call->GetString(TEXT("variable")));
			const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(B, Var);
			if (Index == INDEX_NONE)
			{
				TArray<FString> Names;
				for (const FBPVariableDescription& V : B->NewVariables) Names.Add(V.VarName.ToString());
				Call->Fail(TEXT("blueprint.variable_not_found"), FString::Printf(TEXT("no variable '%s'"), *Var.ToString()), FString::Printf(TEXT("variables: %s"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			const FString Mode = Call->GetString(TEXT("mode"), TEXT("replicated"));
			ELifetimeCondition Condition = COND_None;
			if (Call->Has(TEXT("condition")))
			{
				const UEnum* Enum = StaticEnum<ELifetimeCondition>();
				FString Name = Call->GetString(TEXT("condition"));
				if (!Name.StartsWith(TEXT("COND_"))) Name = TEXT("COND_") + Name;
				const int64 Value = Enum->GetValueByNameString(Name);
				if (Value == INDEX_NONE || Value == COND_Max)
				{
					TArray<FString> Valid;
					for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
					{
						if (Enum->GetValueByIndex(i) != COND_Max) Valid.Add(Enum->GetNameStringByIndex(i).RightChop(5));
					}
					Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("'%s' is not a replication condition"), *Call->GetString(TEXT("condition"))), FString::Printf(TEXT("conditions: %s"), *FString::Join(Valid, TEXT(", "))));
					return;
				}
				Condition = static_cast<ELifetimeCondition>(Value);
			}
			B->Modify();
			FBPVariableDescription& V = B->NewVariables[Index];
			FString NotifyFunction;
			if (Mode == TEXT("none"))
			{
				V.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
				V.RepNotifyFunc = NAME_None;
				V.ReplicationCondition = COND_None;
			}
			else
			{
				V.PropertyFlags |= CPF_Net;
				V.ReplicationCondition = Condition;
				if (Mode == TEXT("notify"))
				{
					NotifyFunction = FString::Printf(TEXT("OnRep_%s"), *Var.ToString());
					if (!FindObject<UEdGraph>(B, *NotifyFunction))
					{
						UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(B, FName(*NotifyFunction), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
						FBlueprintEditorUtils::AddFunctionGraph<UClass>(B, Graph, false, nullptr);
					}
					V.PropertyFlags |= CPF_RepNotify;
					V.RepNotifyFunc = FName(*NotifyFunction);
				}
				else
				{
					V.PropertyFlags &= ~CPF_RepNotify;
					V.RepNotifyFunc = NAME_None;
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("variable"), Var.ToString());
			O->SetStringField(TEXT("mode"), Mode);
			O->SetStringField(TEXT("condition"), StaticEnum<ELifetimeCondition>()->GetNameStringByValue(V.ReplicationCondition).RightChop(5));
			if (!NotifyFunction.IsEmpty()) O->SetStringField(TEXT("notifyFunction"), NotifyFunction);
			Call->Succeed(O);
		});

	R.Add(TEXT("bp_set_rpc"), TEXT("blueprint"), TEXT("Make a custom event a remote procedure call as the Details panel does: type server (runs on the server; call it from the owning client), client (runs on the owning client), multicast (runs on the server and every client; call it on the server) or none; reliable=true guarantees delivery."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"event":{"type":"string"},"type":{"type":"string","enum":["server","client","multicast","none"]},"reliable":{"type":"boolean"}},"required":["path","event","type"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlueprint* B = RequireBP(Call);
			if (!B) return;
			const FString Event = Call->GetString(TEXT("event"));
			TArray<UK2Node_CustomEvent*> Events;
			FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CustomEvent>(B, Events);
			UK2Node_CustomEvent* Node = nullptr;
			TArray<FString> Names;
			for (UK2Node_CustomEvent* E : Events)
			{
				Names.Add(E->CustomFunctionName.ToString());
				if (E->CustomFunctionName.ToString() == Event) Node = E;
			}
			if (!Node)
			{
				Call->Fail(TEXT("blueprint.event_not_found"), FString::Printf(TEXT("no custom event '%s'"), *Event),
					Names.IsEmpty() ? FString(TEXT("add one: bp_add_node type=CustomEvent")) : FString::Printf(TEXT("custom events: %s"), *FString::Join(Names, TEXT(", "))));
				return;
			}
			const FString Type = Call->GetString(TEXT("type"));
			const uint32 Flag = Type == TEXT("server") ? FUNC_NetServer : Type == TEXT("client") ? FUNC_NetClient : Type == TEXT("multicast") ? FUNC_NetMulticast : 0;
			Node->Modify();
			Node->FunctionFlags &= ~(FUNC_Net | FUNC_NetServer | FUNC_NetClient | FUNC_NetMulticast | FUNC_NetReliable);
			if (Flag)
			{
				Node->FunctionFlags |= FUNC_Net | Flag;
				if (Call->GetBool(TEXT("reliable"))) Node->FunctionFlags |= FUNC_NetReliable;
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("event"), Event);
			O->SetStringField(TEXT("type"), Flag ? Type : FString(TEXT("none")));
			O->SetBoolField(TEXT("reliable"), (Node->FunctionFlags & FUNC_NetReliable) != 0);
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
