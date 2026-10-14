---
name: unreal-packaging
description: Use when packaging or cooking the project.
version: 1.0.0
tools: [package_project, cook_content, package_status]
---

# unreal-packaging

## When to use

Use when packaging or cooking the project.

## Steps

1. `package_project` outputDir (configuration Development or Shipping).
2. Poll `package_status` with the returned log until succeeded or failed.

## Verify

`package_status` reports `succeeded`.

## On failure

Show the `errors` lines from `package_status`.
