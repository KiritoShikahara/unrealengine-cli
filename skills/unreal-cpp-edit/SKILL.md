---
name: unreal-cpp-edit
description: Use when changing C++ code in the project and applying it to the running editor.
version: 1.1.0
tools: [code_live_compile]
---

# unreal-cpp-edit

## When to use

Use when changing C++ code in the project and applying it to the running editor.

## Steps

1. Find code: `unrealengine-cli code search <text>`; read: `unrealengine-cli code read Source/Mod/File.cpp --start 1 --end 80`.
2. Edit: `unrealengine-cli code edit <file> --old '<exact text>' --new '<text>'`.
3. New class: `unrealengine-cli code class-create Enemy --parent Actor`.
4. Apply: `unrealengine-cli code apply --plan`, then `unrealengine-cli code apply` (Live Coding for .cpp, rebuild for headers).
5. After deleting or renaming source files: `unrealengine-cli code apply --rebuild` (deletions are not detected as changes).
6. Use the new class from Blueprints: `unrealengine-cli blueprint create /Game/BP_X --parent /Script/<Module>.<Class>`; check it in PIE with `pie call <actor> "<Function> <args>"` and `pie get <actor> <Property>`.

## Verify

`code apply` returns `action: live` with `result: success`, or a rebuild without errors.

## On failure

`build.failed` and `code.compile_failed` include diagnostics; fix the first error. `editor.unsaved_changes`: save before a rebuild.
`code.live_coding_stuck` (Live Coding never finished): `unrealengine-cli code apply --rebuild --force` closes the editor, rebuilds and reopens it; `code apply --force` falls back to that automatically. A stuck editor that does not answer: `unrealengine-cli editor close --force`.
Builds failing with "Unable to build while Live Coding is active": an editor of the same engine version has Live Coding running; close it.
