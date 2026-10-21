---
name: unreal-state-tree
description: Use when building State Trees (states, tasks, transitions) for actors or AI, as a state-machine alternative to Behavior Trees.
version: 1.0.0
tools: [statetree_create, statetree_add_state, statetree_add_task, statetree_add_transition, statetree_compile, statetree_get]
---

# unreal-state-tree

## When to use

Use when building State Trees (states, tasks, transitions) for actors or AI, as a state-machine alternative to Behavior Trees.

## Steps

1. `statetree_create` path, schema `Component` (any actor with a StateTreeComponent; default) or `AIComponent` (AI controllers with a StateTreeAIComponent). The tree starts with one state, `Root`.
2. Add states top-down with `statetree_add_state` path, parent (a state name, first `Root`), name (unique in the tree); optional type `State|Group` and selection `TrySelectChildrenInOrder|TryEnterState|TrySelectChildrenAtRandom`.
3. Give each leaf state work with `statetree_add_task` path, state, class (`Delay` with `{Duration: 2}`, `DebugText` with `{Text: "..."}`, `RunParallelStateTree`, or any task struct name) and properties.
4. Connect states with `statetree_add_transition` path, state, type `GotoState` (with target), `NextState`, `NextSelectableState`, `Succeeded` or `Failed`; trigger `OnStateCompleted` (default), `OnStateSucceeded`, `OnStateFailed` or `OnTick`.
5. `statetree_compile` path, then save with `unrealengine-cli asset save <path>`.
6. Run it: add a `StateTreeComponent` to the actor Blueprint (`unrealengine-cli blueprint add-component`), set its `StateTreeRef` with `prop set`, and play-test with the `unreal-pie-testing` skill.

## Verify

`statetree_compile` returns `compiled: true`; `statetree_get` shows `ready: true`, `upToDate: true` and the expected states, tasks and transition targets.

## On failure

`statetree.state_not_found`: use the names from `statetree_get` (case-sensitive). `statetree.task_not_found`: the class is not a State Tree task struct; Blueprint tasks are added in the State Tree editor. `compiled: false`: read `messages` (often a GotoState target in another subtree or a task the schema does not allow). `statetree.schema_not_found`: the GameplayStateTree plugin is off.
