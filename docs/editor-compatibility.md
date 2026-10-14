# Editor compatibility

| Engine | Plugin build | Automation tests | Editor E2E | Date |
|---|---|---|---|---|
| 5.5 | not verified (engine not installed) | - | - | 2026-10-14 |
| 5.6 | not verified (engine not installed) | - | - | 2026-10-14 |
| 5.7.4 | PASS | 13/13 PASS | 10/10 PASS (PIE skipped: -nullrhi) | 2026-10-14 |
| 5.8 | PASS | 8/8 PASS | 10/10 PASS (PIE skipped: -nullrhi) | 2026-10-14 |

Results are filled in by `scripts/e2e.sh <version>`; the latest run is recorded at the bottom of this file.

## Known limits

* `--null-rhi`: spawning actors with mesh primitives (StaticMeshActor, Blueprint actors) and starting PIE crash
  the engine (`EXCEPTION_INT_DIVIDE_BY_ZERO` in UnrealEditor-Engine). E2E therefore skips PIE unless
  `UNREAL_CLI_E2E_RENDER=1` with a rendering editor.
* Machines with an XGE / Incredibuild controller may never finish shader compilation; set
  `r.XGEShaderCompile=0` under `[ConsoleVariables]` in the project's DefaultEngine.ini.

## Runs

* 2026-10-14 Windows 11, UE 5.7.4: build PASS, Automation 13/13, E2E 10/10.
* 2026-10-14 Windows 11, UE 5.8: build PASS, Automation 8/8 (before the Level/Asset/Runtime specs were added), E2E 10/10.
