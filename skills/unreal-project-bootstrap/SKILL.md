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

1. `unrealengine-cli doctor` to see engine, build tools and plugin state.
2. `unrealengine-cli setup --dry-run` to show the files it will add, then tell the user.
3. `unrealengine-cli setup --build` (Blueprint-only project: add `--add-code-module`).
4. `unrealengine-cli editor open --headless` and wait for it.

## Verify

`unrealengine-cli doctor` shows plugin and editor checks as ok.

## On failure

`build.failed` lists diagnostics with file and line: fix the first one. `engine.not_found`: install the engine or set UNREALENGINE_CLI_ENGINE_DIR.
