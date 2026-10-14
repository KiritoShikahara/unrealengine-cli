#include "Tools/ToolRegistration.h"

#include "Server/JobManager.h"
#include "Server/ToolRegistry.h"

namespace UCli
{
	void RegisterAllTools(FToolRegistry& Registry)
	{
		FJobManager::RegisterTools(Registry);
		RegisterSystemTools(Registry);
		RegisterProjectTools(Registry);
		RegisterBlueprintTools(Registry);
		RegisterPythonTools(Registry);
	}
}
