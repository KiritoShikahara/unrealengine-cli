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
* Machines with an XGE / Incredibuild controller may never finish shader compilation; set
  `r.XGEShaderCompile=0` under `[ConsoleVariables]` in the project's DefaultEngine.ini.

## Runs

* 2026-10-14 Windows 11, UE 5.7.4: build PASS, Automation 13/13, E2E 10/10.
* 2026-10-14 Windows 11, UE 5.8: build PASS, Automation 8/8 (before the Level/Asset/Runtime specs were added), E2E 10/10.
* 2026-10-14 after game-making tools (play-testing, gameplay nodes, AI, UMG layout/animation): UE 5.7.4 and 5.8,
  Automation 14/14, E2E 14/14 + MCP 1/1 with -nullrhi and with rendering, game E2E (`scripts/e2e-game.py`) PASS.
* 2026-10-14 v0.1.1: UE 5.7.4 and 5.8, Automation 14/14, E2E 11/11 with -nullrhi and with a rendering editor
  (`UNREALENGINE_CLI_E2E_RENDER=1`: Blueprint actor spawn + PIE until the BeginPlay log marker).
* 2026-10-14 final run after niagara_module_add: UE 5.7.4 Automation 14/14, E2E 11/11; UE 5.8 Automation 14/14, E2E 11/11.
