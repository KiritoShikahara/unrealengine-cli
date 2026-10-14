#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Server/JsonUtil.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

using namespace UCli;

namespace
{
	FString ConfigFile(const FString& Name)
	{
		if (Name == TEXT("Game")) return GGameIni;
		if (Name == TEXT("Input")) return GInputIni;
		if (Name == TEXT("Editor")) return GEditorIni;
		return GEngineIni;
	}
}

void UCli::RegisterProjectTools(FToolRegistry& R)
{
	R.Add(TEXT("project_info"), TEXT("project"), TEXT("Project name, paths and modules."),
		TEXT(R"({"type":"object","properties":{}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), FApp::GetProjectName());
			O->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
			O->SetStringField(TEXT("dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			O->SetStringField(TEXT("contentDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
			TArray<FString> Modules;
			if (const FProjectDescriptor* P = IProjectManager::Get().GetCurrentProject())
			{
				for (const FModuleDescriptor& M : P->Modules)
				{
					Modules.Add(M.Name.ToString());
				}
			}
			O->SetArrayField(TEXT("modules"), StrArray(Modules));
			O->SetBoolField(TEXT("hasCode"), Modules.Num() > 0);
			Call->Succeed(O);
		});

	R.Add(TEXT("project_settings_get"), TEXT("project"), TEXT("Read a config value (file: Engine|Game|Input|Editor)."),
		TEXT(R"({"type":"object","properties":{"file":{"type":"string","enum":["Engine","Game","Input","Editor"]},"section":{"type":"string"},"key":{"type":"string"}},"required":["section","key"]})"),
		EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			TArray<FString> Values;
			GConfig->GetArray(*Call->GetString(TEXT("section")), *Call->GetString(TEXT("key")), Values, ConfigFile(Call->GetString(TEXT("file"), TEXT("Engine"))));
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("found"), Values.Num() > 0);
			O->SetStringField(TEXT("value"), Values.Num() > 0 ? Values.Last() : FString());
			O->SetArrayField(TEXT("values"), StrArray(Values));
			Call->Succeed(O);
		});

	R.Add(TEXT("project_settings_set"), TEXT("project"), TEXT("Write a config value and flush it to the project's Default*.ini."),
		TEXT(R"({"type":"object","properties":{"file":{"type":"string","enum":["Engine","Game","Input","Editor"]},"section":{"type":"string"},"key":{"type":"string"},"value":{"type":"string"}},"required":["section","key","value"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString File = Call->GetString(TEXT("file"), TEXT("Engine"));
			const FString DefaultIni = FPaths::Combine(FPaths::ProjectConfigDir(), FString::Printf(TEXT("Default%s.ini"), *File));
			GConfig->SetString(*Call->GetString(TEXT("section")), *Call->GetString(TEXT("key")), *Call->GetString(TEXT("value")), ConfigFile(File));
			GConfig->SetString(*Call->GetString(TEXT("section")), *Call->GetString(TEXT("key")), *Call->GetString(TEXT("value")), DefaultIni);
			GConfig->Flush(false, DefaultIni);
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("file"), FPaths::ConvertRelativePathToFull(DefaultIni));
			Call->Succeed(O);
		});

	R.Add(TEXT("plugin_list"), TEXT("project"), TEXT("List plugins (enabled only unless all=true)."),
		TEXT(R"({"type":"object","properties":{"all":{"type":"boolean"},"filter":{"type":"string"}}})"), EToolFlags::None,
		[](const FToolCallRef& Call)
		{
			const bool bAll = Call->GetBool(TEXT("all"));
			const FString Filter = Call->GetString(TEXT("filter"));
			TArray<TSharedRef<IPlugin>> Plugins = bAll ? IPluginManager::Get().GetDiscoveredPlugins() : IPluginManager::Get().GetEnabledPlugins();
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const TSharedRef<IPlugin>& P : Plugins)
			{
				if (!Filter.IsEmpty() && !P->GetName().Contains(Filter))
				{
					continue;
				}
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), P->GetName());
				O->SetBoolField(TEXT("enabled"), P->IsEnabled());
				O->SetStringField(TEXT("version"), P->GetDescriptor().VersionName);
				Items.Add(Obj(O));
			}
			Call->Succeed(Arr(Items));
		});

	R.Add(TEXT("plugin_enable"), TEXT("project"), TEXT("Enable or disable a plugin in the .uproject (takes effect after restart)."),
		TEXT(R"({"type":"object","properties":{"name":{"type":"string"},"enabled":{"type":"boolean"}},"required":["name"]})"),
		EToolFlags::Mutating | EToolFlags::NoUndo,
		[](const FToolCallRef& Call)
		{
			const FString Name = Call->GetString(TEXT("name"));
			if (!IPluginManager::Get().FindPlugin(Name).IsValid())
			{
				Call->Fail(TEXT("plugin.not_found"), FString::Printf(TEXT("no plugin named '%s'"), *Name), TEXT("list plugins: plugin_list all=true"));
				return;
			}
			FText Reason;
			const bool bEnable = Call->GetBool(TEXT("enabled"), true);
			if (!IProjectManager::Get().SetPluginEnabled(Name, bEnable, Reason) || !IProjectManager::Get().SaveCurrentProjectToDisk(Reason))
			{
				Call->Fail(TEXT("plugin.update_failed"), Reason.ToString());
				return;
			}
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), Name);
			O->SetBoolField(TEXT("enabled"), bEnable);
			O->SetBoolField(TEXT("restartRequired"), true);
			Call->Succeed(O);
		});
}
