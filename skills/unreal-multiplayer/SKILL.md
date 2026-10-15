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

## Verify

`net assert` returns `ok: true` with each world's value. The client's change made through a Server RPC shows up in `pie get --world server`.

## On failure

| Code | Meaning |
|---|---|
| `net.mismatch` | The worlds disagree; the details list each world's value. Check bReplicates, the variable's replication, and that the change was made on the server |
| `pie.world_not_found` | The hint lists the session's worlds |
| `net.not_networked` | `net emulate` needs `pie start --players 2 --net-mode listen` |
| `code.exists` | The class already declares that name |
