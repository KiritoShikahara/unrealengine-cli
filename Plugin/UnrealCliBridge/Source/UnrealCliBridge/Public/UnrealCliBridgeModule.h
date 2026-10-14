#pragma once

#include "Modules/ModuleInterface.h"

class FBridgeServer;

class FUnrealCliBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FBridgeServer> Server;
};
