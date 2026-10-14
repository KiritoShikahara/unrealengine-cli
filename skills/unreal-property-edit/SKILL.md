---
name: unreal-property-edit
description: Use when reading or changing reflected properties of actors, components, assets or Blueprint defaults.
version: 1.1.0
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
5. Blueprint defaults: `unrealengine-cli prop set /Game/BP_Enemy AIControllerClass /Game/AI/BP_EnemyAI.BP_EnemyAI_C`.
6. Blueprint components (added or inherited): `unrealengine-cli prop set /Game/BP_Crate.Body StaticMesh /Engine/BasicShapes/Cube`, `prop set /Game/BP_Hero.CharacterMovement MaxWalkSpeed 900` (compile after `bp_add_component` first).
7. Level settings: `unrealengine-cli prop set WorldSettings DefaultGameMode /Game/BP_GM.BP_GM_C`.
8. Enums take short names (`PlacedInWorldOrSpawned`); the error hint lists valid values.

## Verify

`prop get` returns the new value.

## On failure

`prop.not_found` suggests similar names; `component.not_found` lists the Blueprint's components.
