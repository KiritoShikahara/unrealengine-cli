---
name: unreal-cpp-edit
description: Use when changing C++ code in the project and applying it to the running editor.
version: 1.0.0
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

## Verify

`code apply` returns `action: live` with `result: success`, or a rebuild without errors.

## On failure

`build.failed` and `code.compile_failed` include diagnostics; fix the first error. `editor.unsaved_changes`: save before a rebuild.
