---
name: unreal-visual-perf-testing
description: Use when checking that a change did not break how the game looks or how fast it runs: screenshot baselines and comparisons, frame time measurement, and Unreal Insights traces.
version: 1.0.0
tools: [pie_screenshot, editor_screenshot, perf_measure, perf_trace]
---

# unreal-visual-perf-testing

## When to use

Use when checking that a change did not break how the game looks or how fast it runs: screenshot baselines and comparisons, frame time measurement, and Unreal Insights traces.

## Steps

1. Baseline: `unrealengine-cli pie start`, wait a moment, then `unrealengine-cli visual baseline arena` (saved as Tests/Visual/arena.png; `--source editor` uses the editor viewport, `--image <png>` an existing file).
2. After a change: `unrealengine-cli visual compare arena [--threshold 0.5] [--tolerance 8]`. It writes a difference image (differing pixels red) and fails with exit code 4 when more pixels than the threshold (percent) differ.
3. Two files: `unrealengine-cli visual diff a.png b.png --out diff.png`.
4. Speed: with PIE running, `unrealengine-cli perf measure --seconds 10` returns frame, game, render and GPU times (avg, p95, max), hitches, draw calls and memory.
5. Deep dive: `unrealengine-cli perf trace start`, play, `perf trace stop`, then `unrealengine-cli perf trace summary <file.utrace> --top 20` lists the most expensive timers of the game thread.

## Verify

`visual compare` returns `diffPercent` under the threshold; `perf measure` returns `partial: false` with plausible `frameMs.p95`.

## On failure

| Code | Meaning |
|---|---|
| `visual.mismatch` | Look at `details.diffImage`; take a new baseline when the change is intended |
| `visual.size_mismatch` | Capture at the same window size as the baseline |
| `visual.no_baseline` | Take one first: `visual baseline <name>` |
| `perf.no_trace` / `perf.summary_failed` | Check the trace path; the details hold Insights' log tail |
