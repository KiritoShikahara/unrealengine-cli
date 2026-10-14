#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "Server/BridgeLog.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

#if UCLI_WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

using namespace UCli;

void UCli::RegisterCodeTools(FToolRegistry& R)
{
	R.Add(TEXT("code_live_compile"), TEXT("code"), TEXT("Compile changed .cpp files with Live Coding and patch the running editor (Windows). Fails with code.live_coding_stuck after timeoutSeconds (default 300)."),
		TEXT(R"({"type":"object","properties":{"timeoutSeconds":{"type":"number"}}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
#if UCLI_WITH_LIVE_CODING
			ILiveCodingModule* LC = FModuleManager::LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
			if (!LC)
			{
				Call->Fail(TEXT("code.live_coding_unavailable"), TEXT("the Live Coding module is not loaded"), TEXT("rebuild instead: unrealengine-cli code apply --force"));
				return;
			}
			if (!LC->IsEnabledForSession())
			{
				LC->EnableForSession(true);
			}
			if (!LC->IsEnabledForSession())
			{
				Call->Fail(TEXT("code.live_coding_unavailable"), LC->GetEnableErrorText().ToString(), TEXT("rebuild instead: unrealengine-cli code apply --force"));
				return;
			}
			// Start without waiting: the engine's blocking wait spins a modal progress dialog on the game thread.
			// The ticker below watches the compile and keeps the editor responsive.
			const int64 LogStart = FBridgeLog::Get().NextSeq();
			ELiveCodingCompileResult Started = ELiveCodingCompileResult::NotStarted;
			LC->Compile(ELiveCodingCompileFlags::None, &Started);
			if (Started != ELiveCodingCompileResult::InProgress)
			{
				Call->Fail(TEXT("code.compile_failed"), Started == ELiveCodingCompileResult::CompileStillActive ? TEXT("a Live Coding compile is already running") : TEXT("Live Coding did not start"),
					TEXT("wait and retry, or rebuild: unrealengine-cli code apply --force"));
				return;
			}
			TSharedRef<bool> bPatched = MakeShared<bool>(false);
			const FDelegateHandle PatchHandle = LC->GetOnPatchCompleteDelegate().AddLambda([bPatched]() { *bPatched = true; });
			const double StartedAt = FPlatformTime::Seconds();
			const double Limit = FMath::Max(10.0, Call->GetNumber(TEXT("timeoutSeconds"), 300.0));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, LC, bPatched, PatchHandle, LogStart, StartedAt, Limit](float)
			{
				const bool bStuck = FPlatformTime::Seconds() - StartedAt > Limit;
				if (LC->IsCompiling() && !Call->bCancelled && !bStuck)
				{
					return true;
				}
				if (bStuck && LC->IsCompiling())
				{
					LC->GetOnPatchCompleteDelegate().Remove(PatchHandle);
					TArray<FString> Lines;
					for (const FBridgeLogLine& L : FBridgeLog::Get().Read(LogStart, 0, ELogVerbosity::Log, TEXT("LogLiveCoding"))) Lines.Add(L.Text);
					Call->Fail(TEXT("code.live_coding_stuck"), FString::Printf(TEXT("Live Coding has not finished after %.0f s"), Limit),
						TEXT("close the editor and rebuild: unrealengine-cli code apply --rebuild --force (Live Coding blocks other builds of this engine while it runs)"), Arr(StrArray(Lines)));
					return false;
				}
				LC->GetOnPatchCompleteDelegate().Remove(PatchHandle);
				TArray<FString> Lines;
				bool bNoChanges = false;
				for (const FBridgeLogLine& L : FBridgeLog::Get().Read(LogStart, 0, ELogVerbosity::Log, TEXT("LogLiveCoding")))
				{
					Lines.Add(L.Text);
					bNoChanges |= L.Text.Contains(TEXT("No changes"));
				}
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - StartedAt);
				O->SetArrayField(TEXT("log"), StrArray(Lines.Num() > 40 ? TArray<FString>(Lines.GetData() + Lines.Num() - 40, 40) : Lines));
				if (*bPatched || bNoChanges)
				{
					O->SetStringField(TEXT("result"), *bPatched ? TEXT("success") : TEXT("no_changes"));
					Call->Succeed(O);
				}
				else
				{
					Call->Fail(TEXT("code.compile_failed"), Call->bCancelled ? TEXT("stopped waiting for Live Coding (it may still finish)") : TEXT("Live Coding finished without patching (compile error?)"),
						TEXT("read the log lines; fix the code and run code apply again, or rebuild: unrealengine-cli code apply --force"), Obj(O));
				}
				return false;
			}), 0.1f);
#else
			Call->Fail(TEXT("code.live_coding_unavailable"), TEXT("Live Coding is only available on Windows"), TEXT("rebuild instead: unrealengine-cli code apply --force"));
#endif
		});
}
