---
name: unrealengine-cli-usage
description: Use whenever a request touches Unreal Engine (Blueprints, levels, assets, materials, C++, PIE). The editor is driven through this CLI, so do not tell the user Blueprints cannot be edited; connect, find tools, call them, read errors.
version: 1.3.0
tools: []
---

# unrealengine-cli-usage

## When to use

Use whenever a request touches Unreal Engine (Blueprints, levels, assets, materials, C++, PIE). The editor is driven through this CLI, so do not tell the user Blueprints cannot be edited; connect, find tools, call them, read errors.

## Steps

0. If `unrealengine-cli --version` fails, install it: Windows `irm https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.ps1 | iex`, macOS/Linux `curl -fsSL https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.sh | sh` (both verify SHA256SUMS). Then, in the project folder, ask the user before running `unrealengine-cli setup --build` (it copies the UnrealCliBridge plugin into the project and builds it).
1. Check the editor: `unrealengine-cli ping` (exit 3 = no editor: `unrealengine-cli editor open`, which shows the editor window so the user can watch the work; use `--headless` or `--null-rhi` only when the user asks for no window or in CI).
2. Find a tool: `unrealengine-cli tool find <words>`, then `unrealengine-cli tool describe <name>` for its schema.
3. Call it: `unrealengine-cli tool call <name> --args '{"key":"value"}'`, or use a typed command (`blueprint`, `level`, `asset`, `prop`, `pie`, `fab`, `test`, `code`).
4. Add `--output json` when you parse results; every command returns ok plus data or error {code, message, hint}.
5. Preview changes with `--dry-run`; undo editor changes with `unrealengine-cli tool call editor_undo`.
6. Read `warnings` in every result: "the editor runs UnrealCliBridge X but this CLI bundles Y" means the project's plugin is outdated (close the editor, `unrealengine-cli setup --build`, reopen).
7. For a broader check: `unrealengine-cli doctor`; for a bug report: `unrealengine-cli doctor --report`.
8. Claude Code: `unrealengine-cli hook install claude-code` adds a SessionStart hook (`hook session-start`) so new sessions in a UE project start with this context.

## Verify

`unrealengine-cli ping` returns `pong: true`.

## On failure

Read `error.code` and follow `error.hint`. Exit codes: 2 input, 3 connection, 4 editor, 5 timeout, 6 build. On Git Bash, package paths like /Game/... are restored automatically.
Repeated `tool.timeout`: the editor is busy or stuck; `unrealengine-cli editor close --force` ends it (unsaved work is lost), then `editor open`.
