---
name: unreal-material
description: Use when creating materials, wiring material expressions or setting material instance parameters.
version: 1.0.0
tools: [material_create, material_node_add, material_connect, mi_create, mi_set_param]
---

# unreal-material

## When to use

Use when creating materials, wiring material expressions or setting material instance parameters.

## Steps

1. `material_create` path.
2. `material_node_add` type (VectorParameter, Constant, TextureSample, Multiply, ...) with properties.
3. `material_connect` from and property (BaseColor, Roughness, ...) or another expression input.
4. `mi_create` with the parent, then `mi_set_param` name/value.

## Verify

`material_recompile` reports the expression count; `mi_set_param` returns the stored value.

## On failure

`material.param_not_found` lists the parent's parameters.
