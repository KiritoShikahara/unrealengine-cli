---
name: unreal-asset-management
description: Use when searching, creating, moving, importing or deleting assets and checking references.
version: 1.0.0
tools: [asset_search, asset_create, asset_import, asset_references, asset_delete]
---

# unreal-asset-management

## When to use

Use when searching, creating, moving, importing or deleting assets and checking references.

## Steps

1. Find: `unreal-cli asset search <name> --class Material`.
2. Create: `unreal-cli asset create /Game/Path/Name --class MaterialInstanceConstant`.
3. Import: `unreal-cli asset import /Game/Imported <file.fbx>`.
4. Before deleting: `unreal-cli asset refs <path>`.
5. Save: `unreal-cli asset save --folder /Game/Path`.

## Verify

`asset search` finds the asset at its new path.

## On failure

`asset.referenced` lists referencers: update them or ask before `--force`.
