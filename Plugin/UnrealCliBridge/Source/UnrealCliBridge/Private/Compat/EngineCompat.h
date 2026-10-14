#pragma once

#include "Runtime/Launch/Resources/Version.h"

// Version switches for UE 5.5 - 5.8 live here so tool code stays branch-free.
#define UCLI_UE_AT_LEAST(Major, Minor) \
	((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

static_assert(UCLI_UE_AT_LEAST(5, 5), "UnrealCliBridge supports UE 5.5 or newer");

namespace UCli
{
	constexpr int32 ProtocolVersion = 1;
	inline const TCHAR* PluginVersion() { return TEXT("0.1.0"); }
}
