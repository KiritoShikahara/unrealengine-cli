// Performance of a running play session: per-frame timing, hitches, draw calls and memory, and Unreal Insights traces.

#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Editor.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHIStats.h"
#if __has_include("RenderTimer.h")
#include "RenderTimer.h"
#else
#include "RenderCore.h"
#endif
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	TSharedPtr<FJsonObject> Stats(TArray<double> V)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		if (V.Num() == 0) return O;
		V.Sort();
		double Sum = 0;
		for (double X : V) Sum += X;
		const int32 P95 = FMath::Clamp(FMath::CeilToInt(V.Num() * 0.95) - 1, 0, V.Num() - 1);
		auto Round = [](double X) { return FMath::RoundToDouble(X * 100.0) / 100.0; };
		O->SetNumberField(TEXT("avg"), Round(Sum / V.Num()));
		O->SetNumberField(TEXT("p95"), Round(V[P95]));
		O->SetNumberField(TEXT("max"), Round(V.Last()));
		return O;
	}

	struct FPerfRun
	{
		TArray<double> Frame, Game, Render, Gpu, Draws;
		int32 Hitches = 0;
		uint64 PeakUsed = 0;
		double Start = 0;
	};
}

void UCli::RegisterPerfTools(FToolRegistry& R)
{
	R.Add(TEXT("perf_measure"), TEXT("perf"), TEXT("Measure the running play session for seconds (default 10): frameMs, gameMs, renderMs and gpuMs as {avg, p95, max}, hitches (frames over hitchMs, default 50), drawCalls {avg, max}, memoryMB {used, peak}, fps. partial=true when the session ended early."),
		TEXT(R"({"type":"object","properties":{"seconds":{"type":"number"},"hitchMs":{"type":"number"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			if (!ResolvePlayWorld(Call)) return;
			const double Seconds = FMath::Clamp(Call->GetNumber(TEXT("seconds"), 10.0), 0.5, 600.0);
			const double HitchMs = Call->GetNumber(TEXT("hitchMs"), 50.0);
			TSharedRef<FPerfRun> Run = MakeShared<FPerfRun>();
			Run->Start = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Call, Run, Seconds, HitchMs](float DeltaTime)
			{
				const bool bEnded = GEditor->PlayWorld == nullptr;
				const double Elapsed = FPlatformTime::Seconds() - Run->Start;
				if (!bEnded && Elapsed > 0.0)
				{
					const double FrameMs = FApp::GetDeltaTime() * 1000.0;
					Run->Frame.Add(FrameMs);
					Run->Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
					Run->Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
					Run->Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
					Run->Draws.Add(GNumDrawCallsRHI[0]);
					if (FrameMs > HitchMs) ++Run->Hitches;
					Run->PeakUsed = FMath::Max<uint64>(Run->PeakUsed, FPlatformMemory::GetStats().UsedPhysical);
				}
				if (!bEnded && Elapsed < Seconds && !Call->bCancelled) return true;
				const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("seconds"), FMath::RoundToDouble(FMath::Min(Elapsed, Seconds) * 100.0) / 100.0);
				O->SetNumberField(TEXT("frames"), Run->Frame.Num());
				double Total = 0;
				for (double F : Run->Frame) Total += F;
				O->SetNumberField(TEXT("fps"), Total > 0 ? FMath::RoundToDouble(Run->Frame.Num() * 1000.0 / Total * 10.0) / 10.0 : 0.0);
				O->SetObjectField(TEXT("frameMs"), Stats(Run->Frame));
				O->SetObjectField(TEXT("gameMs"), Stats(Run->Game));
				O->SetObjectField(TEXT("renderMs"), Stats(Run->Render));
				O->SetObjectField(TEXT("gpuMs"), Stats(Run->Gpu));
				O->SetNumberField(TEXT("hitches"), Run->Hitches);
				O->SetNumberField(TEXT("hitchMs"), HitchMs);
				TSharedPtr<FJsonObject> Draws = Stats(Run->Draws);
				Draws->RemoveField(TEXT("p95"));
				O->SetObjectField(TEXT("drawCalls"), Draws);
				TSharedPtr<FJsonObject> Memory = MakeShared<FJsonObject>();
				Memory->SetNumberField(TEXT("used"), FMath::RoundToDouble(Mem.UsedPhysical / 1048576.0));
				Memory->SetNumberField(TEXT("peak"), FMath::RoundToDouble(FMath::Max<uint64>(Run->PeakUsed, Mem.UsedPhysical) / 1048576.0));
				O->SetObjectField(TEXT("memoryMB"), Memory);
				O->SetBoolField(TEXT("partial"), bEnded || Call->bCancelled);
				Call->Succeed(O);
				return false;
			}), 0.0f);
		});

	R.Add(TEXT("perf_trace"), TEXT("perf"), TEXT("Unreal Insights trace of the editor process: start (to file, default Saved/UnrealCli/Traces/<time>.utrace; channels default cpu,gpu,frame,bookmark) or stop. Summarise the file with: unrealengine-cli perf trace summary <file>."),
		TEXT(R"({"type":"object","properties":{"action":{"type":"string","enum":["start","stop"]},"file":{"type":"string"},"channels":{"type":"string"}},"required":["action"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FString Action = Call->GetString(TEXT("action"));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			if (Action == TEXT("start"))
			{
				FString File = Call->GetString(TEXT("file"));
				if (File.IsEmpty())
				{
					File = FPaths::ProjectSavedDir() / TEXT("UnrealCli/Traces") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".utrace");
				}
				File = FPaths::ConvertRelativePathToFull(File);
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
				const FString Channels = Call->GetString(TEXT("channels"), TEXT("cpu,gpu,frame,bookmark"));
				GEngine->Exec(nullptr, TEXT("Trace.Stop"));
				GEngine->Exec(nullptr, *FString::Printf(TEXT("Trace.File \"%s\" %s"), *File, *Channels));
				O->SetStringField(TEXT("file"), File);
				O->SetStringField(TEXT("channels"), Channels);
			}
			else if (Action == TEXT("stop"))
			{
				GEngine->Exec(nullptr, TEXT("Trace.Stop"));
				O->SetBoolField(TEXT("stopped"), true);
			}
			else
			{
				Call->Fail(TEXT("args.invalid"), FString::Printf(TEXT("unknown action '%s'"), *Action), TEXT("start or stop"));
				return;
			}
			Call->Succeed(O);
		});
}
