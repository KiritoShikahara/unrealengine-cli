---
name: unreal-landscape
description: Use when creating terrain or sculpting a landscape.
version: 1.0.0
tools: [landscape_create, landscape_sculpt, landscape_info]
---

# unreal-landscape

## When to use

Use when creating terrain or sculpting a landscape.

## Steps

1. `landscape_create` components (each 63 quads), location, scale, optional material.
2. `landscape_sculpt` center [x,y], radius, height (cm, negative lowers).
3. `landscape_info` to check size.

## Verify

`landscape_info` lists the landscape.

## On failure

`landscape.not_found`: create one first.
