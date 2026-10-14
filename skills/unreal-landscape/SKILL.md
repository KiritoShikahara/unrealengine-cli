---
name: unreal-landscape
description: Use when creating terrain, sculpting or painting a landscape.
version: 1.1.0
tools: [landscape_create, landscape_sculpt, landscape_layer_add, landscape_paint, landscape_info]
---

# unreal-landscape

## When to use

Use when creating terrain, sculpting or painting a landscape.

## Steps

1. `landscape_create` components (each 63 quads), location, scale, optional material.
2. `landscape_sculpt` center [x,y], radius, height (cm, negative lowers).
3. `landscape_layer_add` layer (creates its LayerInfo asset) before painting.
4. `landscape_paint` layer, center [x,y], radius, strength 0..1.
5. `landscape_info` to check size.

## Verify

`landscape_info` lists the landscape; `landscape_paint` returns `centerWeight` > 0.

## On failure

`landscape.not_found`: create one first. `landscape.layer_not_found`: run `landscape_layer_add`.
