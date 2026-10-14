---
name: unreal-data-assets
description: Use when defining data: structs, enums, data tables and data assets.
version: 1.0.0
tools: [struct_create, enum_create, datatable_create, datatable_rows_set, data_asset_create]
---

# unreal-data-assets

## When to use

Use when defining data: structs, enums, data tables and data assets.

## Steps

1. `struct_create` path with fields [{name,type}].
2. `enum_create` path with values.
3. `datatable_create` path/rowStruct, then `datatable_rows_set` rows (each with Name).
4. `data_asset_create` path/class.

## Verify

`datatable_rows_get` returns the rows.

## On failure

`data.import_failed`: compare row fields with `datatable_rows_get`.
