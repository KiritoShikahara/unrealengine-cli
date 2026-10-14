---
name: unreal-niagara
description: Use when creating Niagara systems, adding emitters and modules, and setting module inputs.
version: 1.1.0
tools: [niagara_system_create, emitter_add, niagara_module_add, niagara_input_set, niagara_emitters_list]
---

# unreal-niagara

## When to use

Use when creating Niagara systems, adding emitters and modules, and setting module inputs.

## Steps

1. `niagara_system_create` path.
2. Find templates: `unreal-cli asset search Fountain --path /Niagara`.
3. `emitter_add` system/emitter.
4. `niagara_module_add` stage + module script; keep the returned `module` name.
5. `niagara_input_set` stage, module, input, value (number, bool, or [x,y,z(,w)]).
6. `niagara_emitters_list` to check.

## Verify

The system lists the emitter.

## On failure

`niagara.emitter_not_found`: search /Niagara for NiagaraEmitter assets. `niagara.input_not_found`: the hint lists the module's inputs.
