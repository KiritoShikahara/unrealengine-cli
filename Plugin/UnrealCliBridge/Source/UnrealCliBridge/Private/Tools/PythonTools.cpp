#include "IPythonScriptPlugin.h"
#include "Misc/Base64.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	const FString Marker = TEXT("@@UCLI@@");

	struct FPyResult
	{
		bool bOk = false;
		FString Output;
		FString Error;
		TArray<TSharedPtr<FJsonValue>> Log;
	};

	FPyResult RunPython(const FString& Code)
	{
		FPyResult R;
		IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
		if (!Py || !Py->IsPythonAvailable())
		{
			R.Error = TEXT("Python is not available in this editor");
			return R;
		}
		// ExecuteFile treats code whose first token ends in .py as a file path, so pass the code as data.
		const FTCHARToUTF8 Utf8(*Code);
		const FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		FPythonCommandEx Cmd;
		Cmd.Command = FString::Printf(TEXT("exec(compile(__import__('base64').b64decode('%s').decode('utf-8'), 'unrealengine-cli-exec', 'exec'), globals())"), *Encoded);
		Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Cmd.FileExecutionScope = EPythonFileExecutionScope::Public;
		R.bOk = Py->ExecPythonCommandEx(Cmd);
		for (const FPythonLogOutputEntry& E : Cmd.LogOutput)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			const bool bErr = E.Type == EPythonLogOutputType::Error;
			O->SetStringField(TEXT("type"), bErr ? TEXT("error") : (E.Type == EPythonLogOutputType::Warning ? TEXT("warning") : TEXT("info")));
			O->SetStringField(TEXT("text"), E.Output);
			R.Log.Add(Obj(O));
			if (bErr)
			{
				R.Error += E.Output + TEXT("\n");
			}
			else
			{
				R.Output += E.Output + TEXT("\n");
			}
		}
		if (!R.bOk && R.Error.IsEmpty())
		{
			R.Error = Cmd.CommandResult;
		}
		return R;
	}

	/** Extracts the JSON printed after the marker by unreal_cli_bridge. */
	TSharedPtr<FJsonValue> MarkerPayload(const FString& Output)
	{
		const int32 At = Output.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At == INDEX_NONE)
		{
			return nullptr;
		}
		FString Rest = Output.Mid(At + Marker.Len());
		int32 Nl = INDEX_NONE;
		if (Rest.FindChar(TEXT('\n'), Nl))
		{
			Rest.LeftInline(Nl);
		}
		return ParseValue(Rest);
	}

	FString Base64Utf8(const FString& S)
	{
		const FTCHARToUTF8 Utf8(*S);
		return FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	void InvokePythonTool(const FString& Name, const FToolCallRef& Call)
	{
		const FString Code = FString::Printf(TEXT("import unreal_cli_bridge as _ucli\n_ucli.invoke('%s', '%s')\n"), *Name, *Base64Utf8(ToJson(Call->Args)));
		const FPyResult R = RunPython(Code);
		const TSharedPtr<FJsonValue> Payload = MarkerPayload(R.Output);
		if (!Payload.IsValid() || Payload->Type != EJson::Object)
		{
			Call->Fail(TEXT("python.exception"), R.Error.IsEmpty() ? TEXT("python tool produced no result") : R.Error);
			return;
		}
		const TSharedPtr<FJsonObject> P = Payload->AsObject();
		if (P->GetBoolField(TEXT("ok")))
		{
			Call->Succeed(P->TryGetField(TEXT("data")));
			return;
		}
		const TSharedPtr<FJsonObject> E = P->GetObjectField(TEXT("error"));
		FString Hint;
		E->TryGetStringField(TEXT("hint"), Hint);
		Call->Fail(E->GetStringField(TEXT("code")), E->GetStringField(TEXT("message")), Hint, E->TryGetField(TEXT("details")));
	}

	bool GPythonToolsLoaded = false;

	void RegisterCatalog(FToolRegistry& R)
	{
		if (GPythonToolsLoaded)
		{
			return;
		}
		GPythonToolsLoaded = true;
		const FPyResult Res = RunPython(TEXT("import unreal_cli_bridge as _ucli\n_ucli.emit_catalog()\n"));
		const TSharedPtr<FJsonValue> Payload = MarkerPayload(Res.Output);
		if (!Payload.IsValid() || Payload->Type != EJson::Array)
		{
			UE_LOG(LogTemp, Error, TEXT("UnrealCliBridge: could not load Python tools: %s"), *Res.Error);
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : Payload->AsArray())
		{
			const TSharedPtr<FJsonObject> T = V->AsObject();
			FToolDef Def;
			Def.Name = T->GetStringField(TEXT("name"));
			Def.Group = T->GetStringField(TEXT("group"));
			Def.Summary = T->GetStringField(TEXT("summary"));
			Def.Description = T->GetStringField(TEXT("description"));
			Def.InputSchema = T->GetObjectField(TEXT("inputSchema"));
			if (T->GetBoolField(TEXT("mutating"))) Def.Flags |= EToolFlags::Mutating;
			if (T->GetBoolField(TEXT("longRunning"))) Def.Flags |= EToolFlags::LongRunning;
			if (!T->GetBoolField(TEXT("undoable"))) Def.Flags |= EToolFlags::NoUndo;
			const FString Name = Def.Name;
			Def.Handler = [Name](const FToolCallRef& Call) { InvokePythonTool(Name, Call); };
			R.Register(MoveTemp(Def));
		}
	}
}

void UCli::RegisterPythonTools(FToolRegistry& R)
{
	R.Add(TEXT("python_exec"), TEXT("python"), TEXT("Run Python inside the editor (import unreal). Returns printed output and log."),
		TEXT(R"({"type":"object","properties":{"code":{"type":"string"}},"required":["code"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const FPyResult Res = RunPython(Call->GetString(TEXT("code")));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("succeeded"), Res.bOk);
			O->SetStringField(TEXT("output"), Res.Output);
			O->SetStringField(TEXT("error"), Res.Error);
			O->SetArrayField(TEXT("log"), Res.Log);
			if (!Res.bOk)
			{
				Call->Fail(TEXT("python.exception"), Res.Error.TrimStartAndEnd(), TEXT("fix the script and retry"), Obj(O));
				return;
			}
			Call->Succeed(O);
		});

	EnsurePythonTools(R);
}

bool UCli::ArePythonToolsReady()
{
	// Without Python there is nothing to wait for.
	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	return GPythonToolsLoaded || !Py || !Py->IsPythonAvailable();
}

void UCli::EnsurePythonTools(FToolRegistry& R)
{
	IPythonScriptPlugin* Py = IPythonScriptPlugin::Get();
	if (!GPythonToolsLoaded && Py && Py->IsPythonInitialized())
	{
		RegisterCatalog(R);
	}
}
