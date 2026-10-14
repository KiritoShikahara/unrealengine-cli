# Tool Catalog v1

The source of truth is the running editor (`tools_list`); the bundled snapshot is `tools/catalog.json` and the
human-readable list is generated into `docs/tools.md` (`scripts/gen-docs.sh`).

Implemented: 113 tools in 21 groups (blueprint 17, level 11, asset 10, editor 10, anim 7, property 6, data 6,
material 6, runtime 5, project 5, umg 5, jobs 4, packaging 3, niagara 4, input 3, landscape 3, sequencer 3,
system 2, code 1, python 1, audio 1).

Differences from the planned list:

* `class_create` is a CLI command (`unreal-cli code class-create`), not an editor tool.
* Added: `editor_quit`, `level_info`, `asset_list`, `material_recompile`, `niagara_emitters_list`, `landscape_info`,
  `package_status`.
* Not implemented: `landscape_paint`, `landscape_layer_add`, `niagara_input_set`, `cook_content`
  is a BuildCookRun cook only.
