---
name: unreal-ai
description: Use when giving characters AI: Blackboards, Behavior Trees, AI controllers and navigation meshes.
version: 1.1.0
tools: [blackboard_create, blackboard_add_key, blackboard_get, behavior_tree_create, bt_add_node, bt_get, bt_set_node_property, bt_remove_node, nav_bounds_add, nav_build, nav_path]
---

# unreal-ai

## When to use

Use when giving characters AI: Blackboards, Behavior Trees, AI controllers and navigation meshes.

## Steps

1. `blackboard_create` path, keys `[{name, type: Bool|Int|Float|Vector|Rotator|Object|Class|Name|String|Enum, baseClass}]` (e.g. `{name: Target, type: Object, baseClass: Actor}`).
2. `behavior_tree_create` path, blackboard.
3. Build the tree top-down with `bt_add_node` (each call returns the node `id`):
   1. Under `parent: root` add one composite: `kind: composite, class: Selector` (or Sequence, SimpleParallel).
   2. Add children in execution order: `kind: task, class: MoveTo, properties: {BlackboardKey: Target, AcceptableRadius: 100}`, `Wait`, `RotateToFaceBBEntry`, or a Blueprint task path.
   3. Attach conditions and periodic logic: `kind: decorator, class: Blackboard, properties: {BlackboardKey: CanSee}`, `kind: service, class: DefaultFocus`.
   4. Value-or-key properties take a value or `key:<KeyName>`.
4. `bt_get` shows the tree; `runnable: true` means the runtime tree was built.
5. Fix a tree in place: `bt_set_node_property` path, id, properties (same value rules; returns the new values as text); `bt_remove_node` path, id (a decorator/service is detached, a composite/task goes with its subtree). `blackboard_get` lists keys with types (every Blackboard also has `SelfActor`).
6. For state-machine style logic (states, tasks, transitions) use the `unreal-state-tree` skill instead.
7. Controller: `blueprint create /Game/AI/BP_EnemyAI --parent AIController`; on BeginPlay call `RunBehaviorTree` (CallFunction) with the tree; set the pawn's `AIControllerClass` with `prop set`.
8. Navigation: `nav_bounds_add` location, extent (half size) covering the walkable floor; check with `nav_path` from/to (poll; the mesh builds in the background).
9. Play-test with the `unreal-pie-testing` skill (`pie get <enemy>` location changes).

## Verify

`bt_get` has `runnable: true` and the expected children; `nav_path` returns `valid: true` between two floor points.

## On failure

`ai.invalid_parent`: the root takes one composite; tasks go under composites; decorators and services attach to composites or tasks. `args.invalid` names the bad key or property. `nav_path` stays invalid: the bounds do not cover a floor with collision.
