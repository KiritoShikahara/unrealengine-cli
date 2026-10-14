---
name: unreal-umg
description: Use when building UI with Widget Blueprints: widgets, layout, properties and button events.
version: 1.0.0
tools: [widget_create, widget_add, widget_set_prop, widget_bind, widget_tree_get]
---

# unreal-umg

## When to use

Use when building UI with Widget Blueprints: widgets, layout, properties and button events.

## Steps

1. `widget_create` with a path (root CanvasPanel by default).
2. `widget_add` class/name/parent (VerticalBox, TextBlock, Button, Image, ...).
3. `widget_set_prop` widget/property/value (UE text form; `Slot.Padding` targets the slot).
4. `widget_bind` widget/event (e.g. OnClicked), then `unreal-cli blueprint compile <path>`.

## Verify

`widget_tree_get` shows the widgets; compile succeeds.

## On failure

`umg.bad_parent`: the parent must be a panel; `umg.name_taken`: choose another name.
