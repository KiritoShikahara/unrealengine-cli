#pragma once

#include "CoreMinimal.h"
#include "Server/ToolRegistry.h"

struct FBridgeJob
{
	FString Id;
	FString Kind;
	FString State = TEXT("queued");
	double Progress = -1.0;
	TArray<FString> Log;
	TSharedPtr<FJsonObject> Reply;
	FDateTime StartedAt;
	double Deadline = 0.0;
	TSharedPtr<FToolCall, ESPMode::ThreadSafe> Call;
};

class FJobManager
{
public:
	static FJobManager& Get();

	FString Start(const FToolDef& Def, const FToolCallRef& Original);
	TSharedPtr<FBridgeJob> Find(const FString& Id) const;
	TArray<TSharedPtr<FBridgeJob>> All() const;
	bool Cancel(const FString& Id);

	static TSharedPtr<FJsonObject> ToJson(const FBridgeJob& Job, bool bWithLog);
	static void RegisterTools(class FToolRegistry& Registry);

private:
	void EnsureTicker();
	bool Tick(float DeltaTime);

	TMap<FString, TSharedPtr<FBridgeJob>> Jobs;
	int32 Counter = 0;
	bool bTicking = false;
};
