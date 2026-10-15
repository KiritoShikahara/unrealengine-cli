---
name: unreal-audio-physics-pcg
description: Use when building procedural audio with MetaSounds, destructibles with Chaos Geometry Collections, or procedural placement with PCG graphs.
version: 1.0.0
tools: [metasound_create, metasound_add_node, metasound_connect, metasound_set_input, geometry_collection_create, pcg_create, pcg_add_node, pcg_connect, pcg_generate, pcg_status]
---

# unreal-audio-physics-pcg

## When to use

Use when building procedural audio with MetaSounds, destructibles with Chaos Geometry Collections, or procedural placement with PCG graphs.

## Steps

1. MetaSound: `unrealengine-cli audio metasound create /Game/Audio/MS_Tone` prints the graph outputs (e.g. `UE.OutputFormat.Mono.Audio:0`). Add nodes by class: `audio metasound node /Game/Audio/MS_Tone UE.Sine.Audio` prints the node id and pins. Connect `audio metasound connect /Game/Audio/MS_Tone <id>:Audio graph:UE.OutputFormat.Mono.Audio:0` (graph:<name> is the MetaSound's own input or output); set values with `audio metasound set /Game/Audio/MS_Tone <id> Frequency 440`.
2. Destructible: `unrealengine-cli physics collection /Game/FX/GC_Crate --mesh /Engine/BasicShapes/Cube`; fracture it in the editor's Fracture mode (Fracture plugin), which has no scripting interface.
3. PCG: `unrealengine-cli pcg create /Game/PCG/PCG_Forest`, add nodes with `pcg node /Game/PCG/PCG_Forest CreatePointsGrid --props '{"grid_extents":[500,500,0],"cell_size":[100,100,100]}'` and `pcg node ... StaticMeshSpawner --props '{"mesh":"/Game/Meshes/Tree"}'`, connect `pcg connect ... CreatePointsGrid_0:Out StaticMeshSpawner_0:In` and `... StaticMeshSpawner_0:Out output:Out`, then `pcg generate /Game/PCG/PCG_Forest --extent 1000`.

## Verify

`pcg generate` returns the spawned `instances`; MetaSound commands return `connected: true` and the set value; play the MetaSound in the editor to listen.

## On failure

| Code | Meaning |
|---|---|
| `metasound.no_class` | Class names are Namespace.Name.Variant as in the MetaSound editor, e.g. UE.Sine.Audio |
| `metasound.failed` | Usually mismatched pin types (Audio vs Float) |
| `pcg.no_settings` / `pcg.no_pin` | Use the node type and pin labels printed by `pcg node` |
| `pcg.timeout` | The graph is still generating; large volumes need a larger `--timeout` |
