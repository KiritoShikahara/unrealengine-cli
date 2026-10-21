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
		RegisterRuntimeTools(Registry);
		RegisterCodeTools(Registry);
		RegisterUmgTools(Registry);
		RegisterDataTools(Registry);
		RegisterAnimGraphTools(Registry);
		RegisterNiagaraLandscapeTools(Registry);
		RegisterPlayTools(Registry);
		RegisterAiTools(Registry);
		RegisterStateTreeTools(Registry);
		RegisterFabTools(Registry);
		RegisterGasTools(Registry);
		RegisterPerfTools(Registry);
		RegisterLightTools(Registry);
		RegisterAnimAdvancedTools(Registry);
		RegisterPhysicsTools(Registry);
		RegisterLocTools(Registry);
		RegisterPythonTools(Registry);
	}
}
