---
name: unreal-cli-extend
description: Use when adding a new tool (Python or C++), a typed command or a bundled skill to unrealengine-cli itself, in a checkout of its repository.
version: 1.0.0
tools: [tools_list]
---

# unreal-cli-extend

## When to use

Use when adding a new tool (Python or C++), a typed command or a bundled skill to unrealengine-cli itself, in a checkout of its repository. For one-off editor work, call `python_exec` instead of adding a tool.

## Steps

1. Pick the language: Python (`Plugin/UnrealCliBridge/Content/Python/unreal_cli_bridge/tools/<group>.py`) when the `unreal` Python API covers it; C++ (`Plugin/UnrealCliBridge/Source/UnrealCliBridge/Private/Tools/<Group>Tools.cpp`) for editor graphs, editor-only modules or anything Python cannot reach. Grep the engine header (`unrealengine-cli reference search <Symbol>`) before using an engine API.
2. Write the test first: a mock-bridge test in `tests/usNN_<area>.rs` for a typed command, and an E2E case in `tests/e2e_editor.rs` that calls the tool against `TestProject`. Run it and see it fail.
3. Python tool: decorate a function with `@tool("<group>_<verb>", "<group>", "<one-line description>", {<JSON schema properties>}, required=[...], mutating=True)` (`mutating` for anything that changes assets or the level), raise `ToolError("<group>.<reason>", message, hint)` for expected failures, return a dict. A new module is imported in `tools/__init__.py`.
4. C++ tool: in `Register<Group>Tools`, `R.Add(name, group, description, schema, EToolFlags::Mutating or None, lambda)`; read args with `Call->GetString` / `GetObject` / `GetArray`, finish with `Call->Succeed(json)` or `Call->Fail(code, message, hint)`. A new file declares its `Register*` in `ToolRegistration.h` and is called from `ToolRegistration.cpp`; a new engine module goes in `UnrealCliBridge.Build.cs` (and an optional plugin in `UnrealCliBridge.uplugin` with `Optional: true`). Never put `)"` inside `TEXT(R"(...)")`.
5. Restart the editor (`unrealengine-cli editor close`, then `unrealengine-cli editor open`): tools register at startup only, Live Coding does not add them.
6. Typed command (optional, for frequent tools): add a subcommand in `src/commands/` that forwards to the tool, and wire it in `src/cli.rs`.
7. Regenerate the offline catalog: `bash scripts/gen-catalog.sh` (editor running), then update the tool count in `README.md` and `README.ja.md`.
8. Skill: `skills/<name>/SKILL.md` with frontmatter `name` (= directory), `description` (starts with "Use when", at most 300 characters), `version`, `tools: [...]` (catalog names only) and the sections When to use, Steps, Verify, On failure. Bump `version` when you change a skill.

## Verify

`cargo fmt --all -- --check`, `cargo clippy --all-targets -- -D warnings`, `cargo test --all-targets` and `unrealengine-cli skills lint --severity error` pass; the new tool shows in `unrealengine-cli tool describe <name>` and its E2E case passes (`bash scripts/e2e.sh 5.7`).

## On failure

Tool missing after restart: the Python module is not imported in `tools/__init__.py`, or the C++ `Register*` is not called; check `unrealengine-cli logs --severity error`. `unknown-tool` from lint: regenerate the catalog. The docs test fails on the tool count: update both READMEs. Build fails while another editor of the same engine has Live Coding on: close it or use another engine version.
