#include "Server/JobManager.h"

#include "Containers/Ticker.h"
#include "Server/JsonUtil.h"

using namespace UCli;

FJobManager& FJobManager::Get()
{
	static FJobManager Instance;
	return Instance;
}

FString FJobManager::Start(const FToolDef& Def, const FToolCallRef& Original)
{
	TSharedPtr<FBridgeJob> Job = MakeShared<FBridgeJob>();
	Job->Id = FString::Printf(TEXT("job-%d"), ++Counter);
	Job->Kind = Def.Name;
	Job->StartedAt = FDateTime::UtcNow();
	Job->Deadline = FPlatformTime::Seconds() + FMath::Max(Original->TimeoutMs, 1000) / 1000.0;

	FToolCallRef Call = MakeShared<FToolCall, ESPMode::ThreadSafe>();
	Call->Tool = Original->Tool;
	Call->Args = Original->Args;
	Call->TimeoutMs = Original->TimeoutMs;
	TWeakPtr<FBridgeJob> Weak = Job;
	Call->OnLog = [Weak](const FString& Line)
	{
		if (TSharedPtr<FBridgeJob> J = Weak.Pin())
		{
			J->Log.Add(Line);
		}
	};
	Call->OnReply = [Weak](TSharedPtr<FJsonObject> Reply)
	{
		TSharedPtr<FBridgeJob> J = Weak.Pin();
		if (!J || J->State == TEXT("cancelled") || J->State == TEXT("timeout"))
		{
			return;
		}
		J->Reply = Reply;
		J->State = Reply->GetBoolField(TEXT("ok")) ? TEXT("succeeded") : TEXT("failed");
		J->Progress = 1.0;
	};
	Job->Call = Call;
	Jobs.Add(Job->Id, Job);

	FToolHandler Handler = Def.Handler;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Job, Call, Handler](float)
	{
		if (Job->State == TEXT("queued"))
		{
			Job->State = TEXT("running");
			Handler(Call);
		}
		return false;
	}));
	EnsureTicker();
	return Job->Id;
}

void FJobManager::EnsureTicker()
{
	if (!bTicking)
	{
		bTicking = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FJobManager::Tick), 0.25f);
	}
}

bool FJobManager::Tick(float)
{
	const double Now = FPlatformTime::Seconds();
	for (TPair<FString, TSharedPtr<FBridgeJob>>& P : Jobs)
	{
		FBridgeJob& J = *P.Value;
		if ((J.State == TEXT("running") || J.State == TEXT("queued")) && Now > J.Deadline)
		{
			J.State = TEXT("timeout");
			J.Call->bCancelled = true;
		}
	}
	return true;
}

TSharedPtr<FBridgeJob> FJobManager::Find(const FString& Id) const
{
	const TSharedPtr<FBridgeJob>* J = Jobs.Find(Id);
	return J ? *J : nullptr;
}

TArray<TSharedPtr<FBridgeJob>> FJobManager::All() const
{
	TArray<TSharedPtr<FBridgeJob>> Out;
	Jobs.GenerateValueArray(Out);
	return Out;
}

bool FJobManager::Cancel(const FString& Id)
{
	TSharedPtr<FBridgeJob> J = Find(Id);
	if (!J || (J->State != TEXT("running") && J->State != TEXT("queued")))
	{
		return false;
	}
	J->State = TEXT("cancelled");
	J->Call->bCancelled = true;
	return true;
}

TSharedPtr<FJsonObject> FJobManager::ToJson(const FBridgeJob& Job, bool bWithLog)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("id"), Job.Id);
	O->SetStringField(TEXT("kind"), Job.Kind);
	O->SetStringField(TEXT("state"), Job.State);
	if (Job.Progress >= 0)
	{
		O->SetNumberField(TEXT("progress"), Job.Progress);
	}
	else
	{
		O->SetField(TEXT("progress"), MakeShared<FJsonValueNull>());
	}
	O->SetStringField(TEXT("startedAt"), Job.StartedAt.ToIso8601());
	if (bWithLog)
	{
		O->SetArrayField(TEXT("log"), StrArray(Job.Log));
	}
	if (Job.Reply.IsValid())
	{
		const TSharedPtr<FJsonObject>* Err = nullptr;
		if (Job.Reply->TryGetObjectField(TEXT("error"), Err))
		{
			O->SetObjectField(TEXT("error"), *Err);
		}
		if (Job.Reply->HasField(TEXT("data")))
		{
			O->SetField(TEXT("result"), Job.Reply->TryGetField(TEXT("data")));
		}
	}
	return O;
}

void FJobManager::RegisterTools(FToolRegistry& R)
{
	R.Add(TEXT("jobs_list"), TEXT("jobs"), TEXT("List jobs started in this editor session."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TSharedPtr<FBridgeJob>& J : FJobManager::Get().All())
			{
				Items.Add(Obj(ToJson(*J, false)));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("jobs_get"), TEXT("jobs"), TEXT("Get a job's state and result."),
		TEXT(R"({"type":"object","properties":{"jobId":{"type":"string"}},"required":["jobId"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FBridgeJob> J = FJobManager::Get().Find(Call->GetString(TEXT("jobId")));
			if (!J)
			{
				Call->Fail(TEXT("jobs.not_found"), TEXT("no such job"), TEXT("list jobs: unreal-cli jobs list"));
				return;
			}
			Call->Succeed(ToJson(*J, false));
		});

	R.Add(TEXT("jobs_logs"), TEXT("jobs"), TEXT("Read a job's log lines from index 'since'."),
		TEXT(R"({"type":"object","properties":{"jobId":{"type":"string"},"since":{"type":"integer"}},"required":["jobId"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FBridgeJob> J = FJobManager::Get().Find(Call->GetString(TEXT("jobId")));
			if (!J)
			{
				Call->Fail(TEXT("jobs.not_found"), TEXT("no such job"));
				return;
			}
			const int32 Since = FMath::Clamp(static_cast<int32>(Call->GetNumber(TEXT("since"))), 0, J->Log.Num());
			TArray<FString> Lines(J->Log.GetData() + Since, J->Log.Num() - Since);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetArrayField(TEXT("lines"), StrArray(Lines));
			O->SetNumberField(TEXT("next"), J->Log.Num());
			O->SetStringField(TEXT("state"), J->State);
			Call->Succeed(O);
		});

	R.Add(TEXT("jobs_cancel"), TEXT("jobs"), TEXT("Cancel a queued or running job."),
		TEXT(R"({"type":"object","properties":{"jobId":{"type":"string"}},"required":["jobId"]})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const FString Id = Call->GetString(TEXT("jobId"));
			if (!FJobManager::Get().Cancel(Id))
			{
				Call->Fail(TEXT("jobs.not_cancellable"), TEXT("job does not exist or already finished"));
				return;
			}
			Call->Succeed(ToJson(*FJobManager::Get().Find(Id), false));
		});
}
