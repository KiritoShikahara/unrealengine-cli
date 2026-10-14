---
name: unreal-level-build
description: Use when building or changing a level: creating maps, spawning, moving, attaching or deleting actors.
version: 1.0.0
tools: [level_new, actor_spawn, actor_transform, actor_list, level_save]
---

# unreal-level-build

## When to use

Use when building or changing a level: creating maps, spawning, moving, attaching or deleting actors.

## Steps

1. `unrealengine-cli level new /Game/Maps/L_Name` or `unrealengine-cli level open <path>`.
2. `unrealengine-cli level actor spawn PointLight --location 0,0,300 --label Key`.
3. `unrealengine-cli level actor transform Key --rotation 0,90,0`.
4. `unrealengine-cli level actor list --name Key` to check.
5. `unrealengine-cli level save`.

## Verify

`level actor list` shows the actors with the expected transforms.

## On failure

`actor.not_found` suggests close labels. Undo a wrong change with `unrealengine-cli tool call editor_undo`.
