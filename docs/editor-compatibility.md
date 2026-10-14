# Editor compatibility

| Engine | Plugin build | Automation tests | Editor E2E | Date |
|---|---|---|---|---|
| 5.5 | not verified (engine not installed) | - | - | 2026-10-14 |
| 5.6 | not verified (engine not installed) | - | - | 2026-10-14 |
| 5.7.4 | PASS | 14/14 PASS (1 with warnings) | 14/14 + game E2E PASS (-nullrhi and rendering) | 2026-10-14 |
| 5.8 | PASS | 14/14 PASS (2 with warnings) | 14/14 + game E2E PASS (-nullrhi and rendering) | 2026-10-14 |

Results are filled in by `scripts/e2e.sh <version>`; the latest run is recorded at the bottom of this file.

## Known limits

* `--null-rhi`: spawning actors with mesh primitives (StaticMeshActor, Blueprint actors) and starting PIE crash
  the engine (`EXCEPTION_INT_DIVIDE_BY_ZERO` in UnrealEditor-Engine). E2E therefore skips PIE unless
  `UNREALENGINE_CLI_E2E_RENDER=1` with a rendering editor.
* On some machines a Live Coding compile never finishes (the Live Coding console stops after starting UBA) and the
  engine blocks the editor's game thread while it waits. `code apply` gives up after 6 minutes; with `--force` it closes
  the editor, rebuilds and reopens it (with the level that was open), so the change still lands. Seen on the
  development machine with UE 5.7.4; `bAllowUBAExecutor=false` in the project's BuildConfiguration.xml did not help.
  Live Coding's lock is per engine install: while it runs, other builds with the same engine fail.
* Machines with an XGE / Incredibuild controller may never finish shader compilation; set
  `r.ShaderCompiler.AllowDistributedCompilation=0` under `[ConsoleVariables]` in the project's DefaultEngine.ini
  (`r.XGEShaderCompile` no longer exists in UE 5.7; `doctor` accepts either).

## Runs

* 2026-10-14 Windows 11, UE 5.7.4: build PASS, Automation 13/13, E2E 10/10.
* 2026-10-14 Windows 11, UE 5.8: build PASS, Automation 8/8 (before the Level/Asset/Runtime specs were added), E2E 10/10.
* 2026-10-15 after the parallel-clients fix: UE 5.7.4 and 5.8, -nullrhi and rendering, E2E 20/20, MCP 1/1, both game
  E2Es PASS; one intermittent failure seen once in six -nullrhi runs (e2e_18: a just-created Blueprint Interface was
  not found by the next call; not reproduced in 15 isolated attempts or 2 more full runs).
* 2026-10-15 after interfaces/SaveGame/property fixes: UE 5.7.4 and 5.8, -nullrhi and rendering, Automation 14/14,
  E2E 19/19, MCP 1/1, both game E2Es PASS.
* 2026-10-15 after UI/level-Blueprint/packaging-guard work: UE 5.7.4 and 5.8, -nullrhi and rendering, Automation 14/14,
  E2E 17/17 (menu UI clicks in PIE, Level Blueprint), MCP 1/1, both game E2Es PASS; packaging a build and starting the
  packaged game PASS on 5.7.4.
* 2026-10-15 robustness pass (plugin version check, Blueprint component props, input modifiers, editor close/exit fixes,
  Live Coding fallback): UE 5.7.4 and 5.8 with -nullrhi and rendering, Automation 14/14, E2E 15/15, MCP 1/1,
  both game E2Es PASS; C++ E2E (`scripts/e2e-cpp.py`) PASS on 5.7.4 through the rebuild fallback.
* 2026-10-14 after game-making tools (play-testing, gameplay nodes, AI, UMG layout/animation): UE 5.7.4 and 5.8,
  Automation 14/14, E2E 14/14 + MCP 1/1 with -nullrhi and with rendering, game E2E (`scripts/e2e-game.py`) PASS.
* 2026-10-14 v0.1.1: UE 5.7.4 and 5.8, Automation 14/14, E2E 11/11 with -nullrhi and with a rendering editor
  (`UNREALENGINE_CLI_E2E_RENDER=1`: Blueprint actor spawn + PIE until the BeginPlay log marker).
* 2026-10-14 final run after niagara_module_add: UE 5.7.4 Automation 14/14, E2E 11/11; UE 5.8 Automation 14/14, E2E 11/11.
