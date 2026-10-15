#pragma once

#include "Server/ToolRegistry.h"

class AActor;
class UWorld;

namespace UCli
{
	void RegisterAllTools(FToolRegistry& Registry);

	void RegisterSystemTools(FToolRegistry& Registry);
	void RegisterProjectTools(FToolRegistry& Registry);
	void RegisterPythonTools(FToolRegistry& Registry);
	/** Loads the Python tool catalog once Python is initialized (called before every dispatch). */
	void EnsurePythonTools(FToolRegistry& Registry);
	/** True once the Python tool catalog has been registered. */
	bool ArePythonToolsReady();
	void RegisterBlueprintTools(FToolRegistry& Registry);
	void RegisterRuntimeTools(FToolRegistry& Registry);
	void RegisterCodeTools(FToolRegistry& Registry);
	void RegisterUmgTools(FToolRegistry& Registry);
	void RegisterDataTools(FToolRegistry& Registry);
	void RegisterAnimGraphTools(FToolRegistry& Registry);
	void RegisterNiagaraLandscapeTools(FToolRegistry& Registry);
	void RegisterPlayTools(FToolRegistry& Registry);
	void RegisterAiTools(FToolRegistry& Registry);
	void RegisterFabTools(FToolRegistry& Registry);
	void RegisterGasTools(FToolRegistry& Registry);
	void RegisterPerfTools(FToolRegistry& Registry);
	void RegisterLightTools(FToolRegistry& Registry);
	void RegisterAnimAdvancedTools(FToolRegistry& Registry);
	void RegisterPhysicsTools(FToolRegistry& Registry);

	/** The play world named by the call's optional "world" argument, or fails the call. */
	UWorld* ResolvePlayWorld(const FToolCallRef& Call);
	/** The play-world actor named by the call's "actor" argument (label, name, class or "player"), or fails the call. */
	AActor* ResolvePlayActor(const FToolCallRef& Call);
}
