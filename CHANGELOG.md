# Changelog

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
