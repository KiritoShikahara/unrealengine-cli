#pragma once

#include "Runtime/Launch/Resources/Version.h"
#include "Interfaces/IPluginManager.h"

// Version switches for UE 5.5 - 5.8 live here so tool code stays branch-free.
#define UCLI_UE_AT_LEAST(Major, Minor) \
	((ENGINE_MAJOR_VERSION > (Major)) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

static_assert(UCLI_UE_AT_LEAST(5, 5), "UnrealCliBridge supports UE 5.5 or newer");

namespace UCli
{
	constexpr int32 ProtocolVersion = 1;
	/** VersionName from UnrealCliBridge.uplugin (the CLI compares it with the plugin it bundles). */
	inline FString PluginVersion()
	{
		static const FString Version = []
		{
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealCliBridge"));
			return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString(TEXT("unknown"));
		}();
		return Version;
	}
}
