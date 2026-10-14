#include "Server/ToolRegistry.h"

#include "ScopedTransaction.h"
#include "Server/JobManager.h"
#include "Server/JsonUtil.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

void FToolCall::Finish(const TSharedPtr<FJsonObject>& Reply)
{
	bool Expected = false;
	if (!bDone.CompareExchange(Expected, true))
	{
		return;
	}
	if (OnReply)
	{
		OnReply(Reply);
	}
}

void FToolCall::Succeed(const TSharedPtr<FJsonValue>& Data)
{
	TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("ok"), true);
	Reply->SetField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonValueNull>());
	Finish(Reply);
}

void FToolCall::Succeed(const TSharedPtr<FJsonObject>& Data)
{
	Succeed(Data.IsValid() ? Obj(Data) : TSharedPtr<FJsonValue>());
}

void FToolCall::Fail(const FString& Code, const FString& Message, const FString& Hint, const TSharedPtr<FJsonValue>& Details)
{
	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), Message);
	if (!Hint.IsEmpty())
	{
		Error->SetStringField(TEXT("hint"), Hint);
	}
	if (Details.IsValid())
	{
		Error->SetField(TEXT("details"), Details);
	}
	TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("ok"), false);
	Reply->SetObjectField(TEXT("error"), Error);
	Finish(Reply);
}

void FToolCall::Log(const FString& Line) const
{
	if (OnLog)
	{
		OnLog(Line);
	}
}

bool FToolCall::Has(const FString& Key) const
{
	return Args.IsValid() && Args->HasField(Key) && !Args->GetField<EJson::None>(Key)->IsNull();
}

FString FToolCall::GetString(const FString& Key, const FString& Default) const
{
	FString Out;
	return Has(Key) && Args->TryGetStringField(Key, Out) ? Out : Default;
}

double FToolCall::GetNumber(const FString& Key, double Default) const
{
	double Out = 0;
	return Has(Key) && Args->TryGetNumberField(Key, Out) ? Out : Default;
}

bool FToolCall::GetBool(const FString& Key, bool Default) const
{
	bool Out = false;
	return Has(Key) && Args->TryGetBoolField(Key, Out) ? Out : Default;
}

TSharedPtr<FJsonObject> FToolCall::GetObject(const FString& Key) const
{
	const TSharedPtr<FJsonObject>* Out = nullptr;
	return Has(Key) && Args->TryGetObjectField(Key, Out) ? *Out : nullptr;
}

TArray<TSharedPtr<FJsonValue>> FToolCall::GetArray(const FString& Key) const
{
	const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
	return Has(Key) && Args->TryGetArrayField(Key, Out) ? *Out : TArray<TSharedPtr<FJsonValue>>();
}

TArray<FString> FToolCall::GetStringArray(const FString& Key) const
{
	TArray<FString> Out;
	for (const TSharedPtr<FJsonValue>& V : GetArray(Key))
	{
		Out.Add(V->AsString());
	}
	return Out;
}

FToolRegistry& FToolRegistry::Get()
{
	static FToolRegistry Instance;
	return Instance;
}

void FToolRegistry::Register(FToolDef Def)
{
	if (!Def.InputSchema.IsValid())
	{
		Def.InputSchema = ParseObject(TEXT("{\"type\":\"object\",\"properties\":{}}"));
	}
	const FString Name = Def.Name;
	Tools.Add(Name, MoveTemp(Def));
}

void FToolRegistry::Add(const FString& Name, const FString& Group, const FString& Summary, const FString& SchemaJson, uint32 Flags, FToolHandler Handler)
{
	FToolDef Def;
	Def.Name = Name;
	Def.Group = Group;
	Def.Summary = Summary;
	Def.Description = Summary;
	Def.InputSchema = ParseObject(SchemaJson);
	ensureMsgf(Def.InputSchema.IsValid(), TEXT("Invalid schema for tool %s"), *Name);
	Def.Flags = Flags;
	Def.Handler = MoveTemp(Handler);
	Register(MoveTemp(Def));
}

void FToolRegistry::Remove(const FString& Name)
{
	Tools.Remove(Name);
}

const FToolDef* FToolRegistry::Find(const FString& Name) const
{
	return Tools.Find(Name);
}

TArray<const FToolDef*> FToolRegistry::All() const
{
	TArray<const FToolDef*> Out;
	for (const TPair<FString, FToolDef>& P : Tools)
	{
		Out.Add(&P.Value);
	}
	Out.Sort([](const FToolDef& A, const FToolDef& B) { return A.Name < B.Name; });
	return Out;
}

TSharedPtr<FJsonObject> FToolRegistry::Describe(const FToolDef& Def) const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), Def.Name);
	O->SetStringField(TEXT("group"), Def.Group);
	O->SetStringField(TEXT("summary"), Def.Summary);
	O->SetStringField(TEXT("description"), Def.Description);
	O->SetObjectField(TEXT("inputSchema"), Def.InputSchema);
	O->SetBoolField(TEXT("mutating"), Def.IsMutating());
	O->SetBoolField(TEXT("longRunning"), Def.IsLongRunning());
	O->SetStringField(TEXT("minEngine"), Def.MinEngine);
	return O;
}

static bool MatchesType(const FString& Type, const TSharedPtr<FJsonValue>& V)
{
	switch (V->Type)
	{
	case EJson::String: return Type == TEXT("string");
	case EJson::Boolean: return Type == TEXT("boolean");
	case EJson::Array: return Type == TEXT("array");
	case EJson::Object: return Type == TEXT("object");
	case EJson::Number:
		return Type == TEXT("number") || (Type == TEXT("integer") && FMath::IsNearlyEqual(V->AsNumber(), FMath::RoundToDouble(V->AsNumber())));
	case EJson::Null: return Type == TEXT("null");
	default: return false;
	}
}

bool FToolRegistry::Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
	if (!Schema.IsValid())
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Props = nullptr;
	Schema->TryGetObjectField(TEXT("properties"), Props);

	const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
	if (Schema->TryGetArrayField(TEXT("required"), Required))
	{
		for (const TSharedPtr<FJsonValue>& R : *Required)
		{
			const FString Key = R->AsString();
			if (!Args->HasField(Key) || Args->GetField<EJson::None>(Key)->IsNull())
			{
				OutError = FString::Printf(TEXT("missing required argument '%s'"), *Key);
				return false;
			}
		}
	}

	bool bAdditional = true;
	Schema->TryGetBoolField(TEXT("additionalProperties"), bAdditional);

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Arg : Args->Values)
	{
		const TSharedPtr<FJsonObject>* PropSchema = nullptr;
		if (!Props || !(*Props)->TryGetObjectField(Arg.Key, PropSchema))
		{
			if (!bAdditional)
			{
				TArray<FString> Known;
				if (Props)
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Props)->Values)
					{
						Known.Add(P.Key);
					}
				}
				OutError = FString::Printf(TEXT("unknown argument '%s' (known: %s)"), *Arg.Key, *FString::Join(Known, TEXT(", ")));
				return false;
			}
			continue;
		}
		if (Arg.Value->IsNull())
		{
			continue;
		}
		FString Type;
		const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
		if ((*PropSchema)->TryGetStringField(TEXT("type"), Type))
		{
			if (!MatchesType(Type, Arg.Value))
			{
				OutError = FString::Printf(TEXT("argument '%s' must be %s"), *Arg.Key, *Type);
				return false;
			}
		}
		else if ((*PropSchema)->TryGetArrayField(TEXT("type"), Types))
		{
			bool bAny = false;
			for (const TSharedPtr<FJsonValue>& T : *Types)
			{
				bAny |= MatchesType(T->AsString(), Arg.Value);
			}
			if (!bAny)
			{
				OutError = FString::Printf(TEXT("argument '%s' has the wrong type"), *Arg.Key);
				return false;
			}
		}
		const TSharedPtr<FJsonObject>* Items = nullptr;
		FString ItemType;
		if (Arg.Value->Type == EJson::Array && (*PropSchema)->TryGetObjectField(TEXT("items"), Items) && (*Items)->TryGetStringField(TEXT("type"), ItemType))
		{
			const TArray<TSharedPtr<FJsonValue>>& Elements = Arg.Value->AsArray();
			for (int32 I = 0; I < Elements.Num(); ++I)
			{
				if (!MatchesType(ItemType, Elements[I]))
				{
					OutError = FString::Printf(TEXT("argument '%s'[%d] must be %s"), *Arg.Key, I, *ItemType);
					return false;
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Enum = nullptr;
		if ((*PropSchema)->TryGetArrayField(TEXT("enum"), Enum) && Arg.Value->Type == EJson::String)
		{
			TArray<FString> Allowed;
			for (const TSharedPtr<FJsonValue>& E : *Enum)
			{
				Allowed.Add(E->AsString());
			}
			if (!Allowed.Contains(Arg.Value->AsString()))
			{
				OutError = FString::Printf(TEXT("argument '%s' must be one of: %s"), *Arg.Key, *FString::Join(Allowed, TEXT(", ")));
				return false;
			}
		}
	}
	return true;
}

void FToolRegistry::Dispatch(const FToolCallRef& Call)
{
	check(IsInGameThread());
	UCli::EnsurePythonTools(*this);
	const FToolDef* Def = Find(Call->Tool);
	if (!Def)
	{
		Call->Fail(TEXT("tool.unknown"), FString::Printf(TEXT("unknown tool '%s'"), *Call->Tool), TEXT("list tools: unreal-cli tool list"));
		return;
	}
	if (!Call->Args.IsValid())
	{
		Call->Args = MakeShared<FJsonObject>();
	}
	FString Error;
	if (!Validate(Def->InputSchema, Call->Args, Error))
	{
		Call->Fail(TEXT("args.invalid"), Error, FString::Printf(TEXT("see the schema: unreal-cli tool describe %s"), *Def->Name));
		return;
	}
	if (Def->IsMutating() && Call->bDryRun)
	{
		TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
		Plan->SetBoolField(TEXT("dryRun"), true);
		Plan->SetStringField(TEXT("tool"), Def->Name);
		Plan->SetObjectField(TEXT("args"), Call->Args);
		Plan->SetStringField(TEXT("message"), TEXT("no changes were made"));
		Call->Succeed(Plan);
		return;
	}
	if (Def->Precheck && !Def->Precheck(Call))
	{
		return;
	}
	if (Def->IsLongRunning())
	{
		const FString JobId = FJobManager::Get().Start(*Def, Call);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("jobId"), JobId);
		Call->Succeed(Data);
		return;
	}
	if (Def->IsUndoable())
	{
		FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("unreal-cli: %s"), *Def->Name)));
		Def->Handler(Call);
	}
	else
	{
		Def->Handler(Call);
	}
}

TSharedPtr<FJsonObject> FToolRegistry::CallNow(const FString& Tool, const TSharedPtr<FJsonObject>& Args, bool bDryRun)
{
	FToolCallRef Call = MakeShared<FToolCall, ESPMode::ThreadSafe>();
	Call->Tool = Tool;
	Call->Args = Args.IsValid() ? Args : MakeShared<FJsonObject>();
	Call->bDryRun = bDryRun;
	TSharedPtr<FJsonObject> Reply;
	Call->OnReply = [&Reply](TSharedPtr<FJsonObject> R) { Reply = R; };
	Dispatch(Call);
	if (!Reply.IsValid())
	{
		Call->OnReply = nullptr;
		Reply = MakeShared<FJsonObject>();
		Reply->SetBoolField(TEXT("ok"), false);
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("code"), TEXT("tool.async"));
		Error->SetStringField(TEXT("message"), TEXT("tool did not finish synchronously"));
		Reply->SetObjectField(TEXT("error"), Error);
	}
	return Reply;
}
