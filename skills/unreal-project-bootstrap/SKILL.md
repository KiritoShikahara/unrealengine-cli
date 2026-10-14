---
name: unreal-project-bootstrap
description: Use when a project has no UnrealCliBridge plugin yet, or the editor must be built or started for the first time.
version: 1.0.0
tools: []
---

# unreal-project-bootstrap

## When to use

Use when a project has no UnrealCliBridge plugin yet, or the editor must be built or started for the first time.

## Steps

1. `unreal-cli doctor` to see engine, build tools and plugin state.
2. `unreal-cli setup --dry-run` to show the files it will add, then tell the user.
3. `unreal-cli setup --build` (Blueprint-only project: add `--add-code-module`).
4. `unreal-cli editor open --headless` and wait for it.

## Verify

`unreal-cli doctor` shows plugin and editor checks as ok.

## On failure

`build.failed` lists diagnostics with file and line: fix the first one. `engine.not_found`: install the engine or set UNREAL_CLI_ENGINE_DIR.
