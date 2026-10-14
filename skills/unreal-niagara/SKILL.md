---
name: unreal-niagara
description: Use when creating Niagara systems and adding emitters.
version: 1.0.0
tools: [niagara_system_create, emitter_add, niagara_emitters_list]
---

# unreal-niagara

## When to use

Use when creating Niagara systems and adding emitters.

## Steps

1. `niagara_system_create` path.
2. Find templates: `unreal-cli asset search Fountain --path /Niagara`.
3. `emitter_add` system/emitter.
4. `niagara_emitters_list` to check.

## Verify

The system lists the emitter.

## On failure

`niagara.emitter_not_found`: search /Niagara for NiagaraEmitter assets.
