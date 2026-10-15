---
name: unreal-multiplayer
description: Use when making a game multiplayer or checking multiplayer behaviour: replicated actors and variables, RPCs, and play-testing a listen or dedicated server with clients in the editor.
version: 1.0.0
tools: [pie_start, pie_worlds, pie_get, pie_call, pie_input, pie_actors, bp_set_replication, bp_set_rpc, net_assert, net_emulate]
---

# unreal-multiplayer

## When to use

Use when making a game multiplayer or checking multiplayer behaviour: replicated actors and variables, RPCs, and play-testing a listen or dedicated server with clients in the editor.

## Rules of replication

- The server owns the game state. A change made on a client stays on that client.
- An actor reaches the clients only if it replicates: `prop set <bp> bReplicates true` (Characters and PlayerControllers already do).
- A replicated variable goes from the server to the clients. With RepNotify, its `OnRep_<Var>` function runs on the clients when the value arrives. A Blueprint also runs it on the server when the server sets the value.
- A client asks the server to act through a Server RPC on an actor it owns: its PlayerController, its pawn, or something owned by them. Server RPCs on other actors are dropped.
- Multicast RPCs are called on the server and run everywhere. Client RPCs run on the owning client.

## Steps

1. Replicate:
   - Blueprint: `unrealengine-cli blueprint replicate <bp> <Var> [--notify] [--condition OwnerOnly]`, then fill the `OnRep_<Var>` graph.
   - RPC: add a CustomEvent, then `unrealengine-cli blueprint rpc <bp> <Event> --type server --reliable`. A CallFunction node for the event can be added right away.
   - C++: `unrealengine-cli code replicate <Header.h> <Name> <Type> [--notify]` and `code rpc <Header.h> <Name> --type server --reliable [--validate] [--param Amount:float]`; set `bReplicates = true` in the constructor; then `code apply`.
2. Play with several players: `unrealengine-cli pie start --players 2 --net-mode listen` (`client` = dedicated server + clients). `pie worlds` lists `server`, `client:1`, ...
3. Drive and observe one world with `--world`: `pie input W --seconds 1 --world client:1`, `pie get player --world server`, `pie call controller "ServerDoThing" --world client:1`. `controller` is the world's player controller, `player` its pawn; actors report `role`, `remoteRole` and `owner`.
4. Check that the worlds agree: `unrealengine-cli net assert <actor> <Property> [--expect 5] [--timeout 3]`. Level actors have the same name in every world; spawned ones can be found by class name.
5. Bad networks: `unrealengine-cli net emulate --latency 200 --jitter 20 --loss 5`, test again, then `net emulate --off`.
6. Wait about a second after `pie start` before sending input: the clients' pawns are possessed and their input is set up shortly after they join.
7. Separate dedicated server: `unrealengine-cli server start --map /Game/Maps/Arena --port 7777` (editor binary in server mode, works with Launcher engines; returns once it listens), then `unrealengine-cli pie start` and `unrealengine-cli pie connect 127.0.0.1:7777`. Read `server logs <pid>` (look for `Join succeeded`) and end with `server stop <pid>`; `server list` shows running ones.
8. Shipping server build (engine built from source): `unrealengine-cli server target add`, `server build`, `server package --out-dir ../ServerBuild`.
9. Sessions (lobby / matchmaking): `unrealengine-cli online configure --subsystem null` (EOS: `--subsystem eos --set ProductId=... --set ClientSecret=...`; Steam: `--set SteamDevAppId=480`), restart the editor if plugins changed, `online check`. In PIE: `online session create --world standalone`, `online session find --world standalone:2`, `online session join --world standalone:2 --index 0 [--travel]`, `online session destroy`.

## Verify

`net assert` returns `ok: true` with each world's value. The client's change made through a Server RPC shows up in `pie get --world server`.

## On failure

| Code | Meaning |
|---|---|
| `net.mismatch` | The worlds disagree; the details list each world's value. Check bReplicates, the variable's replication, and that the change was made on the server |
| `pie.world_not_found` | The hint lists the session's worlds |
| `net.not_networked` | `net emulate` needs `pie start --players 2 --net-mode listen` |
| `code.exists` | The class already declares that name |
| `server.source_engine_required` | Launcher engines cannot build Server targets; use `server start` (editor binary) or a source build |
| `server.port_in_use` | Pick another `--port` or stop the old one (`server list`, `server stop <pid>`) |
| `server.exited` | The server quit during startup; the details hold the log tail (wrong map path is common) |
| `online.missing_settings` | `online check` lists the missing keys or plugins; set them with `online configure --set` |
| `online.session_failed` | The subsystem refused or timed out; destroy an existing session of the same name first |
| `server.unknown_pid` | `server stop` only ends servers started by `server start` |
