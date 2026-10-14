---
name: unreal-umg
description: Use when building UI with Widget Blueprints: widgets, layout, properties, animations, button events and showing the UI in game.
version: 1.1.0
tools: [widget_create, widget_add, widget_layout, widget_set_prop, widget_animation_add, widget_bind, widget_tree_get]
---

# unreal-umg

## When to use

Use when building UI with Widget Blueprints: widgets, layout, properties, animations, button events and showing the UI in game.

## Steps

1. `widget_create` with a path (root CanvasPanel by default).
2. `widget_add` class/name/parent (VerticalBox, HorizontalBox, Overlay, TextBlock, Button, Image, ProgressBar, ...).
3. `widget_layout`:
   - Canvas children: `anchor` (TopLeft ... BottomRight, TopFill, Fill or [minX,minY,maxX,maxY]), `position`, `size`, `alignment` (pivot 0..1), `autoSize`, `zOrder`.
   - Box/overlay children: `padding`, `hAlign`, `vAlign`, `fill`.
4. `widget_set_prop` widget/property/value for content and style (e.g. `Text`, `ColorAndOpacity`, `Font`, `Percent`).
5. `widget_animation_add` name/widget/track (opacity, translation, scale, angle) with keys `[[seconds, value]]`; call again with the same name to add tracks.
6. `widget_bind` widget/event (e.g. OnClicked), then `unrealengine-cli blueprint compile <path>`.
7. Play an animation: in the widget graph `Event Construct` -> `CallFunction PlayAnimation` with `VariableGet <animation>` (exists after compile).
8. Show the widget in game: in the player controller or level Blueprint, `CreateWidget --member <widget path>` then `CallFunction AddToViewport`.
9. Check it on screen with `pie start` + `pie screenshot` (unreal-pie-testing skill).

## Verify

`widget_tree_get` shows the widgets; `widget_layout` returns the slot layout; compile has 0 errors; the PIE screenshot shows the UI.

## On failure

`umg.bad_parent`: the parent must be a panel; `umg.name_taken`: choose another name; `args.invalid` from `widget_layout`: canvas-only options on a box child (use padding/hAlign/vAlign/fill).
