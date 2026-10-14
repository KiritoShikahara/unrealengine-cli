---
name: unreal-blueprint-edit
description: Use when creating or changing Blueprint graphs: events, function calls, variables, components, compile and save.
version: 1.1.0
tools: [bp_create, bp_add_node, bp_connect, bp_set_pin, bp_compile, bp_save, bp_search_nodes, bp_graph_get, bp_add_component, bp_add_event_dispatcher, bp_add_timeline]
---

# unreal-blueprint-edit

## When to use

Use when creating or changing Blueprint graphs: events, function calls, variables, components, compile and save.

## Steps

1. `unrealengine-cli blueprint create /Game/Blueprints/BP_Name --parent Actor`.
2. Find nodes: `unrealengine-cli blueprint search-nodes PrintString`.
3. Add nodes: `unrealengine-cli blueprint add-node <path> --type Event --member BeginPlay` (ids come back as N<k>).
4. Wire: `unrealengine-cli blueprint connect <path> N1.then N2.execute`; literals: `unrealengine-cli blueprint set-pin <path> N2 InString Hello`.
5. Gameplay node types for `--type`:
   - Input: `InputAction --member /Game/Input/IA_Jump` (pins Triggered, Started, Completed, ActionValue).
   - Collisions: `ComponentEvent --member <component> --delegate OnComponentBeginOverlap` (compile after `bp_add_component` first).
   - Spawning and UI: `SpawnActor --member <actor class>`, `CreateWidget --member <widget Blueprint>`.
   - Events between Blueprints: `bp_add_event_dispatcher` + compile, then `CallDispatcher` / `BindDispatcher --member <name>`; a matching handler is `CustomEvent --member Handler --delegate <dispatcher>` (connect its `OutputDelegate` to the bind node's `Delegate`).
   - Animation over time: `bp_add_timeline` with float tracks (outputs Update, Finished, one pin per track).
   - Data: `MakeStruct` / `BreakStruct --member Vector|HitResult|...`, `SwitchEnum --member <enum>`, `Node --member K2Node_SwitchInteger|K2Node_Select|K2Node_MakeArray`.
   - Another object's variable: `VariableGet --member Health --member-parent BP_Enemy`.
6. `unrealengine-cli blueprint compile <path>`, then `unrealengine-cli blueprint save <path>`.

## Verify

`blueprint compile` returns `succeeded: true, errors: 0`.

## On failure

`blueprint.pin_not_found` lists the node's pins. `blueprint.compile_errors` on save: read the messages and fix them; never pass `--force` without telling the user.
