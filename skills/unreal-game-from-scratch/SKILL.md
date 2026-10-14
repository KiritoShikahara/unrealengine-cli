---
name: unreal-game-from-scratch
description: Use when building a playable game or prototype end to end: game mode, player pawn and input, gameplay actors, HUD, level, then play-testing it.
version: 1.0.0
tools: [bp_create, bp_add_component, bp_add_node, bp_connect, bp_set_pin, bp_compile, prop_set, input_action_create, mapping_add, widget_create, widget_layout, level_new, actor_spawn, pie_start, pie_input, pie_get, pie_screenshot]
---

# unreal-game-from-scratch

## When to use

Use when building a playable game or prototype end to end: game mode, player pawn and input, gameplay actors, HUD, level, then play-testing it.

## Steps

1. Open a rendering editor: `unrealengine-cli editor open` (PIE and screenshots need it).
2. Input (unreal-enhanced-input skill): an Axis2D move action with WASD modifiers, Boolean actions for buttons, one mapping context.
3. Player: `unrealengine-cli blueprint create /Game/Game/BP_Hero --parent Character`; BeginPlay adds the mapping context; `InputAction` nodes drive `AddMovementInput` and gameplay (spawn projectiles with `SpawnActor`).
4. Gameplay actors: Blueprints with collision components (`bp_add_component`, then `prop set /Game/X.Comp CollisionProfileName BlockAllDynamic`); react with `ComponentEvent` (overlap/hit), `Event AnyDamage`, `ApplyDamage`, event dispatchers for cross-actor messages.
5. Rules and score: a `GameModeBase` Blueprint with variables and functions; actors reach it with `GetGameMode` + `Cast`.
6. HUD (unreal-umg skill): `widget_create`, `widget_add`, `widget_layout`; the game mode's BeginPlay does `CreateWidget` + `AddToViewport` and keeps the widget in a variable to update texts.
7. Wire it up: `prop set /Game/Game/BP_GM DefaultPawnClass /Game/Game/BP_Hero.BP_Hero_C`; `unrealengine-cli level new /Game/Game/L_Main --template /Engine/Maps/Templates/Template_Default`; place actors with `actor_spawn`; `prop set WorldSettings DefaultGameMode /Game/Game/BP_GM.BP_GM_C`; `level save`.
8. Compile every Blueprint (`blueprint compile`, 0 errors) and save.
9. Play-test (unreal-pie-testing skill): `pie start`, `pie input W --seconds 1`, `pie get player`, `pie actors --class BP_Enemy`, `pie screenshot` (view it), `pie stop`.
10. Enemies that move: unreal-ai skill (Behavior Tree with MoveTo, `nav_bounds_add` over the floor).

## Verify

In PIE the pawn moves on input, gameplay state changes as expected (`pie get`), and the screenshot shows the scene and HUD.

## On failure

Nothing moves: the mapping context is not added or the pawn is not the game mode's DefaultPawnClass (`pie get player` shows its class). Pins not found: the error lists the node's pins (structs like HitResult and Vector2D break into their fields; their input pin is `Hit` / `InVec`). Black screenshot (`blank: true`): the level has no lights; use the Template_Default template.
