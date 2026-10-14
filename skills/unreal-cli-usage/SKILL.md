---
name: unreal-cli-usage
description: Use when an agent needs to drive Unreal Editor through unreal-cli: connecting, finding tools, calling them, reading errors.
version: 1.0.0
tools: []
---

# unreal-cli-usage

## When to use

Use when an agent needs to drive Unreal Editor through unreal-cli: connecting, finding tools, calling them, reading errors.

## Steps

1. Check the editor: `unreal-cli ping` (exit 3 = no editor: `unreal-cli editor open --headless`).
2. Find a tool: `unreal-cli tool find <words>`, then `unreal-cli tool describe <name>` for its schema.
3. Call it: `unreal-cli tool call <name> --args '{"key":"value"}'`, or use a typed command (`blueprint`, `level`, `asset`, `prop`, `pie`, `test`, `code`).
4. Add `--output json` when you parse results; every command returns ok plus data or error {code, message, hint}.
5. Preview changes with `--dry-run`; undo editor changes with `unreal-cli tool call editor_undo`.

## Verify

`unreal-cli ping` returns `pong: true`.

## On failure

Read `error.code` and follow `error.hint`. Exit codes: 2 input, 3 connection, 4 editor, 5 timeout, 6 build. On Git Bash, package paths like /Game/... are restored automatically.
