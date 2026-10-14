#include "Server/InstanceFile.h"

#include "Compat/EngineCompat.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Server/JsonUtil.h"

namespace UCli::InstanceFile
{
	FString DataDir()
	{
		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREALENGINE_CLI_HOME"));
		if (!Override.IsEmpty())
		{
			return Override;
		}
#if PLATFORM_WINDOWS
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("unrealengine-cli"));
#elif PLATFORM_MAC
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Library/Application Support/unrealengine-cli"));
#else
		const FString Xdg = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_DATA_HOME"));
		return Xdg.IsEmpty()
			? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".local/share/unrealengine-cli"))
			: FPaths::Combine(Xdg, TEXT("unrealengine-cli"));
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
		static const FString StartedAt = FDateTime::UtcNow().ToIso8601();
		O->SetStringField(TEXT("startedAt"), StartedAt);
		// How the editor was launched, so tools that restart it can reuse the same mode.
		const TCHAR* Cmd = FCommandLine::Get();
		O->SetStringField(TEXT("mode"), FParse::Param(Cmd, TEXT("nullrhi")) ? TEXT("nullrhi") : (FParse::Param(Cmd, TEXT("RenderOffscreen")) ? TEXT("headless") : TEXT("normal")));
		const FString Path = PathForThisProcess();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		// Write then rename, so a reader never sees a half-written file.
		const FString Temp = Path + TEXT(".tmp");
		return FFileHelper::SaveStringToFile(ToJson(O), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			&& IFileManager::Get().Move(*Path, *Temp, true, true);
	}

	void Remove()
	{
		IFileManager::Get().Delete(*PathForThisProcess(), false, true, true);
	}
}
