---
name: unreal-lighting
description: Use when lighting a level or changing how the project renders: lighting presets (day, night, indoor), static lighting and HLOD builds, and rendering settings such as Lumen, virtual shadow maps, Nanite and anti-aliasing.
version: 1.0.0
tools: [light_preset, light_build, render_settings]
---

# unreal-lighting

## When to use

Use when lighting a level or changing how the project renders: lighting presets (day, night, indoor), static lighting and HLOD builds, and rendering settings such as Lumen, virtual shadow maps, Nanite and anti-aliasing.

## Steps

1. Light the open level: `unrealengine-cli light preset outdoor` (sun, sky atmosphere, sky light, height fog, clouds, post process), `night` or `indoor` (no sun: sky light, ceiling light, eye adaptation). `--keep` keeps the existing sun, sky and fog actors.
2. Look at the result: `pie start` and `pie screenshot`, or compare with a baseline: `visual compare <name>`.
3. Rendering settings: `unrealengine-cli render get`; `render set lumen true`, `render set virtualShadowMaps true`, `render set antiAliasing TSR`, `render set nanite true`, or any URendererSettings property name. The result says when an editor restart is needed.
4. Static lighting (projects with `staticLighting` on and static or stationary lights): `unrealengine-cli light build --quality preview|production`. Lumen projects need no build.
5. World Partition HLODs: save, close the editor, then `unrealengine-cli light hlod /Game/Maps/World` (minutes on large maps).

## Verify

`light preset` lists the spawned actors; a screenshot comparison with the previous look shows the change; `light build` returns the seconds it took.

## On failure

| Code | Meaning |
|---|---|
| `lighting.build_failed` | Static lighting is off (`render set staticLighting true` + restart) or there is nothing static to build |
| `lighting.build_in_progress` | Wait for the running build |
| `lighting.editor_running` | `light hlod` needs the editor closed |
| `render.unknown_setting` / `render.bad_value` | The hint lists the names; enum values are the engine's (e.g. TSR, Lumen) |
