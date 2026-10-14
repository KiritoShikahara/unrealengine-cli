#include "UnrealCliBridgeModule.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Server/BridgeLog.h"
#include "Server/BridgeServer.h"
#include "Server/ToolRegistry.h"
#include "Tools/ToolRegistration.h"

void FUnrealCliBridgeModule::StartupModule()
{
	FBridgeLog::Install();
	FToolRegistry& Registry = FToolRegistry::Get();
	UCli::RegisterAllTools(Registry);

	if (IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("NoUnrealCliBridge")))
	{
		return;
	}
	Server = MakeUnique<FBridgeServer>();
	Server->Start();
}

void FUnrealCliBridgeModule::ShutdownModule()
{
	if (Server)
	{
		Server->Stop();
		Server.Reset();
	}
	FBridgeLog::Uninstall();
}

IMPLEMENT_MODULE(FUnrealCliBridgeModule, UnrealCliBridge)
