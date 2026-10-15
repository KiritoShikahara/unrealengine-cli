---
name: unreal-localization
description: Use when translating a game or keeping save games working across versions: localization targets, gathering text, PO files, compiling translations, previewing a language, and SaveGame compatibility.
version: 1.0.0
tools: [loc_target, loc_preview]
---

# unreal-localization

## When to use

Use when translating a game or keeping save games working across versions: localization targets, gathering text, PO files, compiling translations, previewing a language, and SaveGame compatibility.

## Steps

1. Target: `unrealengine-cli loc target --culture ja --culture fr [--path Content/UI]` (target Game, native en by default).
2. Gather and export: `unrealengine-cli loc gather`, then `loc export` writes Content/Localization/Game/<culture>/Game.po.
3. Translate: edit the PO files, or `unrealengine-cli loc translate ja --entry "Start=スタート" --entry "Quit=終了"`.
4. Import and compile: `unrealengine-cli loc import`, then `loc compile` (writes Game.locres per culture).
5. Check: `unrealengine-cli loc preview ja --asset /Game/UI/WBP_Menu --property Title` shows the translated text in the editor; `loc preview off` goes back. A packaged game picks the language with `-culture=ja`.
6. Save games across versions: add new SaveGame variables with defaults; old saves keep the values they have and new variables take their defaults. Never rename or change the type of a saved variable; add a new one and copy the old value on load (keep an int SaveVersion to know when).

## Verify

`loc translate` lists the filled entries; `loc compile` lists a locres per culture; `loc preview <culture>` returns the translated text.

## On failure

| Code | Meaning |
|---|---|
| `loc.no_target` | Run `loc target` first |
| `loc.no_po` | Run `loc gather` and `loc export` first |
| `loc.step_failed` | The details list the commandlet errors |
| Translation missing in `loc translate` | The source text differs (whitespace, punctuation); copy it from the PO msgid |
