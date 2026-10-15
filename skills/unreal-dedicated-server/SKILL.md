---
name: unreal-dedicated-server
description: Use when running or testing a game on a dedicated server: starting a server process, joining it from Play In Editor, reading its log, or building and packaging a Server target.
version: 1.0.0
tools: [pie_exec, pie_worlds]
---

# unreal-dedicated-server

## When to use

Use when running or testing a game on a dedicated server: starting a server process, joining it from Play In Editor, reading its log, or building and packaging a Server target.

## Steps

1. Quick test server (any engine, no build): `unrealengine-cli server start --map /Game/Maps/Arena --port 7777`. It runs the editor binary in server mode and returns once the log says it listens; `server list` shows running servers.
2. Join from the editor: `unrealengine-cli pie start`, then `unrealengine-cli pie connect 127.0.0.1:7777` (add `--world client:1` in a multiplayer session).
3. Read the server: `unrealengine-cli server logs <pid> --tail 100`; a joined player shows `Join succeeded`.
4. Stop it: `unrealengine-cli server stop <pid>` (only servers started by `server start` can be stopped).
5. Shipping server (engine built from source): `unrealengine-cli server target add`, `server build [--config Shipping]`, `server package --out-dir ../ServerBuild`. Afterwards `server start` uses `Binaries/<platform>/<Project>Server`.

## Verify

`server logs` contains `listening on port` and, after `pie connect`, `Join succeeded`.

## On failure

| Code | Meaning |
|---|---|
| `server.source_engine_required` | Launcher engines cannot build Server targets; use `server start` (editor binary) or a source build |
| `server.no_target` / `server.no_code` | Run `server target add` (needs a C++ module: `setup --add-code-module`) |
| `server.port_in_use` | Pick another `--port` or stop the old server |
| `server.exited` | The details hold the log tail; a wrong map path is the usual cause |
| `server.unsafe_output` | Package to a folder outside the project |
