#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** One in-flight tool invocation. Handlers finish it with Succeed or Fail, now or on a later tick. */
class FToolCall : public TSharedFromThis<FToolCall, ESPMode::ThreadSafe>
{
public:
	FString Tool;
	TSharedPtr<FJsonObject> Args;
	bool bDryRun = false;
	int32 TimeoutMs = 30000;

	/** Receives {"ok":true,"data":...} or {"ok":false,"error":{...}} exactly once. */
	TFunction<void(TSharedPtr<FJsonObject>)> OnReply;
	/** Optional progress sink (set for jobs). */
	TFunction<void(const FString&)> OnLog;
	TAtomic<bool> bCancelled{false};

	void Succeed(const TSharedPtr<FJsonValue>& Data);
	void Succeed(const TSharedPtr<FJsonObject>& Data);
	void Fail(const FString& Code, const FString& Message, const FString& Hint = FString(), const TSharedPtr<FJsonValue>& Details = nullptr);
	void Log(const FString& Line) const;
	bool IsDone() const { return bDone; }

	bool Has(const FString& Key) const;
	FString GetString(const FString& Key, const FString& Default = FString()) const;
	double GetNumber(const FString& Key, double Default = 0.0) const;
	bool GetBool(const FString& Key, bool Default = false) const;
	TSharedPtr<FJsonObject> GetObject(const FString& Key) const;
	TArray<TSharedPtr<FJsonValue>> GetArray(const FString& Key) const;
	TArray<FString> GetStringArray(const FString& Key) const;

private:
	void Finish(const TSharedPtr<FJsonObject>& Reply);
	TAtomic<bool> bDone{false};
};

using FToolCallRef = TSharedRef<FToolCall, ESPMode::ThreadSafe>;
using FToolHandler = TFunction<void(const FToolCallRef&)>;

namespace EToolFlags
{
	enum Type : uint32
	{
		None = 0,
		/** Changes editor state: honours dry-run and records an undo transaction. */
		Mutating = 1 << 0,
		/** Runs as a job; the call returns {jobId} immediately. */
		LongRunning = 1 << 1,
		/** Mutating but not undoable (saving, building, file writes). */
		NoUndo = 1 << 2,
	};
}

struct FToolDef
{
	FString Name;
	FString Group;
	FString Summary;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema;
	uint32 Flags = EToolFlags::None;
	FString MinEngine = TEXT("5.5");
	FToolHandler Handler;

	bool IsMutating() const { return (Flags & EToolFlags::Mutating) != 0; }
	bool IsLongRunning() const { return (Flags & EToolFlags::LongRunning) != 0; }
	bool IsUndoable() const { return IsMutating() && (Flags & EToolFlags::NoUndo) == 0; }
};

class FToolRegistry
{
public:
	static FToolRegistry& Get();

	void Register(FToolDef Def);
	/** SchemaJson is a JSON Schema object literal for the arguments. */
	void Add(const FString& Name, const FString& Group, const FString& Summary, const FString& SchemaJson, uint32 Flags, FToolHandler Handler);
	void Remove(const FString& Name);

	const FToolDef* Find(const FString& Name) const;
	TArray<const FToolDef*> All() const;
	TSharedPtr<FJsonObject> Describe(const FToolDef& Def) const;

	/** Runs a call on the game thread: lookup, schema check, dry-run, transaction, job routing. */
	void Dispatch(const FToolCallRef& Call);

	/** Synchronous helper for tests: dispatches and returns the reply (handler must finish within the tick). */
	TSharedPtr<FJsonObject> CallNow(const FString& Tool, const TSharedPtr<FJsonObject>& Args, bool bDryRun = false);

	static bool Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Args, FString& OutError);

private:
	TMap<FString, FToolDef> Tools;
};
