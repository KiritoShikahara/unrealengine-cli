#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/Ticker.h"

namespace UCli { class FBridgeConnection; }

class FSocket;
class FRunnableThread;

/** Accepts loopback connections and spawns one reader thread per client. */
class FBridgeServer : public FRunnable
{
public:
	bool Start();
	void Stop();
	/** Engine exit: signal threads and remove the instance file without joining or freeing anything. */
	void StopForExit();
	int32 GetPort() const { return Port; }
	const FString& GetToken() const { return Token; }

	virtual uint32 Run() override;

private:
	FSocket* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	int32 Port = 0;
	FString Token;
	/** Shared with connection threads so they never outlive the flag they poll. */
	TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bStopping = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

	struct FConnectionThread
	{
		UCli::FBridgeConnection* Runnable = nullptr;
		FRunnableThread* Thread = nullptr;
	};
	/** Owned by the accept thread; finished ones are reaped there, the rest in Stop. */
	TArray<FConnectionThread> Connections;
	void Reap(bool bAll);
	/** Drains queued tool calls on the game thread between frames (never inside a world tick). */
	FTSTicker::FDelegateHandle DispatchTicker;
};
