---
name: unreal-pie-testing
description: Use when verifying behaviour by playing the game in the editor (input, screenshots, actor state) or running Automation tests.
version: 1.1.0
tools: [pie_run, pie_start, pie_input, pie_wait, pie_screenshot, pie_actors, pie_get, pie_call, pie_exec, pie_widgets, pie_click, pie_stop, automation_run, automation_list]
---

# unreal-pie-testing

## When to use

Use when verifying behaviour by playing the game in the editor (input, screenshots, actor state) or running Automation tests.

## Steps

1. Play-test needs a rendering editor: `unrealengine-cli editor open` (or `--headless`), not `--null-rhi`.
2. Quick check: `unrealengine-cli pie run --seconds 15 --stop-on-log <expected log text>`.
3. Interactive play-test:
   1. `unrealengine-cli pie start`.
   2. `unrealengine-cli pie input W --seconds 1` (keys: W, SpaceBar, LeftMouseButton, Gamepad_LeftX) or `pie input --action /Game/Input/IA_Jump --value true`.
   3. `unrealengine-cli pie wait --seconds 2` or `--until-log <text>`.
   4. `unrealengine-cli pie get player` (location, velocity), `pie get <actor> Health Score`, `pie actors --class Enemy`.
   5. `unrealengine-cli pie screenshot` and view the PNG; `blank: true` means nothing visible (unlit level or wrong camera).
   6. `unrealengine-cli pie call <actor> "TakeHit 25"` to trigger functions or custom events; `pie exec "slomo 0.5"`.
   7. UI: `unrealengine-cli pie widgets` lists on-screen widgets and texts; `pie click <ButtonName>` presses a button and returns what its handler logged.
   8. `unrealengine-cli pie stop`.
4. `unrealengine-cli test list <filter>` and `unrealengine-cli test run <filter>`; without a running editor `test run` uses a headless editor process.

## Verify

State read with `pie get` changed as expected after input; the screenshot shows the expected scene; `test run` has `failed: 0`.

## On failure

Quote failing test names and their first error. `pie.already_running`: `unrealengine-cli pie stop`. `pie.not_running`: `pie start` first. `pie.screenshot_failed`: reopen the editor with rendering.
