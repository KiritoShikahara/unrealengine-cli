---
name: unreal-animation
description: Use when creating Animation Blueprints, state machines, states, transitions, montages or blend spaces.
version: 1.0.0
tools: [animbp_create, statemachine_add, state_add, transition_add, sequence_player_add, montage_create, blendspace_create]
---

# unreal-animation

## When to use

Use when creating Animation Blueprints, state machines, states, transitions, montages or blend spaces.

## Steps

1. `unrealengine-cli tool call animbp_create --args '{"path":"/Game/Anim/ABP_Hero","skeleton":"<skeleton path>"}'`.
2. `statemachine_add` (name), then `state_add` per state with an optional `sequence`.
3. `transition_add` from/to (automatic=true for time-based).
4. `unrealengine-cli blueprint compile /Game/Anim/ABP_Hero`.

## Verify

Compile reports `errors: 0`.

## On failure

`anim.machine_not_found` or `anim.state_not_found`: list graphs with `unrealengine-cli blueprint get <path>`.
