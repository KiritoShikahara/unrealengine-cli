---
name: unreal-packaging
description: Use when packaging or cooking the project into a runnable build.
version: 1.1.0
tools: [package_project, cook_content, package_status]
---

# unreal-packaging

## When to use

Use when packaging or cooking the project into a runnable build.

## Steps

1. Save everything first (`unrealengine-cli level save --all`); Blueprints must compile.
2. Check the destination: `package_project` with `outputDir` (relative to the project, e.g. `Build/Windows`) and `check: true` returns the resolved folder without building. Engine installs and the Content folder are refused.
3. Start: `package_project` outputDir, configuration (Development or Shipping); it returns `pid` and `log`.
4. Poll `package_status` with the returned log until `succeeded` or `failed` (cooking can take minutes).
5. Smoke-test the build: run `<outputDir>/Windows/<Project>.exe` and check its `Saved/Logs` for "Engine is initialized".

## Verify

`package_status` reports `succeeded` and the executable exists under outputDir.

## On failure

Show the `errors` lines from `package_status`. Cooking stuck at "Waiting on job" for shaders: add `r.ShaderCompiler.AllowDistributedCompilation=0` under `[ConsoleVariables]` in Config/DefaultEngine.ini (`unrealengine-cli doctor` flags machines with XGE). "Package is unloadable ... newer custom version": assets saved by a newer engine are in Content; remove or resave them.
