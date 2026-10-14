---
name: unreal-enhanced-input
description: Use when adding input actions, mapping contexts and key bindings, and wiring them to player Blueprints.
version: 1.1.0
tools: [input_action_create, mapping_context_create, mapping_add, input_default_context_add, bp_add_node]
---

# unreal-enhanced-input

## When to use

Use when adding input actions, mapping contexts and key bindings, and wiring them to player Blueprints.

## Steps

1. `input_action_create` path, valueType (Boolean for buttons, Axis2D for movement/look).
2. `mapping_context_create` path.
3. `mapping_add` context/action/key, with `modifiers` and `triggers` when needed:
   - WASD on one Axis2D move action (X = right, Y = forward): D none, A `["Negate"]`, W `["Swizzle"]`, S `["Swizzle","Negate"]`.
   - Mouse look: `Mouse2D` on an Axis2D action (add `Negate` for inverted Y).
   - Buttons: SpaceBar, LeftMouseButton, Gamepad_FaceButton_Bottom; `triggers: ["Pressed"]` fires once per press.
4. Make the context active, either:
   - `input_default_context_add` context (Project Settings, applies to every player), or
   - in the pawn's BeginPlay: `Node K2Node_GetSubsystemFromPC` with `properties: {CustomClass: /Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem}` fed by `GetPlayerController`, then `CallFunction AddMappingContext` (memberParent EnhancedInputLocalPlayerSubsystem).
5. React in the pawn: `bp_add_node --type InputAction --member <action>`; pins Triggered (every frame while held), Started, Completed, ActionValue (break Axis2D with `BreakStruct Vector2D`, input pin InVec).
6. Movement: `AddMovementInput` with `GetActorForwardVector` and the Y value, `GetActorRightVector` and the X value.

## Verify

`mapping_add` returns the modifiers it applied; in PIE `pie input W --seconds 0.5` moves the pawn (`pie get player`).

## On failure

`input.bad_key`: wrong key name. `input.bad_modifier` / `input.bad_trigger` list the valid names. Nothing happens in PIE: the mapping context was not added (step 4).
