#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FSocket;
class FRunnableThread;

/** Accepts loopback connections and spawns one reader thread per client. */
class FBridgeServer : public FRunnable
{
public:
	bool Start();
	void Stop();
	int32 GetPort() const { return Port; }
	const FString& GetToken() const { return Token; }

	virtual uint32 Run() override;

private:
	FSocket* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	int32 Port = 0;
	FString Token;
	TAtomic<bool> bStopping{false};
};
