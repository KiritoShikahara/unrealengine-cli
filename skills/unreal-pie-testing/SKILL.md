---
name: unreal-pie-testing
description: Use when verifying behaviour by playing in the editor or running Automation tests.
version: 1.0.0
tools: [pie_run, automation_run, automation_list]
---

# unreal-pie-testing

## When to use

Use when verifying behaviour by playing in the editor or running Automation tests.

## Steps

1. `unreal-cli pie run --seconds 15 --stop-on-log <expected log text>`.
2. `unreal-cli test list <filter>` and `unreal-cli test run <filter>`.
3. Without a running editor `test run` uses a headless editor process.

## Verify

`pie run` ends with `endedReason: log` and no errors; `test run` has `failed: 0`.

## On failure

Quote failing test names and their first error. `pie.already_running`: `unreal-cli pie stop`.
