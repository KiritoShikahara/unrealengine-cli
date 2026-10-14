---
name: unreal-property-edit
description: Use when reading or changing reflected properties of actors, components, assets or Blueprint defaults.
version: 1.0.0
tools: [prop_get, prop_set, prop_array_edit, component_add]
---

# unreal-property-edit

## When to use

Use when reading or changing reflected properties of actors, components, assets or Blueprint defaults.

## Steps

1. List: `unrealengine-cli prop get <Label or /Game/Asset> --filter speed`.
2. Read one: `unrealengine-cli prop get Lamp.LightComponent0 Intensity`.
3. Set: `unrealengine-cli prop set Lamp.LightComponent0 Intensity 5000` (vectors as [x,y,z]).
4. Arrays: `unrealengine-cli prop array Lamp Tags append --value Enemy`.

## Verify

`prop get` returns the new value.

## On failure

`prop.not_found` suggests similar names.
