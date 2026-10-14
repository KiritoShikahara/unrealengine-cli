---
name: unreal-development-loop
description: Use when iterating on a feature: edit, compile, run in PIE, read logs, repeat until it works.
version: 1.0.0
tools: [pie_run, editor_logs, bp_compile]
---

# unreal-development-loop

## When to use

Use when iterating on a feature: edit, compile, run in PIE, read logs, repeat until it works.

## Steps

1. Make the change (Blueprint, level or C++).
2. Compile: `unrealengine-cli blueprint compile <path>` or `unrealengine-cli code apply`.
3. Run: `unrealengine-cli pie run --seconds 10 --stop-on-log <marker>`.
4. Read problems: `unrealengine-cli logs --severity warning --since <nextSince>`.
5. Save: `unrealengine-cli tool call editor_save_all`.

## Verify

`pie run` reports `errors: []` and the expected `endedReason`.

## On failure

Report the first error with its log line; do not claim success without a clean PIE run.
