---
name: unreal-python-scripting
description: Use when no dedicated tool exists and the editor's Python API can do the job.
version: 1.0.0
tools: [python_exec]
---

# unreal-python-scripting

## When to use

Use when no dedicated tool exists and the editor's Python API can do the job.

## Steps

1. `unreal-cli python exec "import unreal; print(...)"` or `unreal-cli python exec @script.py`.
2. Keep scripts short; print the values you need.

## Verify

The result has `succeeded: true` and the printed output.

## On failure

`python.exception` carries the traceback in `error.message`.
