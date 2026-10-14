#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

struct FBridgeLogLine
{
	int64 Seq = 0;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FName Category;
	FString Text;
};

/** Ring buffer of recent editor log lines with monotonically increasing sequence numbers. */
class FBridgeLog : public FOutputDevice
{
public:
	static void Install();
	static void Uninstall();
	static FBridgeLog& Get();

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	/** Lines with Seq >= Since, at most Limit, at least MinVerbosity severe. */
	TArray<FBridgeLogLine> Read(int64 Since, int32 Limit, ELogVerbosity::Type MinVerbosity, const FString& CategoryFilter) const;
	int64 NextSeq() const;

	static const TCHAR* VerbosityName(ELogVerbosity::Type V);
	static ELogVerbosity::Type ParseVerbosity(const FString& Name);

private:
	mutable FCriticalSection Lock;
	TArray<FBridgeLogLine> Lines;
	int64 Next = 1;
	static constexpr int32 Capacity = 10000;
};
