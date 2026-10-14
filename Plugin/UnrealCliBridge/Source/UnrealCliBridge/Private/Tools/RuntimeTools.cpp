#include "Containers/Ticker.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	TArray<FAutomationTestInfo> MatchingTests(const FString& Filter)
	{
		TArray<FAutomationTestInfo> All;
		FAutomationTestFramework::Get().GetValidTestNames(All);
		return All.FilterByPredicate([&Filter](const FAutomationTestInfo& T)
		{
			return Filter.IsEmpty() || T.GetFullTestPath().Contains(Filter);
		});
	}

	struct FPieSession
	{
		FToolCallRef Call;
		double Duration = 5.0;
		FString StopOnLog;
		int64 LogStart = 0;
		double RequestedAt = 0.0;
		double StartedAt = 0.0;
		bool bStarted = false;
	};

	void FinishPie(const TSharedRef<FPieSession>& S, const FString& Reason)
	{
		const double Played = S->bStarted ? FPlatformTime::Seconds() - S->StartedAt : 0.0;
		if (GEditor->PlayWorld)
		{
			GEditor->RequestEndPlayMap();
		}
		TArray<TSharedPtr<FJsonValue>> Errors;
		int32 Warnings = 0;
		for (const FBridgeLogLine& L : FBridgeLog::Get().Read(S->LogStart, 0, ELogVerbosity::Warning, FString()))
		{
			if (L.Verbosity <= ELogVerbosity::Error)
			{
				Errors.Add(Str(FString::Printf(TEXT("%s: %s"), *L.Category.ToString(), *L.Text)));
			}
			else
			{
				++Warnings;
			}
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ran"), S->bStarted);
		O->SetStringField(TEXT("endedReason"), Reason);
		O->SetNumberField(TEXT("playSeconds"), Played);
		O->SetArrayField(TEXT("errors"), Errors);
		O->SetNumberField(TEXT("warnings"), Warnings);
		S->Call->Succeed(O);
	}

	struct FAutomationRun
	{
		FToolCallRef Call;
		TArray<FAutomationTestInfo> Tests;
		int32 Index = -1;
		bool bRunning = false;
		TArray<TSharedPtr<FJsonValue>> Results;
		int32 Failed = 0;
	};
}

void UCli::RegisterRuntimeTools(FToolRegistry& R)
{
	R.Add(TEXT("pie_run"), TEXT("runtime"), TEXT("Play In Editor for durationSeconds (or until a log line contains stopOnLog), then stop and report errors."),
		TEXT(R"({"type":"object","properties":{"durationSeconds":{"type":"number"},"stopOnLog":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
			{
				Call->Fail(TEXT("pie.already_running"), TEXT("a play session is already running"), TEXT("stop it: pie_stop"));
				return;
			}
			TSharedRef<FPieSession> S = MakeShared<FPieSession>(FPieSession{Call});
			S->Duration = FMath::Max(0.1, Call->GetNumber(TEXT("durationSeconds"), 5.0));
			S->StopOnLog = Call->GetString(TEXT("stopOnLog"));
			S->LogStart = FBridgeLog::Get().NextSeq();
			S->RequestedAt = FPlatformTime::Seconds();
			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			GEditor->RequestPlaySession(Params);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float)
			{
				const double Now = FPlatformTime::Seconds();
				if (!S->bStarted)
				{
					if (GEditor->PlayWorld)
					{
						S->bStarted = true;
						S->StartedAt = Now;
					}
					else if (Now - S->RequestedAt > 60.0)
					{
						S->Call->Fail(TEXT("pie.start_failed"), TEXT("the play session did not start within 60 s"), TEXT("check the log: unreal-cli logs --severity warning"));
						return false;
					}
					return true;
				}
				if (S->Call->bCancelled)
				{
					FinishPie(S, TEXT("cancelled"));
					return false;
				}
				if (!GEditor->PlayWorld)
				{
					FinishPie(S, TEXT("ended"));
					return false;
				}
				if (!S->StopOnLog.IsEmpty())
				{
					for (const FBridgeLogLine& L : FBridgeLog::Get().Read(S->LogStart, 0, ELogVerbosity::VeryVerbose, FString()))
					{
						if (L.Text.Contains(S->StopOnLog))
						{
							FinishPie(S, TEXT("log"));
							return false;
						}
					}
				}
				if (Now - S->StartedAt >= S->Duration)
				{
					FinishPie(S, TEXT("duration"));
					return false;
				}
				return true;
			}), 0.05f);
		});

	R.Add(TEXT("pie_stop"), TEXT("runtime"), TEXT("Stop the running play session."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const bool bWas = GEditor->PlayWorld != nullptr;
			if (bWas)
			{
				GEditor->RequestEndPlayMap();
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("stopped"), bWas);
			Call->Succeed(O);
		});

	R.Add(TEXT("pie_status"), TEXT("runtime"), TEXT("Whether a play session is running and its world."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("playing"), GEditor->PlayWorld != nullptr);
			O->SetStringField(TEXT("world"), GEditor->PlayWorld ? GEditor->PlayWorld->GetPathName() : FString());
			Call->Succeed(O);
		});

	R.Add(TEXT("automation_list"), TEXT("runtime"), TEXT("List Automation tests whose full path contains filter."),
		TEXT(R"({"type":"object","properties":{"filter":{"type":"string"},"limit":{"type":"integer"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const int32 Limit = static_cast<int32>(Call->GetNumber(TEXT("limit"), 500));
			TArray<FString> Names;
			for (const FAutomationTestInfo& T : MatchingTests(Call->GetString(TEXT("filter"))))
			{
				if (Names.Num() >= Limit) break;
				Names.Add(T.GetFullTestPath());
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("tests"), StrArray(Names));
			O->SetNumberField(TEXT("count"), Names.Num());
			Call->Succeed(O);
		});

	R.Add(TEXT("automation_run"), TEXT("runtime"), TEXT("Run Automation tests matching filter as a job; the result lists each test with its errors."),
		TEXT(R"({"type":"object","properties":{"filter":{"type":"string"}},"required":["filter"]})"), EToolFlags::LongRunning,
		[](const FToolCallRef& Call)
		{
			TSharedRef<FAutomationRun> Run = MakeShared<FAutomationRun>(FAutomationRun{Call});
			Run->Tests = MatchingTests(Call->GetString(TEXT("filter")));
			if (Run->Tests.Num() == 0)
			{
				Call->Fail(TEXT("automation.no_tests"), TEXT("no test matches the filter"), TEXT("list tests: automation_list"));
				return;
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float)
			{
				FAutomationTestFramework& F = FAutomationTestFramework::Get();
				if (Run->bRunning)
				{
					if (!F.ExecuteLatentCommands())
					{
						return true;
					}
					FAutomationTestExecutionInfo Info;
					const bool bOk = F.StopTest(Info);
					Run->bRunning = false;
					TArray<FString> Errors;
					for (const FAutomationExecutionEntry& E : Info.GetEntries())
					{
						if (E.Event.Type == EAutomationEventType::Error)
						{
							Errors.Add(E.Event.Message);
						}
					}
					TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
					T->SetStringField(TEXT("test"), Run->Tests[Run->Index].GetFullTestPath());
					T->SetBoolField(TEXT("passed"), bOk);
					T->SetArrayField(TEXT("errors"), StrArray(Errors));
					Run->Results.Add(Obj(T));
					Run->Failed += bOk ? 0 : 1;
					Run->Call->Log(FString::Printf(TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Run->Tests[Run->Index].GetFullTestPath()));
				}
				if (Run->Call->bCancelled || ++Run->Index >= Run->Tests.Num())
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetNumberField(TEXT("total"), Run->Results.Num());
					O->SetNumberField(TEXT("failed"), Run->Failed);
					O->SetArrayField(TEXT("tests"), Run->Results);
					if (Run->Failed > 0)
					{
						Run->Call->Fail(TEXT("automation.failed"), FString::Printf(TEXT("%d of %d test(s) failed"), Run->Failed, Run->Results.Num()), FString(), Obj(O));
					}
					else
					{
						Run->Call->Succeed(O);
					}
					return false;
				}
				const FAutomationTestInfo& Info = Run->Tests[Run->Index];
				FString Command = Info.GetTestName();
				if (!Info.GetTestParameter().IsEmpty())
				{
					Command += TEXT(" ") + Info.GetTestParameter();
				}
				F.StartTestByName(Command, 0, Info.GetFullTestPath());
				Run->bRunning = true;
				return true;
			}));
		});
}
