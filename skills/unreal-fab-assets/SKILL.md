---
name: unreal-fab-assets
description: Use when the project needs ready-made content from Fab (meshes, materials, VFX, sounds, asset packs): find a listing, then download and add it to the project.
version: 1.0.0
tools: [fab_status, fab_search, fab_library, fab_add]
---

# unreal-fab-assets

## When to use

Use when the project needs ready-made content from Fab (meshes, materials, VFX, sounds, asset packs): find a listing, then download and add it to the project.

## Steps

1. `unrealengine-cli fab status`: `signedIn` must be true. If it is false, ask the user to open Window > Fab in the editor and sign in. You cannot sign in for them.
2. Find a listing:
   - `unrealengine-cli fab library <words>` searches what the user already owns. Prefer these.
   - `unrealengine-cli fab search <words> --free` searches the whole store. `formats` must include `unreal-engine`, `gltf`, `glb` or `fbx`.
3. `unrealengine-cli fab add <uid>` downloads the listing and imports it, then waits until the import is done.
   - Asset packs (`unreal-engine`) land in their own `/Game/<PackName>` folder. glTF/FBX files land under `/Game/Fab/<Title>`.
   - `--format fbx` picks a format. `--quality high` picks a Megascans quality.
4. Use the returned `path` and `assets` with `asset search`, `level spawn`, `prop set` and the other commands.

## Rules

- Only add listings the user owns. If `fab add` fails with `fab.not_owned`, ask the user before you pass `--acquire-free`. That flag adds a free listing to their Fab library and accepts its license for them.
- Never buy anything. Paid listings fail with `fab.not_free`; tell the user to buy the listing on fab.com.
- Plugins and complete projects (`fab.unsupported_format`) are installed from the Epic Games Launcher, not by `fab add`.

## Verify

`fab add` returns `path`, `assetCount` and up to 50 `assets`. Check them with `asset search --path <path>`.

## On failure

| Code | Meaning |
|---|---|
| `fab.not_signed_in` | The Fab tab is not signed in |
| `fab.not_owned` | The listing is not in the user's library |
| `fab.engine_mismatch` | The pack has no version for this engine; the message lists the versions it has |
| `fab.unsupported_format` | The listing has no importable format, or it is a plugin or project |
| `fab.import_failed` | The Fab plugin reported an error; read `logs --category LogFab` |
| `fab.unavailable` | The Fab plugin is disabled in this editor |
