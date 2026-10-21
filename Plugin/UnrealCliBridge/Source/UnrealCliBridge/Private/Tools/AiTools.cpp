// AI authoring: Blackboards, Behavior Trees (edited through their editor graph so the editor and the
// runtime tree stay in sync) and navigation (bounds volumes, build, path queries).

#include "ActorFactories/ActorFactory.h"
#include "AIGraphNode.h"
#include "AIGraphTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "Builders/CubeBuilder.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"
#include "UObject/Package.h"

using namespace UCli;

namespace
{
	template <typename T>
	T* CreateAsset(const FToolCallRef& Call)
	{
		const FString Path = Call->GetString(TEXT("path"));
		// Existence check without LoadObject (a failed load leaves an empty package the new asset would land in).
		if (FPackageName::DoesPackageExist(Path) || FindObject<UObject>(nullptr, *BP::ObjectPath(Path)))
		{
			Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("an asset already exists at %s"), *Path));
			return nullptr;
		}
		UPackage* Pkg = CreatePackage(*Path);
		T* Asset = NewObject<T>(Pkg, FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Pkg->MarkPackageDirty();
		return Asset;
	}

	template <typename T>
	T* LoadAsset(const FToolCallRef& Call, const TCHAR* Key, const TCHAR* Code, const TCHAR* Hint)
	{
		T* A = LoadObject<T>(nullptr, *BP::ObjectPath(Call->GetString(Key)), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!A)
		{
			Call->Fail(Code, FString::Printf(TEXT("no %s at '%s'"), *T::StaticClass()->GetName(), *Call->GetString(Key)), Hint);
		}
		return A;
	}

	// Class of a node or key: native name (with or without prefix) or a Blueprint path.
	UClass* ResolveAiClass(const FString& Name, UClass* Base, const TCHAR* Prefix)
	{
		if (Name.StartsWith(TEXT("/")))
		{
			UClass* C = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!C)
			{
				if (UBlueprint* B = LoadObject<UBlueprint>(nullptr, *BP::ObjectPath(Name), nullptr, LOAD_NoWarn | LOAD_Quiet)) C = B->GeneratedClass;
			}
			return C && C->IsChildOf(Base) ? C : nullptr;
		}
		for (const FString& Candidate : {Name, FString(Prefix) + Name})
		{
			UClass* C = FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
			if (C && C->IsChildOf(Base) && !C->HasAnyClassFlags(CLASS_Abstract)) return C;
		}
		return nullptr;
	}

	bool AddBlackboardKey(UBlackboardData* BB, const TSharedPtr<FJsonObject>& Key, FString& Error)
	{
		const FString Name = Key->GetStringField(TEXT("name"));
		const FString Type = Key->GetStringField(TEXT("type"));
		UClass* KeyClass = ResolveAiClass(Type, UBlackboardKeyType::StaticClass(), TEXT("BlackboardKeyType_"));
		if (Name.IsEmpty() || !KeyClass)
		{
			Error = FString::Printf(TEXT("key '%s': unknown type '%s' (Bool, Int, Float, Vector, Rotator, Object, Class, Name, String, Enum)"), *Name, *Type);
			return false;
		}
		for (const FBlackboardEntry& E : BB->Keys)
		{
			if (E.EntryName == FName(*Name))
			{
				Error = FString::Printf(TEXT("key '%s' already exists"), *Name);
				return false;
			}
		}
		FBlackboardEntry Entry;
		Entry.EntryName = FName(*Name);
		Entry.KeyType = NewObject<UBlackboardKeyType>(BB, KeyClass);
		FString BaseClass;
		if (Key->TryGetStringField(TEXT("baseClass"), BaseClass))
		{
			UClass* Base = ResolveAiClass(BaseClass, UObject::StaticClass(), TEXT(""));
			FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(KeyClass, TEXT("BaseClass"));
			if (!Base || !Prop)
			{
				Error = FString::Printf(TEXT("key '%s': baseClass '%s' not found (or the key type has no base class)"), *Name, *BaseClass);
				return false;
			}
			Prop->SetObjectPropertyValue_InContainer(Entry.KeyType, Base);
		}
		BB->Keys.Add(Entry);
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> KeyNames(const UBlackboardData* BB)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FBlackboardEntry& E : BB->Keys) Out.Add(Str(E.EntryName.ToString()));
		return Out;
	}

	UBehaviorTreeGraph* EnsureGraph(UBehaviorTree* BT)
	{
		if (UBehaviorTreeGraph* G = Cast<UBehaviorTreeGraph>(BT->BTGraph))
		{
			return G;
		}
		UClass* SchemaClass = FindFirstObject<UClass>(TEXT("EdGraphSchema_BehaviorTree"), EFindFirstObjectOptions::NativeFirst);
		UBehaviorTreeGraph* G = CastChecked<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(BT, NAME_None, UBehaviorTreeGraph::StaticClass(), SchemaClass));
		BT->BTGraph = G;
		G->bAllowDeletion = false;
		G->GetSchema()->CreateDefaultNodesForGraph(*G);
		G->OnCreated();
		G->Initialize();
		return G;
	}

	UBehaviorTreeGraphNode* FindBtNode(UBehaviorTreeGraph* G, const FString& Id)
	{
		for (UEdGraphNode* N : G->Nodes)
		{
			UBehaviorTreeGraphNode* BN = Cast<UBehaviorTreeGraphNode>(N);
			if (!BN) continue;
			if ((Id == TEXT("root") && BN->IsA<UBehaviorTreeGraphNode_Root>()) || BN->NodeGuid.ToString() == Id) return BN;
			for (UAIGraphNode* Sub : BN->SubNodes)
			{
				if (Sub && Sub->NodeGuid.ToString() == Id) return Cast<UBehaviorTreeGraphNode>(Sub);
			}
		}
		return nullptr;
	}

	// Sets node-instance properties from text; Blackboard key selectors take a key name.
	bool SetNodeProperties(UObject* Instance, const TSharedPtr<FJsonObject>& Props, UBlackboardData* BB, FString& Error)
	{
		if (!Props.IsValid()) return true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Props->Values)
		{
			FProperty* P = Instance->GetClass()->FindPropertyByName(FName(*KV.Key));
			if (!P)
			{
				Error = FString::Printf(TEXT("%s has no property '%s'"), *Instance->GetClass()->GetName(), *KV.Key);
				return false;
			}
			const FString Text = KV.Value->Type == EJson::String ? KV.Value->AsString() : (KV.Value->Type == EJson::Boolean ? (KV.Value->AsBool() ? TEXT("True") : TEXT("False")) : FString::SanitizeFloat(KV.Value->AsNumber()));
			void* Addr = P->ContainerPtrToValuePtr<void>(Instance);
			FStructProperty* SP = CastField<FStructProperty>(P);
			if (SP && SP->Struct == FBlackboardKeySelector::StaticStruct())
			{
				FBlackboardKeySelector* Sel = static_cast<FBlackboardKeySelector*>(Addr);
				if (BB && BB->GetKeyID(FName(*Text)) == FBlackboard::InvalidKey)
				{
					Error = FString::Printf(TEXT("'%s' is not a key of the blackboard"), *Text);
					return false;
				}
				Sel->SelectedKeyName = FName(*Text);
				if (BB) Sel->ResolveSelectedKey(*BB);
				if (BB && Sel->IsNone())
				{
					Error = FString::Printf(TEXT("'%s' is not a key of the blackboard (or has an unsupported type for %s)"), *Text, *KV.Key);
					return false;
				}
				continue;
			}
			// "Value or Blackboard key" properties (5.6+): a plain value, or "key:<KeyName>" to read it from the blackboard.
			if (SP && SP->Struct->GetName().StartsWith(TEXT("ValueOrBBKey")))
			{
				const bool bKey = Text.StartsWith(TEXT("key:"));
				FProperty* Inner = SP->Struct->FindPropertyByName(bKey ? FName(TEXT("Key")) : FName(TEXT("DefaultValue")));
				if (!Inner || !Inner->ImportText_Direct(bKey ? *Text.RightChop(4) : *Text, Inner->ContainerPtrToValuePtr<void>(Addr), Instance, PPF_None))
				{
					Error = FString::Printf(TEXT("could not set %s to '%s'"), *KV.Key, *Text);
					return false;
				}
				continue;
			}
			if (!P->ImportText_Direct(*Text, Addr, Instance, PPF_None))
			{
				Error = FString::Printf(TEXT("could not set %s to '%s'"), *KV.Key, *Text);
				return false;
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> BtNodeJson(UAIGraphNode* N)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), N->IsA<UBehaviorTreeGraphNode_Root>() ? TEXT("root") : N->NodeGuid.ToString());
		O->SetStringField(TEXT("class"), N->NodeInstance ? N->NodeInstance->GetClass()->GetName() : N->GetClass()->GetName());
		if (const UBTNode* BTN = Cast<UBTNode>(N->NodeInstance)) O->SetStringField(TEXT("name"), BTN->GetNodeName());
		TArray<TSharedPtr<FJsonValue>> Subs;
		for (UAIGraphNode* S : N->SubNodes)
		{
			if (!S) continue;
			TSharedPtr<FJsonObject> SO = BtNodeJson(S);
			SO->SetStringField(TEXT("kind"), S->IsA<UBehaviorTreeGraphNode_Service>() ? TEXT("service") : TEXT("decorator"));
			Subs.Add(Obj(SO));
		}
		if (Subs.Num() > 0) O->SetArrayField(TEXT("attached"), Subs);
		TArray<UAIGraphNode*> Children;
		if (UEdGraphPin* Out = N->GetOutputPin())
		{
			for (UEdGraphPin* L : Out->LinkedTo)
			{
				if (UAIGraphNode* C = Cast<UAIGraphNode>(L->GetOwningNode())) Children.Add(C);
			}
		}
		Children.Sort([](const UAIGraphNode& A, const UAIGraphNode& B) { return A.NodePosX < B.NodePosX; });
		TArray<TSharedPtr<FJsonValue>> Kids;
		for (UAIGraphNode* C : Children) Kids.Add(Obj(BtNodeJson(C)));
		if (Kids.Num() > 0) O->SetArrayField(TEXT("children"), Kids);
		return O;
	}

	UBehaviorTreeGraphNode* PlaceBtNode(UBehaviorTreeGraph* G, UClass* GraphNodeClass, UClass* InstanceClass)
	{
		UBehaviorTreeGraphNode* Node = NewObject<UBehaviorTreeGraphNode>(G, GraphNodeClass, NAME_None, RF_Transactional);
		Node->ClassData = FGraphNodeClassData(InstanceClass, FString());
		G->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	// Removes a node, its attached decorators/services and everything below it.
	void RemoveBtSubtree(UBehaviorTreeGraph* G, UAIGraphNode* N)
	{
		TArray<UAIGraphNode*> Children;
		if (UEdGraphPin* Out = N->GetOutputPin())
		{
			for (UEdGraphPin* L : Out->LinkedTo)
			{
				if (UAIGraphNode* C = Cast<UAIGraphNode>(L->GetOwningNode())) Children.Add(C);
			}
		}
		for (UAIGraphNode* C : Children) RemoveBtSubtree(G, C);
		N->RemoveAllSubNodes();
		G->RemoveNode(N);
	}

	FString KeyTypeName(const UBlackboardKeyType* T)
	{
		FString Name = T ? T->GetClass()->GetName() : FString();
		Name.RemoveFromStart(TEXT("BlackboardKeyType_"));
		return Name;
	}

	UWorld* EditorWorld() { return GEditor->GetEditorWorldContext().World(); }

	FVector VecArg(const FToolCallRef& Call, const TCHAR* Key, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>> A = Call->GetArray(Key);
		return A.Num() == 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : Default;
	}
}

void UCli::RegisterAiTools(FToolRegistry& R)
{
	R.Add(TEXT("blackboard_create"), TEXT("ai"), TEXT("Create a Blackboard with keys [{name, type: Bool|Int|Float|Vector|Rotator|Object|Class|Name|String|Enum, baseClass (Object/Class keys, e.g. Actor)}]."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"keys":{"type":"array"}},"required":["path"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const TArray<TSharedPtr<FJsonValue>> Keys = Call->GetArray(TEXT("keys"));
			UBlackboardData* BB = CreateAsset<UBlackboardData>(Call);
			if (!BB) return;
			FString Error;
			for (const TSharedPtr<FJsonValue>& K : Keys)
			{
				if (!AddBlackboardKey(BB, K->AsObject(), Error))
				{
					Call->Fail(TEXT("args.invalid"), Error);
					return;
				}
			}
			BB->PostEditChange();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), BB->GetPathName());
			O->SetArrayField(TEXT("keys"), KeyNames(BB));
			Call->Succeed(O);
		});

	R.Add(TEXT("blackboard_add_key"), TEXT("ai"), TEXT("Add a key to a Blackboard (type: Bool|Int|Float|Vector|Rotator|Object|Class|Name|String|Enum; baseClass for Object/Class)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"name":{"type":"string"},"type":{"type":"string"},"baseClass":{"type":"string"}},"required":["path","name","type"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlackboardData* BB = LoadAsset<UBlackboardData>(Call, TEXT("path"), TEXT("ai.blackboard_not_found"), TEXT("create one: blackboard_create"));
			if (!BB) return;
			BB->Modify();
			FString Error;
			if (!AddBlackboardKey(BB, Call->Args, Error))
			{
				Call->Fail(TEXT("args.invalid"), Error);
				return;
			}
			BB->PostEditChange();
			BB->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("keys"), KeyNames(BB));
			Call->Succeed(O);
		});

	R.Add(TEXT("behavior_tree_create"), TEXT("ai"), TEXT("Create a Behavior Tree using a Blackboard; build it with bt_add_node (parent \"root\" first)."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"blackboard":{"type":"string"}},"required":["path","blackboard"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBlackboardData* BB = LoadAsset<UBlackboardData>(Call, TEXT("blackboard"), TEXT("ai.blackboard_not_found"), TEXT("create one: blackboard_create"));
			if (!BB) return;
			UBehaviorTree* BT = CreateAsset<UBehaviorTree>(Call);
			if (!BT) return;
			BT->BlackboardAsset = BB;
			UBehaviorTreeGraph* G = EnsureGraph(BT);
			G->UpdateBlackboardChange();
			G->UpdateAsset();
			Call->Succeed(BtNodeJson(CastChecked<UAIGraphNode>(FindBtNode(G, TEXT("root")))));
		});

	R.Add(TEXT("bt_add_node"), TEXT("ai"), TEXT("Add to a Behavior Tree: kind composite (Selector, Sequence, SimpleParallel) or task (MoveTo, Wait, RunBehavior, PlaySound, a Blueprint task path...) as the last child of parent (\"root\" or a node id), or kind decorator (Blackboard, Cooldown, Loop, TimeLimit...) / service (DefaultFocus, RunEQS, a Blueprint service...) attached to parent. properties: {Name: value}; Blackboard key selectors take a key name; value-or-key properties take a value or \"key:<KeyName>\"."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"parent":{"type":"string"},"kind":{"type":"string","enum":["composite","task","decorator","service"]},"class":{"type":"string"},"properties":{"type":"object"}},"required":["path","parent","kind","class"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBehaviorTree* BT = LoadAsset<UBehaviorTree>(Call, TEXT("path"), TEXT("ai.behavior_tree_not_found"), TEXT("create one: behavior_tree_create"));
			if (!BT) return;
			UBehaviorTreeGraph* G = EnsureGraph(BT);
			UBehaviorTreeGraphNode* Parent = FindBtNode(G, Call->GetString(TEXT("parent")));
			if (!Parent)
			{
				Call->Fail(TEXT("ai.node_not_found"), FString::Printf(TEXT("no node '%s' in the tree"), *Call->GetString(TEXT("parent"))), TEXT("ids: bt_get (\"root\" is the root)"));
				return;
			}
			const FString Kind = Call->GetString(TEXT("kind"));
			const FString ClassName = Call->GetString(TEXT("class"));
			const bool bAttached = Kind == TEXT("decorator") || Kind == TEXT("service");
			UClass* Base = FindFirstObject<UClass>(Kind == TEXT("composite") ? TEXT("BTCompositeNode") : Kind == TEXT("task") ? TEXT("BTTaskNode") : Kind == TEXT("decorator") ? TEXT("BTDecorator") : TEXT("BTService"), EFindFirstObjectOptions::NativeFirst);
			const TCHAR* Prefix = Kind == TEXT("composite") ? TEXT("BTComposite_") : Kind == TEXT("task") ? TEXT("BTTask_") : Kind == TEXT("decorator") ? TEXT("BTDecorator_") : TEXT("BTService_");
			UClass* Instance = Base ? ResolveAiClass(ClassName, Base, Prefix) : nullptr;
			if (!Instance)
			{
				Call->Fail(TEXT("ai.class_not_found"), FString::Printf(TEXT("no %s class '%s'"), *Kind, *ClassName), TEXT("composites: Selector, Sequence, SimpleParallel; tasks: MoveTo, Wait, RotateToFaceBBEntry, RunBehavior; decorators: Blackboard, Cooldown, Loop; services: DefaultFocus"));
				return;
			}
			const bool bParentTakesChildren = Parent->IsA<UBehaviorTreeGraphNode_Root>() || Parent->IsA<UBehaviorTreeGraphNode_Composite>();
			if (!bAttached && !bParentTakesChildren)
			{
				Call->Fail(TEXT("ai.invalid_parent"), TEXT("composites and tasks go under the root or a composite"));
				return;
			}
			if (Parent->IsA<UBehaviorTreeGraphNode_Root>() && !bAttached)
			{
				UEdGraphPin* Out = Parent->GetOutputPin();
				if (Out && Out->LinkedTo.Num() > 0)
				{
					Call->Fail(TEXT("ai.invalid_parent"), TEXT("the root takes one child; add a composite (Selector/Sequence) and put nodes under it"));
					return;
				}
			}
			if (bAttached && Parent->IsA<UBehaviorTreeGraphNode_Root>())
			{
				Call->Fail(TEXT("ai.invalid_parent"), TEXT("decorators and services attach to a composite or task, not the root"));
				return;
			}
			BT->Modify();
			G->Modify();
			UClass* GraphNodeClass = nullptr;
			if (Kind == TEXT("composite"))
			{
				UClass* Parallel = FindFirstObject<UClass>(TEXT("BehaviorTreeGraphNode_SimpleParallel"), EFindFirstObjectOptions::NativeFirst);
				GraphNodeClass = Instance->GetName().Contains(TEXT("SimpleParallel")) && Parallel ? Parallel : UBehaviorTreeGraphNode_Composite::StaticClass();
			}
			else if (Kind == TEXT("task")) GraphNodeClass = UBehaviorTreeGraphNode_Task::StaticClass();
			else if (Kind == TEXT("decorator")) GraphNodeClass = UBehaviorTreeGraphNode_Decorator::StaticClass();
			else GraphNodeClass = UBehaviorTreeGraphNode_Service::StaticClass();
			UBehaviorTreeGraphNode* Node = nullptr;
			if (bAttached)
			{
				Node = NewObject<UBehaviorTreeGraphNode>(G, GraphNodeClass, NAME_None, RF_Transactional);
				Node->ClassData = FGraphNodeClassData(Instance, FString());
				Parent->AddSubNode(Node, G);
			}
			else
			{
				Node = PlaceBtNode(G, GraphNodeClass, Instance);
			}
			FString Error;
			if (!Node->NodeInstance || !SetNodeProperties(Node->NodeInstance, Call->GetObject(TEXT("properties")), BT->BlackboardAsset, Error))
			{
				if (bAttached) Parent->RemoveSubNode(Node);
				else G->RemoveNode(Node);
				Call->Fail(TEXT("args.invalid"), Error.IsEmpty() ? TEXT("could not create the node instance") : Error);
				return;
			}
			if (!bAttached)
			{
				int32 Siblings = 0;
				if (UEdGraphPin* Out = Parent->GetOutputPin()) Siblings = Out->LinkedTo.Num();
				Node->NodePosX = Parent->NodePosX + Siblings * 300;
				Node->NodePosY = Parent->NodePosY + 200;
				Parent->GetOutputPin()->MakeLinkTo(Node->GetInputPin());
			}
			G->UpdateAsset();
			BT->MarkPackageDirty();
			Call->Succeed(BtNodeJson(Node));
		});

	R.Add(TEXT("bt_get"), TEXT("ai"), TEXT("A Behavior Tree as a nested tree: node ids, classes, names, attached decorators/services, children in execution order."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UBehaviorTree* BT = LoadAsset<UBehaviorTree>(Call, TEXT("path"), TEXT("ai.behavior_tree_not_found"), TEXT("create one: behavior_tree_create"));
			if (!BT) return;
			UBehaviorTreeGraph* G = EnsureGraph(BT);
			TSharedPtr<FJsonObject> O = BtNodeJson(CastChecked<UAIGraphNode>(FindBtNode(G, TEXT("root"))));
			O->SetStringField(TEXT("blackboard"), BT->BlackboardAsset ? BT->BlackboardAsset->GetPathName() : FString());
			O->SetBoolField(TEXT("runnable"), BT->RootNode != nullptr);
			Call->Succeed(O);
		});

	R.Add(TEXT("bt_remove_node"), TEXT("ai"), TEXT("Remove a Behavior Tree node by id (from bt_get): a decorator/service is detached; a composite or task is removed with everything below it."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"string"}},"required":["path","id"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBehaviorTree* BT = LoadAsset<UBehaviorTree>(Call, TEXT("path"), TEXT("ai.behavior_tree_not_found"), TEXT("create one: behavior_tree_create"));
			if (!BT) return;
			UBehaviorTreeGraph* G = EnsureGraph(BT);
			const FString Id = Call->GetString(TEXT("id"));
			UBehaviorTreeGraphNode* Node = FindBtNode(G, Id);
			if (!Node)
			{
				Call->Fail(TEXT("ai.node_not_found"), FString::Printf(TEXT("no node '%s' in the tree"), *Id), TEXT("ids: bt_get"));
				return;
			}
			if (Node->IsA<UBehaviorTreeGraphNode_Root>())
			{
				Call->Fail(TEXT("ai.invalid_parent"), TEXT("the root cannot be removed"), TEXT("remove its child instead"));
				return;
			}
			BT->Modify();
			G->Modify();
			if (Node->bIsSubNode && Node->ParentNode) Node->ParentNode->RemoveSubNode(Node);
			else RemoveBtSubtree(G, Node);
			G->UpdateAsset();
			BT->MarkPackageDirty();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("removed"), Id);
			Call->Succeed(O);
		});

	R.Add(TEXT("bt_set_node_property"), TEXT("ai"), TEXT("Set properties on a Behavior Tree node (id from bt_get): {Name: value}; Blackboard key selectors take a key name, value-or-key properties a value or \"key:<KeyName>\". Returns the new values as text."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"id":{"type":"string"},"properties":{"type":"object"}},"required":["path","id","properties"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UBehaviorTree* BT = LoadAsset<UBehaviorTree>(Call, TEXT("path"), TEXT("ai.behavior_tree_not_found"), TEXT("create one: behavior_tree_create"));
			if (!BT) return;
			UBehaviorTreeGraph* G = EnsureGraph(BT);
			const FString Id = Call->GetString(TEXT("id"));
			UBehaviorTreeGraphNode* Node = FindBtNode(G, Id);
			if (!Node || !Node->NodeInstance)
			{
				Call->Fail(TEXT("ai.node_not_found"), FString::Printf(TEXT("no node '%s' with properties in the tree"), *Id), TEXT("ids: bt_get (the root has no properties)"));
				return;
			}
			const TSharedPtr<FJsonObject> Props = Call->GetObject(TEXT("properties"));
			BT->Modify();
			Node->NodeInstance->Modify();
			FString Error;
			if (!SetNodeProperties(Node->NodeInstance, Props, BT->BlackboardAsset, Error))
			{
				Call->Fail(TEXT("args.invalid"), Error);
				return;
			}
			Node->NodeInstance->PostEditChange();
			G->UpdateAsset();
			BT->MarkPackageDirty();
			TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
			if (Props.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Props->Values)
				{
					FProperty* P = Node->NodeInstance->GetClass()->FindPropertyByName(FName(*KV.Key));
					FString Text;
					if (P) P->ExportTextItem_Direct(Text, P->ContainerPtrToValuePtr<void>(Node->NodeInstance), nullptr, Node->NodeInstance, PPF_None);
					Values->SetStringField(KV.Key, Text);
				}
			}
			TSharedPtr<FJsonObject> O = BtNodeJson(Node);
			O->SetObjectField(TEXT("values"), Values);
			Call->Succeed(O);
		});

	R.Add(TEXT("blackboard_get"), TEXT("ai"), TEXT("A Blackboard's keys (name, type, baseClass for Object/Class keys, instanceSynced) and its parent Blackboard."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UBlackboardData* BB = LoadAsset<UBlackboardData>(Call, TEXT("path"), TEXT("ai.blackboard_not_found"), TEXT("create one: blackboard_create"));
			if (!BB) return;
			TArray<TSharedPtr<FJsonValue>> Keys;
			for (const FBlackboardEntry& E : BB->Keys)
			{
				TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
				K->SetStringField(TEXT("name"), E.EntryName.ToString());
				K->SetStringField(TEXT("type"), KeyTypeName(E.KeyType));
				if (E.KeyType)
				{
					if (FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(E.KeyType->GetClass(), TEXT("BaseClass")))
					{
						if (const UClass* Base = Cast<UClass>(Prop->GetObjectPropertyValue_InContainer(E.KeyType))) K->SetStringField(TEXT("baseClass"), Base->GetName());
					}
				}
				K->SetBoolField(TEXT("instanceSynced"), E.bInstanceSynced != 0);
				Keys.Add(Obj(K));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), BB->GetPathName());
			O->SetStringField(TEXT("parent"), BB->Parent ? BB->Parent->GetPathName() : FString());
			O->SetArrayField(TEXT("keys"), Keys);
			Call->Succeed(O);
		});

	R.Add(TEXT("nav_bounds_add"), TEXT("ai"), TEXT("Add a Nav Mesh Bounds Volume (location, extent = half size in cm) to the level; navigation rebuilds in the background (check with nav_path)."),
		TEXT(R"({"type":"object","properties":{"location":{"type":"array"},"extent":{"type":"array"},"label":{"type":"string"}}})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UWorld* World = EditorWorld();
			const FVector Extent = VecArg(Call, TEXT("extent"), FVector(2000, 2000, 500));
			ANavMeshBoundsVolume* V = World->SpawnActor<ANavMeshBoundsVolume>(VecArg(Call, TEXT("location"), FVector::ZeroVector), FRotator::ZeroRotator);
			UCubeBuilder* Builder = NewObject<UCubeBuilder>();
			Builder->X = Extent.X * 2;
			Builder->Y = Extent.Y * 2;
			Builder->Z = Extent.Z * 2;
			UActorFactory::CreateBrushForVolumeActor(V, Builder);
			if (Call->Has(TEXT("label"))) V->SetActorLabel(Call->GetString(TEXT("label")));
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				Nav->OnNavigationBoundsUpdated(V);
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("label"), V->GetActorLabel());
			Call->Succeed(O);
		});

	R.Add(TEXT("nav_build"), TEXT("ai"), TEXT("Start rebuilding navigation for the open level (asynchronous: poll until building=false, or query nav_path)."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(EditorWorld());
			if (!Nav)
			{
				Call->Fail(TEXT("ai.no_navigation"), TEXT("the level has no navigation system"), TEXT("add bounds: nav_bounds_add"));
				return;
			}
			for (TActorIterator<ANavMeshBoundsVolume> It(EditorWorld()); It; ++It)
			{
				Nav->OnNavigationBoundsUpdated(*It);
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("building"), Nav->IsNavigationBuildInProgress());
			O->SetBoolField(TEXT("hasNavMesh"), Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr);
			Call->Succeed(O);
		});

	R.Add(TEXT("nav_path"), TEXT("ai"), TEXT("Find a navigation path between two points [x,y,z] in the open level (or the play world when playing): valid, partial, length and points."),
		TEXT(R"({"type":"object","properties":{"from":{"type":"array"},"to":{"type":"array"}},"required":["from","to"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UWorld* World = GEditor->PlayWorld ? GEditor->PlayWorld.Get() : EditorWorld();
			UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, VecArg(Call, TEXT("from"), FVector::ZeroVector), VecArg(Call, TEXT("to"), FVector::ZeroVector));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			const bool bValid = Path && Path->IsValid();
			O->SetBoolField(TEXT("valid"), bValid);
			O->SetBoolField(TEXT("partial"), bValid && Path->IsPartial());
			O->SetNumberField(TEXT("length"), bValid ? Path->GetPathLength() : 0.0);
			TArray<TSharedPtr<FJsonValue>> Points;
			if (bValid)
			{
				for (const FVector& P : Path->PathPoints)
				{
					Points.Add(Arr({Num(P.X), Num(P.Y), Num(P.Z)}));
				}
			}
			O->SetArrayField(TEXT("points"), Points);
			Call->Succeed(O);
		});
}
