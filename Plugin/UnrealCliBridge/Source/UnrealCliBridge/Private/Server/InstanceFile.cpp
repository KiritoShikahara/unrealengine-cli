#include "Server/InstanceFile.h"

#include "Compat/EngineCompat.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Server/JsonUtil.h"

namespace UCli::InstanceFile
{
	FString DataDir()
	{
		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_CLI_HOME"));
		if (!Override.IsEmpty())
		{
			return Override;
		}
#if PLATFORM_WINDOWS
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("unreal-cli"));
#elif PLATFORM_MAC
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Library/Application Support/unreal-cli"));
#else
		const FString Xdg = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_DATA_HOME"));
		return Xdg.IsEmpty()
			? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".local/share/unreal-cli"))
			: FPaths::Combine(Xdg, TEXT("unreal-cli"));
#endif
	}

	FString PathForThisProcess()
	{
		return FPaths::Combine(DataDir(), TEXT("instances"), FString::Printf(TEXT("%u.json"), FPlatformProcess::GetCurrentProcessId()));
	}

	bool Write(int32 Port, const FString& Token)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
		O->SetNumberField(TEXT("port"), Port);
		O->SetStringField(TEXT("token"), Token);
		O->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
		O->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
		O->SetStringField(TEXT("pluginVersion"), PluginVersion());
		O->SetNumberField(TEXT("protocolVersion"), ProtocolVersion);
		O->SetStringField(TEXT("startedAt"), FDateTime::UtcNow().ToIso8601());
		const FString Path = PathForThisProcess();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(ToJson(O), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void Remove()
	{
		IFileManager::Get().Delete(*PathForThisProcess(), false, true, true);
	}
}
