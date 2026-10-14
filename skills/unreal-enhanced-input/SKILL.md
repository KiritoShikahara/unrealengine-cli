---
name: unreal-enhanced-input
description: Use when adding input actions, mapping contexts and key bindings.
version: 1.0.0
tools: [input_action_create, mapping_context_create, mapping_add]
---

# unreal-enhanced-input

## When to use

Use when adding input actions, mapping contexts and key bindings.

## Steps

1. `input_action_create` path, valueType (Boolean, Axis1D, Axis2D, Axis3D).
2. `mapping_context_create` path.
3. `mapping_add` context/action/key (SpaceBar, W, Gamepad_FaceButton_Bottom).

## Verify

`mapping_add` returns the mapping count.

## On failure

`input.bad_key` means the key name is wrong.
