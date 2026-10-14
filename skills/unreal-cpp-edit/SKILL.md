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

1. Find code: `unreal-cli code search <text>`; read: `unreal-cli code read Source/Mod/File.cpp --start 1 --end 80`.
2. Edit: `unreal-cli code edit <file> --old '<exact text>' --new '<text>'`.
3. New class: `unreal-cli code class-create Enemy --parent Actor`.
4. Apply: `unreal-cli code apply --plan`, then `unreal-cli code apply` (Live Coding for .cpp, rebuild for headers).

## Verify

`code apply` returns `action: live` with `result: success`, or a rebuild without errors.

## On failure

`build.failed` and `code.compile_failed` include diagnostics; fix the first error. `editor.unsaved_changes`: save before a rebuild.
