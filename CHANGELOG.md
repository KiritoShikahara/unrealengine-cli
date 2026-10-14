# Changelog

## 0.1.0 (unreleased)

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
