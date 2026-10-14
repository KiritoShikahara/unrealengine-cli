#include "Server/BridgeServer.h"

#include "Async/Async.h"
#include "Compat/EngineCompat.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Server/InstanceFile.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Containers/Queue.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealCliBridge, Log, All);

using namespace UCli;

namespace
{
	TQueue<TSharedPtr<FToolCall, ESPMode::ThreadSafe>, EQueueMode::Mpsc>& PendingCalls()
	{
		static TQueue<TSharedPtr<FToolCall, ESPMode::ThreadSafe>, EQueueMode::Mpsc> Queue;
		return Queue;
	}

	constexpr uint32 MaxFrameBytes = 64u * 1024u * 1024u;

	bool RecvExact(FSocket* S, uint8* Out, int32 Len, const TAtomic<bool>& bStop)
	{
		int32 Done = 0;
		while (Done < Len)
		{
			if (bStop)
			{
				return false;
			}
			if (!S->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(250)))
			{
				if (S->GetConnectionState() != SCS_Connected)
				{
					return false;
				}
				continue;
			}
			int32 Read = 0;
			if (!S->Recv(Out + Done, Len - Done, Read) || Read <= 0)
			{
				return false;
			}
			Done += Read;
		}
		return true;
	}

	bool SendAll(FSocket* S, const uint8* Data, int32 Len)
	{
		int32 Sent = 0;
		while (Sent < Len)
		{
			int32 N = 0;
			if (!S->Send(Data + Sent, Len - Sent, N) || N <= 0)
			{
				return false;
			}
			Sent += N;
		}
		return true;
	}

	bool ReadFrame(FSocket* S, TSharedPtr<FJsonObject>& Out, const TAtomic<bool>& bStop)
	{
		uint8 Header[4];
		if (!RecvExact(S, Header, 4, bStop))
		{
			return false;
		}
		const uint32 Len = (uint32(Header[0]) << 24) | (uint32(Header[1]) << 16) | (uint32(Header[2]) << 8) | uint32(Header[3]);
		if (Len > MaxFrameBytes)
		{
			return false;
		}
		TArray<uint8> Body;
		Body.SetNumUninitialized(Len + 1);
		if (Len > 0 && !RecvExact(S, Body.GetData(), Len, bStop))
		{
			return false;
		}
		Body[Len] = 0;
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Len);
		Out = ParseObject(FString(Conv.Length(), Conv.Get()));
		return Out.IsValid();
	}

	bool WriteFrame(FSocket* S, const TSharedPtr<FJsonObject>& Obj)
	{
		const FTCHARToUTF8 Utf8(*ToJson(Obj));
		const uint32 Len = Utf8.Length();
		const uint8 Header[4] = {uint8(Len >> 24), uint8(Len >> 16), uint8(Len >> 8), uint8(Len)};
		return SendAll(S, Header, 4) && SendAll(S, reinterpret_cast<const uint8*>(Utf8.Get()), Len);
	}

	TSharedPtr<FJsonObject> ErrorObject(const FString& Code, const FString& Message, const FString& Hint = FString())
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("code"), Code);
		E->SetStringField(TEXT("message"), Message);
		if (!Hint.IsEmpty())
		{
			E->SetStringField(TEXT("hint"), Hint);
		}
		return E;
	}

	struct FPendingReply
	{
		FEventRef Event{EEventMode::ManualReset};
		FCriticalSection Lock;
		TSharedPtr<FJsonObject> Reply;
	};

}

namespace UCli
{
	/** One client connection, served on its own thread; tool calls hop to the game thread. */
	class FBridgeConnection : public FRunnable
	{
	public:
		FBridgeConnection(FSocket* InSocket, FString InToken, TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> InStop)
			: Socket(InSocket), Token(MoveTemp(InToken)), StopFlag(InStop), bStop(*InStop) {}

		virtual uint32 Run() override
		{
			Serve();
			Socket->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
			bFinished = true;
			return 0;
		}

		TAtomic<bool> bFinished{false};

		virtual void Exit() override {}

	private:
		void Serve()
		{
			TSharedPtr<FJsonObject> Hello;
			if (!ReadFrame(Socket, Hello, bStop))
			{
				return;
			}
			const FString Type = Hello->GetStringField(TEXT("type"));
			FString GivenToken;
			Hello->TryGetStringField(TEXT("token"), GivenToken);
			int32 Protocol = 0;
			Hello->TryGetNumberField(TEXT("protocol"), Protocol);
			if (Type != TEXT("hello") || GivenToken != Token)
			{
				Reject(ErrorObject(TEXT("connect.unauthorized"), TEXT("invalid session token"), TEXT("the instance file may be stale: unrealengine-cli instances list")));
				return;
			}
			TSharedPtr<FJsonObject> Welcome = MakeShared<FJsonObject>();
			Welcome->SetStringField(TEXT("type"), TEXT("welcome"));
			Welcome->SetNumberField(TEXT("protocol"), ProtocolVersion);
			Welcome->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
			Welcome->SetStringField(TEXT("pluginVersion"), PluginVersion());
			Welcome->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
			if (!WriteFrame(Socket, Welcome) || Protocol != ProtocolVersion)
			{
				return;
			}

			TSharedPtr<FJsonObject> Request;
			while (!bStop && ReadFrame(Socket, Request, bStop))
			{
				if (!WriteFrame(Socket, Handle(Request)))
				{
					return;
				}
			}
		}

		void Reject(const TSharedPtr<FJsonObject>& Error)
		{
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("type"), TEXT("reject"));
			R->SetObjectField(TEXT("error"), Error);
			WriteFrame(Socket, R);
		}

		TSharedPtr<FJsonObject> Handle(const TSharedPtr<FJsonObject>& Request)
		{
			FString Id;
			Request->TryGetStringField(TEXT("id"), Id);
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("type"), TEXT("result"));
			Result->SetStringField(TEXT("id"), Id);

			if (Request->GetStringField(TEXT("type")) != TEXT("call"))
			{
				Result->SetBoolField(TEXT("ok"), false);
				Result->SetObjectField(TEXT("error"), ErrorObject(TEXT("protocol.bad_request"), TEXT("expected a call frame")));
				return Result;
			}

			FToolCallRef Call = MakeShared<FToolCall, ESPMode::ThreadSafe>();
			Call->Tool = Request->GetStringField(TEXT("tool"));
			const TSharedPtr<FJsonObject>* Args = nullptr;
			Call->Args = Request->TryGetObjectField(TEXT("args"), Args) ? *Args : MakeShared<FJsonObject>();
			Request->TryGetBoolField(TEXT("dryRun"), Call->bDryRun);
			double TimeoutMs = 30000;
			Request->TryGetNumberField(TEXT("timeoutMs"), TimeoutMs);
			Call->TimeoutMs = static_cast<int32>(FMath::Clamp(TimeoutMs, 100.0, 86400000.0));

			TSharedRef<FPendingReply, ESPMode::ThreadSafe> Pending = MakeShared<FPendingReply, ESPMode::ThreadSafe>();
			Call->OnReply = [Pending](TSharedPtr<FJsonObject> Reply)
			{
				FScopeLock Guard(&Pending->Lock);
				Pending->Reply = Reply;
				Pending->Event->Trigger();
			};
			PendingCalls().Enqueue(Call);

			const uint32 WaitMs = static_cast<uint32>(Call->TimeoutMs);
			if (!Pending->Event->Wait(WaitMs))
			{
				Call->bCancelled = true;
				Result->SetBoolField(TEXT("ok"), false);
				Result->SetObjectField(TEXT("error"), ErrorObject(TEXT("tool.timeout"),
					FString::Printf(TEXT("'%s' did not finish within %d ms"), *Call->Tool, Call->TimeoutMs),
					TEXT("the editor may be busy; raise --timeout-ms or check for a modal dialog")));
				return Result;
			}
			FScopeLock Guard(&Pending->Lock);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& F : Pending->Reply->Values)
			{
				Result->SetField(F.Key, F.Value);
			}
			return Result;
		}

		FSocket* Socket;
		FString Token;
		TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> StopFlag;
		const TAtomic<bool>& bStop;
	};
}

bool FBridgeServer::Start()
{
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	Listener = Subsystem->CreateSocket(NAME_Stream, TEXT("UnrealCliBridge"), false);
	if (!Listener)
	{
		UE_LOG(LogUnrealCliBridge, Error, TEXT("Could not create listen socket"));
		return false;
	}
	TSharedRef<FInternetAddr> Addr = Subsystem->CreateInternetAddr();
	Addr->SetLoopbackAddress();
	const FString PortEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREALENGINE_CLI_PORT"));
	Addr->SetPort(PortEnv.IsEmpty() ? 0 : FCString::Atoi(*PortEnv));
	if (!Listener->Bind(*Addr) || !Listener->Listen(16))
	{
		UE_LOG(LogUnrealCliBridge, Error, TEXT("Could not listen on loopback"));
		Subsystem->DestroySocket(Listener);
		Listener = nullptr;
		return false;
	}
	Port = Listener->GetPortNo();
	Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	InstanceFile::Write(Port, Token);
	// Tool calls run from the core ticker, outside the world tick: a task-graph game-thread task can run in the
	// middle of a tick group, where switching levels or starting PIE trips engine assertions.
	DispatchTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		TSharedPtr<FToolCall, ESPMode::ThreadSafe> Next;
		while (PendingCalls().Dequeue(Next))
		{
			FToolRegistry::Get().Dispatch(Next.ToSharedRef());
		}
		return true;
	}));
	Thread = FRunnableThread::Create(this, TEXT("UnrealCliBridgeServer"));
	UE_LOG(LogUnrealCliBridge, Display, TEXT("UnrealCliBridge listening on 127.0.0.1:%d"), Port);
	return true;
}

void FBridgeServer::Stop()
{
	*bStopping = true;
	FTSTicker::GetCoreTicker().RemoveTicker(DispatchTicker);
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	if (Listener)
	{
		Listener->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Listener);
		Listener = nullptr;
	}
	Reap(true);
	InstanceFile::Remove();
}

void FBridgeServer::Reap(bool bAll)
{
	for (int32 I = Connections.Num() - 1; I >= 0; --I)
	{
		FConnectionThread& C = Connections[I];
		if (bAll || C.Runnable->bFinished)
		{
			C.Thread->WaitForCompletion();
			delete C.Thread;
			delete C.Runnable;
			Connections.RemoveAtSwap(I);
		}
	}
}

uint32 FBridgeServer::Run()
{
	while (!*bStopping)
	{
		Reap(false);
		bool bPending = false;
		if (!Listener->WaitForPendingConnection(bPending, FTimespan::FromMilliseconds(250)) || !bPending)
		{
			continue;
		}
		FSocket* Client = Listener->Accept(TEXT("UnrealCliClient"));
		if (!Client)
		{
			continue;
		}
		UCli::FBridgeConnection* Conn = new UCli::FBridgeConnection(Client, Token, bStopping);
		FRunnableThread* T = FRunnableThread::Create(Conn, TEXT("UnrealCliBridgeConnection"));
		if (!T)
		{
			delete Conn;
			continue;
		}
		Connections.Add({Conn, T});
	}
	return 0;
}
