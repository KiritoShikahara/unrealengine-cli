#include "Compat/EngineCompat.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/PlatformProcess.h"
#include "HighResScreenshot.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"
#include "UnrealClient.h"

using namespace UCli;

namespace
{
	const double GStartSeconds = FPlatformTime::Seconds();

	TSharedPtr<FJsonObject> EditorInfo()
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
		O->SetStringField(TEXT("pluginVersion"), PluginVersion());
		O->SetNumberField(TEXT("protocolVersion"), ProtocolVersion);
		O->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
		O->SetStringField(TEXT("projectName"), FApp::GetProjectName());
		O->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		O->SetNumberField(TEXT("uptimeSeconds"), FPlatformTime::Seconds() - GStartSeconds);
		if (GEditor)
		{
			O->SetBoolField(TEXT("playInEditor"), GEditor->PlayWorld != nullptr);
			UWorld* World = GEditor->GetEditorWorldContext().World();
			O->SetStringField(TEXT("map"), World ? World->GetOutermost()->GetName() : FString());
			TArray<UPackage*> Dirty;
			FEditorFileUtils::GetDirtyContentPackages(Dirty);
			FEditorFileUtils::GetDirtyWorldPackages(Dirty);
			O->SetNumberField(TEXT("dirtyPackages"), Dirty.Num());
		}
		return O;
	}

	TSharedPtr<FJsonObject> VectorJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X);
		O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z);
		return O;
	}

	bool ReadVector(const TArray<TSharedPtr<FJsonValue>>& A, FVector& Out)
	{
		if (A.Num() != 3)
		{
			return false;
		}
		Out = FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
		return true;
	}

	FEditorViewportClient* ActiveViewportClient()
	{
		if (!GEditor || !GEditor->GetActiveViewport())
		{
			return nullptr;
		}
		return static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	}
}

void UCli::RegisterSystemTools(FToolRegistry& R)
{
	R.Add(TEXT("system_ping"), TEXT("system"), TEXT("Check the bridge answers; returns editor identity."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FJsonObject> O = EditorInfo();
			O->SetBoolField(TEXT("pong"), true);
			Call->Succeed(O);
		});

	R.Add(TEXT("tools_list"), TEXT("system"), TEXT("List registered tools with their input schemas."),
		TEXT(R"({"type":"object","properties":{"group":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FString Group = Call->GetString(TEXT("group"));
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FToolDef* Def : FToolRegistry::Get().All())
			{
				if (Group.IsEmpty() || Def->Group == Group)
				{
					Items.Add(Obj(FToolRegistry::Get().Describe(*Def)));
				}
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("editor_info"), TEXT("editor"), TEXT("Engine version, project, open map, PIE state, dirty package count."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call) { Call->Succeed(EditorInfo()); });

	R.Add(TEXT("editor_logs"), TEXT("editor"), TEXT("Tail the editor log. Pass since = previous nextSince to get only newer lines."),
		TEXT(R"({"type":"object","properties":{"since":{"type":"integer"},"limit":{"type":"integer"},"severity":{"type":"string","enum":["error","warning","display","log","verbose"]},"category":{"type":"string"}}})"),
		EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FBridgeLog& Log = FBridgeLog::Get();
			const TArray<FBridgeLogLine> Lines = Log.Read(
				static_cast<int64>(Call->GetNumber(TEXT("since"))),
				static_cast<int32>(Call->GetNumber(TEXT("limit"), 200)),
				FBridgeLog::ParseVerbosity(Call->GetString(TEXT("severity"), TEXT("log"))),
				Call->GetString(TEXT("category")));
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FBridgeLogLine& L : Lines)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("seq"), static_cast<double>(L.Seq));
				O->SetStringField(TEXT("severity"), FBridgeLog::VerbosityName(L.Verbosity));
				O->SetStringField(TEXT("category"), L.Category.ToString());
				O->SetStringField(TEXT("text"), L.Text);
				Items.Add(Obj(O));
			}
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetArrayField(TEXT("lines"), Items);
			Out->SetNumberField(TEXT("nextSince"), static_cast<double>(Log.NextSeq()));
			Call->Succeed(Out);
		});

	R.Add(TEXT("editor_undo"), TEXT("editor"), TEXT("Undo the last editor transaction."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("undone"), GEditor && GEditor->UndoTransaction());
			Call->Succeed(O);
		});

	R.Add(TEXT("editor_redo"), TEXT("editor"), TEXT("Redo the last undone editor transaction."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("redone"), GEditor && GEditor->RedoTransaction());
			Call->Succeed(O);
		});

	R.Add(TEXT("editor_save_all"), TEXT("editor"), TEXT("Save every dirty map and asset package."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			TArray<UPackage*> Dirty;
			FEditorFileUtils::GetDirtyContentPackages(Dirty);
			FEditorFileUtils::GetDirtyWorldPackages(Dirty);
			TArray<FString> Names;
			for (UPackage* P : Dirty)
			{
				Names.Add(P->GetName());
			}
			const bool bOk = UEditorLoadingAndSavingUtils::SavePackages(Dirty, true);
			if (!bOk)
			{
				Call->Fail(TEXT("editor.save_failed"), TEXT("some packages could not be saved"), TEXT("check the log: unreal-cli logs --severity warning"), Arr(StrArray(Names)));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("saved"), StrArray(Names));
			Call->Succeed(O);
		});

	R.Add(TEXT("editor_selection_get"), TEXT("editor"), TEXT("Labels and paths of the selected level actors."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			if (GEditor)
			{
				for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
				{
					if (AActor* A = Cast<AActor>(*It))
					{
						TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
						O->SetStringField(TEXT("label"), A->GetActorLabel());
						O->SetStringField(TEXT("path"), A->GetPathName());
						Items.Add(Obj(O));
					}
				}
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("editor_selection_set"), TEXT("editor"), TEXT("Select level actors by label (replaces the selection)."),
		TEXT(R"({"type":"object","properties":{"labels":{"type":"array","items":{"type":"string"}}},"required":["labels"]})"),
		EToolFlags::Mutating,
		[](const FToolCallRef& Call)
		{
			const TArray<FString> Labels = Call->GetStringArray(TEXT("labels"));
			UWorld* World = GEditor->GetEditorWorldContext().World();
			GEditor->SelectNone(false, true);
			TArray<FString> Found;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (Labels.Contains(It->GetActorLabel()))
				{
					GEditor->SelectActor(*It, true, false);
					Found.Add(It->GetActorLabel());
				}
			}
			GEditor->NoteSelectionChange();
			TArray<FString> Missing = Labels.FilterByPredicate([&Found](const FString& L) { return !Found.Contains(L); });
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("selected"), StrArray(Found));
			O->SetArrayField(TEXT("missing"), StrArray(Missing));
			Call->Succeed(O);
		});

	R.Add(TEXT("editor_viewport_camera"), TEXT("editor"), TEXT("Get the active viewport camera, or move it when location/rotation are given."),
		TEXT(R"({"type":"object","properties":{"location":{"type":"array","items":{"type":"number"}},"rotation":{"type":"array","items":{"type":"number"}}}})"),
		EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			FEditorViewportClient* Client = ActiveViewportClient();
			if (!Client)
			{
				Call->Fail(TEXT("editor.no_viewport"), TEXT("no active viewport (is the editor running with -nullrhi?)"));
				return;
			}
			FVector V;
			if (ReadVector(Call->GetArray(TEXT("location")), V))
			{
				Client->SetViewLocation(V);
			}
			if (ReadVector(Call->GetArray(TEXT("rotation")), V))
			{
				Client->SetViewRotation(FRotator(V.X, V.Y, V.Z));
			}
			const FRotator Rot = Client->GetViewRotation();
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetObjectField(TEXT("location"), VectorJson(Client->GetViewLocation()));
			O->SetObjectField(TEXT("rotation"), VectorJson(FVector(Rot.Pitch, Rot.Yaw, Rot.Roll)));
			Call->Succeed(O);
		});

	R.Add(TEXT("editor_screenshot"), TEXT("editor"), TEXT("Capture the active viewport to a PNG file."),
		TEXT(R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			if (!ActiveViewportClient())
			{
				Call->Fail(TEXT("editor.no_viewport"), TEXT("no active viewport to capture"));
				return;
			}
			const FString Path = FPaths::ConvertRelativePathToFull(Call->GetString(TEXT("path")));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Path);
			O->SetBoolField(TEXT("requested"), true);
			Call->Succeed(O);
		});
}
