---
name: unreal-online-sessions
description: Use when adding online play: configuring the online subsystem (Null for local tests, EOS or Steam), checking the settings, and creating, finding and joining sessions.
version: 1.0.0
tools: [online_session, pie_start, pie_worlds]
---

# unreal-online-sessions

## When to use

Use when adding online play: configuring the online subsystem (Null for local tests, EOS or Steam), checking the settings, and creating, finding and joining sessions.

## Steps

1. Configure: `unrealengine-cli online configure --subsystem null` for local tests. EOS: `--subsystem eos --set ProductId=... --set SandboxId=... --set DeploymentId=... --set ClientId=... --set ClientSecret=...`; Steam: `--subsystem steam --set SteamDevAppId=480`. Restart the editor when it reports plugin changes.
2. Check: `unrealengine-cli online check` lists missing keys and plugins. Secret values (ClientSecret, EncryptionKey) are never printed; they show as `***`.
3. Try sessions in the editor: `unrealengine-cli pie start --players 2 --net-mode standalone`, then `online session create --world standalone --max-players 4`, `online session find --world standalone:2`, `online session join --world standalone:2 --index 0 [--travel]`, `online session destroy --world standalone`.
4. Real EOS / Steam accounts are tested in a packaged or standalone game; the editor tests use LAN sessions through Null.

## Verify

`online session find` lists the created session's id; `join` returns a `connectString`.

## On failure

| Code | Meaning |
|---|---|
| `online.missing_settings` | Set the listed keys with `online configure --set KEY=VALUE` |
| `online.unknown_key` | The hint lists the subsystem's keys |
| `online.session_failed` | Refused or timed out; destroy a session with the same name first |
| `online.no_result` | Run `online session find` in the same world before `join` |
| `online.no_subsystem` | Configure a subsystem and restart the editor |
