---
name: unreal-blueprint-edit
description: Use when creating or changing Blueprint graphs: events, function calls, variables, components, compile and save.
version: 1.0.0
tools: [bp_create, bp_add_node, bp_connect, bp_set_pin, bp_compile, bp_save, bp_search_nodes, bp_graph_get]
---

# unreal-blueprint-edit

## When to use

Use when creating or changing Blueprint graphs: events, function calls, variables, components, compile and save.

## Steps

1. `unreal-cli blueprint create /Game/Blueprints/BP_Name --parent Actor`.
2. Find nodes: `unreal-cli blueprint search-nodes PrintString`.
3. Add nodes: `unreal-cli blueprint add-node <path> --type Event --member BeginPlay` (ids come back as N<k>).
4. Wire: `unreal-cli blueprint connect <path> N1.then N2.execute`; literals: `unreal-cli blueprint set-pin <path> N2 InString Hello`.
5. `unreal-cli blueprint compile <path>`, then `unreal-cli blueprint save <path>`.

## Verify

`blueprint compile` returns `succeeded: true, errors: 0`.

## On failure

`blueprint.pin_not_found` lists the node's pins. `blueprint.compile_errors` on save: read the messages and fix them; never pass `--force` without telling the user.
