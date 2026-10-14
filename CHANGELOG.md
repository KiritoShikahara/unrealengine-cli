# Changelog

## Unreleased

- Multiplayer PIE: `pie start --players <1-4> --net-mode standalone|listen|client`, `pie worlds`, and `--world server|client:<n>`
  for every pie command; actors report `role`, `remoteRole` and `owner`.

## 0.3.2

- Fix (#1): `fab` commands work with the Fab plugin shipped with UE 5.8 (no fixed `FabTab` tab). Fab is detected by its
  module, its page is found in any window and opened through Window > Fab; commands wait for the page to sign in.
- Fix: a Fab tab opened by a `fab` command is closed when the command ends. A Fab tab saved in the editor layout
  made UE 5.7 crash at startup without rendering (`-nullrhi`, Automation runs).
- `scripts/dev-build.sh` drops the plugin's intermediates when the engine version changes.
- Fix (#2): `python exec` code whose first line ends in `.py` is no longer run as a file path.

## 0.3.1

- Fab: `fab status|search|library|add` (tools `fab_status`, `fab_search`, `fab_library`, `fab_add`, `fab_page_run`).
  `fab add` downloads a listing from the signed-in user's library through the editor's Fab tab and imports it like
  Add to Project (asset packs for the running engine version, or glTF/GLB/FBX under `/Game/Fab`), then waits and saves.
  `--acquire-free` adds a free listing to the library first; paid listings are never bought.
- New skill `unreal-fab-assets`.
- Fix: a command run in a project whose editor is not running no longer drives another project's editor
  (it used to fall back to the only running editor, so `editor close` could close someone else's editor).
  Instance files caught mid-rewrite are re-read instead of reported as missing.

## 0.3.0

- The running editor's plugin version is checked on every command; an outdated plugin gives a warning with the update command.
- `prop get|set` reach Blueprint component templates (`/Game/BP.Component`, added or inherited) and collision/physics
  settings (CollisionProfileName, CollisionEnabled, GenerateOverlapEvents, SimulatePhysics, ...).
- Enhanced Input: `mapping_add` modifiers and triggers; `input_default_context_add`.
- Blueprint: `Node` takes `properties` (e.g. Get Subsystem's class); structs with native make/break functions use them.
- C++: Live Coding no longer blocks the editor and stops with `code.live_coding_stuck`; `code apply --rebuild`;
  `code apply --force` falls back to a rebuild; `editor close --force` ends a stuck editor and its child processes.
- Game E2E 2 (`scripts/e2e-game2.py`): a shooter with input mapping, projectiles and damage; C++ E2E (`scripts/e2e-cpp.py`).
- `pie_widgets` / `pie_click` (`pie widgets`, `pie click <Button>`): read and press on-screen UI in a play session.
- Level Blueprints: pass a map path to any `blueprint` command.
- `doctor` reports the running editor's plugin version; recommends `r.ShaderCompiler.AllowDistributedCompilation=0`
  (the old `r.XGEShaderCompile` no longer exists in UE 5.7).
- `code apply` reopens the level that was open after a rebuild.
- Blueprint Interfaces: `blueprint create --interface`, interface events in implementers, calls as messages.
- MCP clients receive CLI warnings as an extra text item.
- `prop set` takes asset paths in arrays (e.g. `OverrideMaterials`), sets physics flags on Blueprint component
  templates, and reports values the engine reverted (`prop.not_applied`).
- E2E: menu UI clicks, Level Blueprint, Blueprint Interfaces, SaveGame round trip.
- Fix (parallel agents): a CLI under load could delete a live editor's instance file, after which no command found
  the editor. Files are now removed only when the editor process is gone or the file is stale; the plugin rewrites
  its file every 10 s (recreating it if deleted), writes it atomically and accepts more pending connections.
- Fix: creating a Blueprint or AI asset right after a failed lookup could lose it to garbage collection (the lookup
  left an empty package); `blueprint.not_found` carries package diagnostics.
- Fixes: `package_project` with an empty outputDir archived into the engine's Binaries folder (now refused, as is
  any Unreal Engine install and the project's Content folder; relative paths are under the project; `check: true`
  validates without building); relative paths for `editor_screenshot` and `asset_import` resolve under the project; editor exit no longer crashes in the bridge; `editor close` waits for the process
  to exit (DLL locks) and ends an editor that hangs while shutting down; `--class` filters prefer an exact class
  name; `e2e.sh` clears test assets from earlier runs.

## 0.2.0

- Play-testing: `pie_start`, `pie_input` (keys and Enhanced Input actions), `pie_wait`, `pie_screenshot` (scene + UMG, `blank` flag),
  `pie_actors`, `pie_get`, `pie_call`, `pie_exec`; CLI `pie start|input|wait|screenshot|actors|get|call|exec`.
- Blueprint gameplay nodes: InputAction, ComponentEvent, SpawnActor, CreateWidget, CallDispatcher/BindDispatcher,
  MakeStruct/BreakStruct, SwitchEnum, Timeline, any node class (`Node`); `bp_add_event_dispatcher`, `bp_add_timeline`;
  another object's variables (`memberParent`), component variables, object/class pin values, pins resolved by direction.
- AI: `blackboard_create`, `blackboard_add_key`, `behavior_tree_create`, `bt_add_node`, `bt_get`, `nav_bounds_add`,
  `nav_build`, `nav_path`; `unreal-ai` skill.
- UMG: `widget_layout` (anchors, position, size, padding, alignment) and `widget_animation_add`.
- Game E2E (`scripts/e2e-game.py`): builds and plays a coin game with HUD and chasing AI through the CLI only.
- Fixes: tool calls run from the core ticker (switching levels mid-tick crashed the editor); `--null-rhi` adds
  `-RenderOffscreen`; widgets and widget animations get their variable GUIDs; `data_asset_create` refuses abstract
  classes; `prop set` accepts short enum names and `WorldSettings`.

## 0.1.1

- Command renamed to `unrealengine-cli` (environment variables `UNREALENGINE_CLI_*`).
- New tools: `landscape_layer_add`, `landscape_paint`, `niagara_input_set` (local value, parameter link or dynamic input).
- Installed binaries update themselves in the background once a day (`UNREALENGINE_CLI_NO_AUTO_UPDATE=1` or `CI` disables it).
- `pie_run` replies after the session has ended; empty log lines are no longer reported as errors.
- Install scripts: `UNREALENGINE_CLI_INSTALL_DIR`, `UNREALENGINE_CLI_NO_MODIFY_PATH`.

## 0.1.0

### CLI

- Single binary with a common JSON envelope (`--output json`) and exit codes 0/1/2/3/4/5/6.
- Editor discovery through instance files; selection by `--instance`, `--project`, working directory or `instances use`.
- `setup` (bundled plugin, `--dry-run`, `--add-code-module`, `--build`), `doctor`, `editor open|close|build|status`
  with `--headless` (offscreen) and `--null-rhi` modes; detached start without handle inheritance on Windows.
- Typed commands: `blueprint`, `level`, `asset`, `prop`, `pie`, `test`, `code`, `python`, `jobs`, `batch`,
  `logs` (including `--wait-for`), plus `tool list|find|describe|call` for all 113 tools (offline catalog).
- `code apply` chooses Live Coding (.cpp only) or close → rebuild → reopen in the original window mode.
- `skills install|refresh|uninstall|list|lint` for Claude Code, Codex, Cursor, Windsurf and VS Code; 20 bundled skills.
- `mcp serve` (stdio JSON-RPC) and `mcp configure <client>`.
- `reference search` over engine headers and the editor's Python stub.
- `self update` (SHA256SUMS verified) and `self update --check`; install scripts and release workflow.
- Git Bash path conversion of `/Game/...` arguments is undone.

### Editor plugin (UnrealCliBridge)

- Loopback TCP server with session token, protocol v1, schema validation, dry-run, undo transactions and jobs.
- 113 tools: system/editor/project, Blueprint graphs, levels and actors, assets, reflected properties and
  components, PIE and Automation tests, Live Coding, animation (AnimBP state machines, montages, blend spaces),
  UMG, materials, Niagara (systems, emitters, modules), landscapes, Enhanced Input, structs/enums/data tables,
  Sequencer, audio, packaging, Python.
- Verified on UE 5.7.4 and 5.8 (Windows): plugin build, 14 Automation tests, 11 editor E2E tests.
