---
name: unreal-engine-reference
description: Use when you need to check an engine C++ or Python API signature before using it.
version: 1.0.0
tools: []
---

# unreal-engine-reference

## When to use

Use when you need to check an engine C++ or Python API signature before using it.

## Steps

1. `unreal-cli reference search <Symbol>` searches engine headers.
2. `unreal-cli reference search <name> --scope python` searches the Python stub in Intermediate/PythonStub.

## Verify

The hit shows the declaration with file and line.

## On failure

No Python stub: open the editor once with Python enabled.
