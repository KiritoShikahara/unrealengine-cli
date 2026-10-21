// State Trees: create (with a schema), add states, tasks and transitions through the editor data, compile, read back.

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeFactory.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "Tools/BlueprintHelpers.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	UStateTree* LoadTree(const FToolCallRef& Call, UStateTreeEditorData*& OutData)
	{
		const FString Path = Call->GetString(TEXT("path"));
		UStateTree* ST = LoadObject<UStateTree>(nullptr, *BP::ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
		OutData = ST ? Cast<UStateTreeEditorData>(ST->EditorData) : nullptr;
		if (!OutData)
		{
			Call->Fail(TEXT("statetree.not_found"), FString::Printf(TEXT("no State Tree at '%s'"), *Path), TEXT("create one: statetree_create"));
			return nullptr;
		}
		return ST;
	}

	UStateTreeState* FindState(UStateTreeEditorData* Data, const FString& Name)
	{
		UStateTreeState* Found = nullptr;
		Data->VisitHierarchy([&](UStateTreeState& S, UStateTreeState*)
		{
			if (S.Name.ToString() == Name)
			{
				Found = &S;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});
		return Found;
	}

	UStateTreeState* RequireState(const FToolCallRef& Call, UStateTreeEditorData* Data, const TCHAR* Key)
	{
		const FString Name = Call->GetString(Key);
		UStateTreeState* S = FindState(Data, Name);
		if (!S) Call->Fail(TEXT("statetree.state_not_found"), FString::Printf(TEXT("no state '%s'"), *Name), TEXT("states: statetree_get (the top state is \"Root\")"));
		return S;
	}

	template <typename E>
	bool ParseEnum(const FString& Text, E& Out)
	{
		const int64 V = StaticEnum<E>()->GetValueByNameString(Text);
		if (V == INDEX_NONE) return false;
		Out = static_cast<E>(V);
		return true;
	}

	template <typename E>
	FString EnumName(E Value)
	{
		return StaticEnum<E>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	UClass* ResolveSchema(const FString& Name)
	{
		for (const FString& Candidate : {Name, FString::Printf(TEXT("StateTree%sSchema"), *Name), Name + TEXT("Schema")})
		{
			UClass* C = FindFirstObject<UClass>(*Candidate, EFindFirstObjectOptions::NativeFirst);
			if (C && C->IsChildOf(UStateTreeSchema::StaticClass()) && !C->HasAnyClassFlags(CLASS_Abstract)) return C;
		}
		return nullptr;
	}

	const UScriptStruct* ResolveTask(const FString& Name)
	{
		FString Base = Name;
		Base.RemoveFromStart(TEXT("F"), ESearchCase::CaseSensitive);
		for (const FString& Candidate : {Base, TEXT("StateTree") + Base, TEXT("StateTree") + Base + TEXT("Task"), Base + TEXT("Task")})
		{
			const UScriptStruct* S = FindFirstObject<UScriptStruct>(*Candidate, EFindFirstObjectOptions::NativeFirst);
			if (S && S->IsChildOf(FStateTreeTaskBase::StaticStruct()) && S != FStateTreeTaskBase::StaticStruct()) return S;
		}
		return nullptr;
	}

	// Sets {Name: value} on the task's instance data first, then on the task struct itself.
	bool SetTaskProperties(FStateTreeEditorNode& Node, const TSharedPtr<FJsonObject>& Props, FString& Error)
	{
		if (!Props.IsValid()) return true;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Props->Values)
		{
			const FString Text = KV.Value->Type == EJson::String ? KV.Value->AsString() : (KV.Value->Type == EJson::Boolean ? (KV.Value->AsBool() ? TEXT("True") : TEXT("False")) : FString::SanitizeFloat(KV.Value->AsNumber()));
			bool bSet = false;
			for (FInstancedStruct* IS : {&Node.Instance, &Node.Node})
			{
				const UScriptStruct* S = IS->GetScriptStruct();
				FProperty* P = S ? S->FindPropertyByName(FName(*KV.Key)) : nullptr;
				if (!P) continue;
				if (!P->ImportText_Direct(*Text, P->ContainerPtrToValuePtr<void>(IS->GetMutableMemory()), nullptr, PPF_None))
				{
					Error = FString::Printf(TEXT("could not set %s to '%s'"), *KV.Key, *Text);
					return false;
				}
				bSet = true;
				break;
			}
			if (!bSet)
			{
				Error = FString::Printf(TEXT("the task has no property '%s'"), *KV.Key);
				return false;
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> StateJson(const UStateTreeState* S)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), S->Name.ToString());
		O->SetStringField(TEXT("type"), EnumName(S->Type));
		O->SetStringField(TEXT("selection"), EnumName(S->SelectionBehavior));
		TArray<TSharedPtr<FJsonValue>> Tasks;
		for (const FStateTreeEditorNode& T : S->Tasks)
		{
			Tasks.Add(Str(T.Node.GetScriptStruct() ? T.Node.GetScriptStruct()->GetName() : FString()));
		}
		O->SetArrayField(TEXT("tasks"), Tasks);
		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (const FStateTreeTransition& T : S->Transitions)
		{
			TSharedPtr<FJsonObject> TO = MakeShared<FJsonObject>();
			TO->SetStringField(TEXT("trigger"), EnumName(T.Trigger));
			TO->SetStringField(TEXT("type"), EnumName(T.State.LinkType));
			if (T.State.LinkType == EStateTreeTransitionType::GotoState) TO->SetStringField(TEXT("target"), T.State.Name.ToString());
			Transitions.Add(Obj(TO));
		}
		O->SetArrayField(TEXT("transitions"), Transitions);
		TArray<TSharedPtr<FJsonValue>> Kids;
		for (const UStateTreeState* C : S->Children)
		{
			if (C) Kids.Add(Obj(StateJson(C)));
		}
		if (Kids.Num() > 0) O->SetArrayField(TEXT("children"), Kids);
		return O;
	}

	TSharedPtr<FJsonObject> Compile(UStateTree* ST)
	{
		FStateTreeCompilerLog Log;
		const bool bOk = UStateTreeEditingSubsystem::CompileStateTree(ST, Log);
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const TSharedRef<FTokenizedMessage>& M : Log.ToTokenizedMessages())
		{
			Messages.Add(Str(M->ToText().ToString()));
		}
		ST->MarkPackageDirty();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("compiled"), bOk);
		O->SetArrayField(TEXT("messages"), Messages);
		return O;
	}

	void Changed(UStateTree* ST, UStateTreeEditorData* Data)
	{
		Data->PostEditChange();
		ST->MarkPackageDirty();
	}
}

void UCli::RegisterStateTreeTools(FToolRegistry& R)
{
	R.Add(TEXT("statetree_create"), TEXT("statetree"), TEXT("Create a State Tree asset with a schema: Component (default, StateTreeComponent on any actor), AIComponent (AI controllers), or a schema class name. Starts with one state named Root."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"schema":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const FString Path = Call->GetString(TEXT("path"));
			FString SchemaName = Call->GetString(TEXT("schema"));
			if (SchemaName.IsEmpty()) SchemaName = TEXT("Component");
			UClass* Schema = ResolveSchema(SchemaName);
			if (!Schema)
			{
				Call->Fail(TEXT("statetree.schema_not_found"), FString::Printf(TEXT("no State Tree schema '%s'"), *SchemaName), TEXT("schemas: Component, AIComponent (GameplayStateTree plugin)"));
				return;
			}
			if (FPackageName::DoesPackageExist(Path) || FindObject<UObject>(nullptr, *BP::ObjectPath(Path)))
			{
				Call->Fail(TEXT("asset.exists"), FString::Printf(TEXT("an asset already exists at %s"), *Path));
				return;
			}
			UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
			Factory->SetSchemaClass(Schema);
			IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UStateTree* ST = Cast<UStateTree>(Tools.CreateAsset(FPackageName::GetShortName(Path), FPackageName::GetLongPackagePath(Path), UStateTree::StaticClass(), Factory));
			if (!ST)
			{
				Call->Fail(TEXT("asset.create_failed"), FString::Printf(TEXT("could not create a State Tree at %s"), *Path));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), ST->GetPathName());
			O->SetStringField(TEXT("schema"), Schema->GetName());
			Call->Succeed(O);
		});

	R.Add(TEXT("statetree_add_state"), TEXT("statetree"), TEXT("Add a child state under parent (a state name, e.g. Root). type: State (default), Group; selection: TrySelectChildrenInOrder (default), TryEnterState, TrySelectChildrenAtRandom... State names must be unique in the tree."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"parent":{"type":"string"},"name":{"type":"string"},"type":{"type":"string"},"selection":{"type":"string"}},"required":["path","parent","name"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UStateTreeEditorData* Data = nullptr;
			UStateTree* ST = LoadTree(Call, Data);
			if (!ST) return;
			UStateTreeState* Parent = RequireState(Call, Data, TEXT("parent"));
			if (!Parent) return;
			const FString Name = Call->GetString(TEXT("name"));
			if (Name.IsEmpty() || FindState(Data, Name))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("state name '%s' is empty or already used"), *Name));
				return;
			}
			EStateTreeStateType Type = EStateTreeStateType::State;
			const FString TypeText = Call->GetString(TEXT("type"));
			if (!TypeText.IsEmpty() && !ParseEnum(TypeText, Type))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown state type '%s'"), *TypeText), TEXT("types: State, Group"));
				return;
			}
			EStateTreeStateSelectionBehavior Selection = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
			const FString SelText = Call->GetString(TEXT("selection"));
			if (!SelText.IsEmpty() && !ParseEnum(SelText, Selection))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown selection '%s'"), *SelText), TEXT("TryEnterState, TrySelectChildrenInOrder, TrySelectChildrenAtRandom, TrySelectChildrenWithHighestUtility"));
				return;
			}
			Data->Modify();
			Parent->Modify();
			UStateTreeState& S = Parent->AddChildState(FName(*Name), Type);
			S.SelectionBehavior = Selection;
			Changed(ST, Data);
			Call->Succeed(StateJson(&S));
		});

	R.Add(TEXT("statetree_add_task"), TEXT("statetree"), TEXT("Add a task to a state: class is a task struct (Delay, DebugText, RunParallelStateTree, or any FStateTreeTaskBase struct name); properties {Name: value} set its instance data (e.g. Delay {Duration: 2})."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"state":{"type":"string"},"class":{"type":"string"},"properties":{"type":"object"}},"required":["path","state","class"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UStateTreeEditorData* Data = nullptr;
			UStateTree* ST = LoadTree(Call, Data);
			if (!ST) return;
			UStateTreeState* S = RequireState(Call, Data, TEXT("state"));
			if (!S) return;
			const FString ClassName = Call->GetString(TEXT("class"));
			const UScriptStruct* Task = ResolveTask(ClassName);
			if (!Task)
			{
				Call->Fail(TEXT("statetree.task_not_found"), FString::Printf(TEXT("no State Tree task struct '%s'"), *ClassName), TEXT("tasks: Delay, DebugText, RunParallelStateTree"));
				return;
			}
			FStateTreeEditorNode Node;
			Node.ID = FGuid::NewGuid();
			Node.Node.InitializeAs(Task);
			const FStateTreeNodeBase& Base = Node.Node.Get<FStateTreeNodeBase>();
			if (const UScriptStruct* IT = Cast<const UScriptStruct>(Base.GetInstanceDataType())) Node.Instance.InitializeAs(IT);
			if (const UScriptStruct* RT = Cast<const UScriptStruct>(Base.GetExecutionRuntimeDataType())) Node.ExecutionRuntimeData.InitializeAs(RT);
			FString Error;
			if (!SetTaskProperties(Node, Call->GetObject(TEXT("properties")), Error))
			{
				Call->Fail(TEXT("args.invalid"), Error);
				return;
			}
			Data->Modify();
			S->Modify();
			S->Tasks.Add(MoveTemp(Node));
			Changed(ST, Data);
			Call->Succeed(StateJson(S));
		});

	R.Add(TEXT("statetree_add_transition"), TEXT("statetree"), TEXT("Add a transition to a state. trigger: OnStateCompleted (default), OnStateSucceeded, OnStateFailed, OnTick; type: GotoState (needs target), NextState, NextSelectableState, Succeeded, Failed."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"},"state":{"type":"string"},"trigger":{"type":"string"},"type":{"type":"string"},"target":{"type":"string"}},"required":["path","state","type"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UStateTreeEditorData* Data = nullptr;
			UStateTree* ST = LoadTree(Call, Data);
			if (!ST) return;
			UStateTreeState* S = RequireState(Call, Data, TEXT("state"));
			if (!S) return;
			EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
			const FString TriggerText = Call->GetString(TEXT("trigger"));
			if (!TriggerText.IsEmpty() && !ParseEnum(TriggerText, Trigger))
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown trigger '%s'"), *TriggerText), TEXT("OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick"));
				return;
			}
			EStateTreeTransitionType Type = EStateTreeTransitionType::None;
			const FString TypeText = Call->GetString(TEXT("type"));
			if (!ParseEnum(TypeText, Type) || Type == EStateTreeTransitionType::None)
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown transition type '%s'"), *TypeText), TEXT("GotoState, NextState, NextSelectableState, Succeeded, Failed"));
				return;
			}
			UStateTreeState* Target = nullptr;
			if (Type == EStateTreeTransitionType::GotoState)
			{
				Target = RequireState(Call, Data, TEXT("target"));
				if (!Target) return;
			}
			Data->Modify();
			S->Modify();
			S->AddTransition(Trigger, Type, Target);
			Changed(ST, Data);
			Call->Succeed(StateJson(S));
		});

	R.Add(TEXT("statetree_get"), TEXT("statetree"), TEXT("A State Tree as nested states (name, type, selection, task structs, transitions with trigger/type/target), its schema and whether the compiled data is up to date."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			UStateTreeEditorData* Data = nullptr;
			UStateTree* ST = LoadTree(Call, Data);
			if (!ST) return;
			TArray<TSharedPtr<FJsonValue>> States;
			for (const UStateTreeState* S : Data->SubTrees)
			{
				if (S) States.Add(Obj(StateJson(S)));
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), ST->GetPathName());
			O->SetStringField(TEXT("schema"), Data->Schema ? Data->Schema->GetClass()->GetName() : FString());
			O->SetBoolField(TEXT("ready"), ST->IsReadyToRun());
			O->SetBoolField(TEXT("upToDate"), ST->LastCompiledEditorDataHash == UStateTreeEditingSubsystem::CalculateStateTreeHash(ST));
			O->SetArrayField(TEXT("states"), States);
			Call->Succeed(O);
		});

	R.Add(TEXT("statetree_compile"), TEXT("statetree"), TEXT("Compile a State Tree; returns compiled and the compiler messages. Save it afterwards with asset save."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			UStateTreeEditorData* Data = nullptr;
			UStateTree* ST = LoadTree(Call, Data);
			if (!ST) return;
			UStateTreeEditingSubsystem::ValidateStateTree(ST);
			Call->Succeed(Compile(ST));
		});
}
