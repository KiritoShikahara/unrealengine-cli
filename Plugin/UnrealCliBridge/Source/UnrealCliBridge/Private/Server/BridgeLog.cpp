#include "Server/BridgeLog.h"

#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

static FBridgeLog* GBridgeLog = nullptr;

void FBridgeLog::Install()
{
	if (!GBridgeLog)
	{
		GBridgeLog = new FBridgeLog();
		GLog->AddOutputDevice(GBridgeLog);
	}
}

void FBridgeLog::Uninstall()
{
	if (GBridgeLog)
	{
		GLog->RemoveOutputDevice(GBridgeLog);
		delete GBridgeLog;
		GBridgeLog = nullptr;
	}
}

FBridgeLog& FBridgeLog::Get()
{
	Install();
	return *GBridgeLog;
}

void FBridgeLog::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock Guard(&Lock);
	FBridgeLogLine Line;
	Line.Seq = Next++;
	Line.Verbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	Line.Category = Category;
	Line.Text = V;
	if (Lines.Num() >= Capacity)
	{
		Lines.RemoveAt(0, Capacity / 10, EAllowShrinking::No);
	}
	Lines.Add(MoveTemp(Line));
}

TArray<FBridgeLogLine> FBridgeLog::Read(int64 Since, int32 Limit, ELogVerbosity::Type MinVerbosity, const FString& CategoryFilter) const
{
	FScopeLock Guard(&Lock);
	TArray<FBridgeLogLine> Out;
	for (const FBridgeLogLine& L : Lines)
	{
		if (L.Seq < Since || L.Verbosity > MinVerbosity)
		{
			continue;
		}
		if (!CategoryFilter.IsEmpty() && !L.Category.ToString().Contains(CategoryFilter))
		{
			continue;
		}
		Out.Add(L);
	}
	if (Limit > 0 && Out.Num() > Limit)
	{
		Out.RemoveAt(0, Out.Num() - Limit);
	}
	return Out;
}

int64 FBridgeLog::NextSeq() const
{
	FScopeLock Guard(&Lock);
	return Next;
}

const TCHAR* FBridgeLog::VerbosityName(ELogVerbosity::Type V)
{
	switch (V)
	{
	case ELogVerbosity::Fatal: return TEXT("fatal");
	case ELogVerbosity::Error: return TEXT("error");
	case ELogVerbosity::Warning: return TEXT("warning");
	case ELogVerbosity::Display: return TEXT("display");
	case ELogVerbosity::Log: return TEXT("log");
	default: return TEXT("verbose");
	}
}

ELogVerbosity::Type FBridgeLog::ParseVerbosity(const FString& Name)
{
	if (Name == TEXT("error")) return ELogVerbosity::Error;
	if (Name == TEXT("warning")) return ELogVerbosity::Warning;
	if (Name == TEXT("display")) return ELogVerbosity::Display;
	if (Name == TEXT("verbose")) return ELogVerbosity::VeryVerbose;
	return ELogVerbosity::Log;
}
