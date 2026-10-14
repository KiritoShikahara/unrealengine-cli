#pragma once

#include "CoreMinimal.h"

/** Writes and removes <data dir>/instances/<pid>.json so the CLI can discover this editor. */
namespace UCli::InstanceFile
{
	FString DataDir();
	FString PathForThisProcess();
	bool Write(int32 Port, const FString& Token);
	void Remove();
}
