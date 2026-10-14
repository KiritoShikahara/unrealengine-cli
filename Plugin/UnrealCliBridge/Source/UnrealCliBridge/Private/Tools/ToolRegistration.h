#pragma once

class FToolRegistry;

namespace UCli
{
	void RegisterAllTools(FToolRegistry& Registry);

	void RegisterSystemTools(FToolRegistry& Registry);
	void RegisterProjectTools(FToolRegistry& Registry);
	void RegisterPythonTools(FToolRegistry& Registry);
	/** Loads the Python tool catalog once Python is initialized (called before every dispatch). */
	void EnsurePythonTools(FToolRegistry& Registry);
	void RegisterBlueprintTools(FToolRegistry& Registry);
	void RegisterRuntimeTools(FToolRegistry& Registry);
	void RegisterCodeTools(FToolRegistry& Registry);
	void RegisterUmgTools(FToolRegistry& Registry);
	void RegisterDataTools(FToolRegistry& Registry);
	void RegisterAnimGraphTools(FToolRegistry& Registry);
	void RegisterNiagaraLandscapeTools(FToolRegistry& Registry);
}
