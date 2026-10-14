#pragma once

class FToolRegistry;

namespace UCli
{
	void RegisterAllTools(FToolRegistry& Registry);

	void RegisterSystemTools(FToolRegistry& Registry);
	void RegisterProjectTools(FToolRegistry& Registry);
	void RegisterPythonTools(FToolRegistry& Registry);
	void RegisterBlueprintTools(FToolRegistry& Registry);
}
