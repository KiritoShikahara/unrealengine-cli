---
name: unrealengine-cli-usage
description: Use when an agent needs to drive Unreal Editor through unrealengine-cli: connecting, finding tools, calling them, reading errors.
version: 1.1.0
tools: []
---

# unrealengine-cli-usage

## When to use

Use when an agent needs to drive Unreal Editor through unrealengine-cli: connecting, finding tools, calling them, reading errors.

## Steps

1. Check the editor: `unrealengine-cli ping` (exit 3 = no editor: `unrealengine-cli editor open`; use `--headless` for no window, `--null-rhi` only for asset/graph work without PIE or meshes).
2. Find a tool: `unrealengine-cli tool find <words>`, then `unrealengine-cli tool describe <name>` for its schema.
3. Call it: `unrealengine-cli tool call <name> --args '{"key":"value"}'`, or use a typed command (`blueprint`, `level`, `asset`, `prop`, `pie`, `fab`, `test`, `code`).
4. Add `--output json` when you parse results; every command returns ok plus data or error {code, message, hint}.
5. Preview changes with `--dry-run`; undo editor changes with `unrealengine-cli tool call editor_undo`.
6. Read `warnings` in every result: "the editor runs UnrealCliBridge X but this CLI bundles Y" means the project's plugin is outdated (close the editor, `unrealengine-cli setup --build`, reopen).
7. For a broader check: `unrealengine-cli doctor`; for a bug report: `unrealengine-cli doctor --report`.

## Verify

`unrealengine-cli ping` returns `pong: true`.

## On failure

Read `error.code` and follow `error.hint`. Exit codes: 2 input, 3 connection, 4 editor, 5 timeout, 6 build. On Git Bash, package paths like /Game/... are restored automatically.
Repeated `tool.timeout`: the editor is busy or stuck; `unrealengine-cli editor close --force` ends it (unsaved work is lost), then `editor open`.
