#include "Modules/ModuleManager.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

#if UCLI_WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

using namespace UCli;

void UCli::RegisterCodeTools(FToolRegistry& R)
{
	R.Add(TEXT("code_live_compile"), TEXT("code"), TEXT("Compile changed .cpp files with Live Coding and patch the running editor (Windows)."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
#if UCLI_WITH_LIVE_CODING
			ILiveCodingModule* LC = FModuleManager::LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
			if (!LC)
			{
				Call->Fail(TEXT("code.live_coding_unavailable"), TEXT("the Live Coding module is not loaded"), TEXT("rebuild instead: unreal-cli code apply --force"));
				return;
			}
			if (!LC->IsEnabledForSession())
			{
				LC->EnableForSession(true);
			}
			if (!LC->IsEnabledForSession())
			{
				Call->Fail(TEXT("code.live_coding_unavailable"), LC->GetEnableErrorText().ToString(), TEXT("rebuild instead: unreal-cli code apply --force"));
				return;
			}
			ELiveCodingCompileResult Result = ELiveCodingCompileResult::NotStarted;
			LC->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
			const TCHAR* Name = TEXT("unknown");
			switch (Result)
			{
			case ELiveCodingCompileResult::Success: Name = TEXT("success"); break;
			case ELiveCodingCompileResult::NoChanges: Name = TEXT("no_changes"); break;
			case ELiveCodingCompileResult::InProgress: Name = TEXT("in_progress"); break;
			case ELiveCodingCompileResult::CompileStillActive: Name = TEXT("still_active"); break;
			case ELiveCodingCompileResult::NotStarted: Name = TEXT("not_started"); break;
			case ELiveCodingCompileResult::Failure: Name = TEXT("failure"); break;
			case ELiveCodingCompileResult::Cancelled: Name = TEXT("cancelled"); break;
			default: break;
			}
			if (Result != ELiveCodingCompileResult::Success && Result != ELiveCodingCompileResult::NoChanges)
			{
				Call->Fail(TEXT("code.compile_failed"), FString::Printf(TEXT("Live Coding result: %s"), Name), TEXT("see compiler output: unreal-cli logs --category LogLiveCoding"));
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("result"), Name);
			Call->Succeed(O);
#else
			Call->Fail(TEXT("code.live_coding_unavailable"), TEXT("Live Coding is only available on Windows"), TEXT("rebuild instead: unreal-cli code apply --force"));
#endif
		});
}
