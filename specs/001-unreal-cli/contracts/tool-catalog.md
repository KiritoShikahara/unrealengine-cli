# Tool Catalog v1(目標 100 個以上)

| グループ | 主なツール | 個数の目安 |
|---|---|---|
| system / editor | system_ping, tools_list, editor_info, editor_logs, editor_undo, editor_redo, editor_save_all, editor_selection_get/set, editor_viewport_camera, editor_screenshot | 11 |
| project | project_info, project_settings_get/set, plugin_list/enable | 5 |
| blueprint | bp_create, bp_get, bp_graph_get, bp_add_node, bp_remove_node, bp_connect, bp_disconnect, bp_set_pin, bp_add_variable, bp_remove_variable, bp_add_function, bp_add_component, bp_compile, bp_save, bp_search_nodes, bp_reparent, bp_implement_interface | 17 |
| level | level_new, level_open, level_save, actor_list, actor_spawn, actor_destroy, actor_transform, actor_attach, actor_duplicate, actor_find | 10 |
| asset | asset_search, asset_create, asset_duplicate, asset_rename, asset_delete, asset_import, asset_save, asset_references, asset_dependencies | 9 |
| property | prop_get, prop_set, prop_array_edit, component_list, component_add, component_remove | 6 |
| code | code_search, code_read, code_edit, code_apply, code_live_compile, code_rebuild, class_create | 7 |
| runtime / test | pie_run, pie_stop, pie_status, automation_list, automation_run | 5 |
| anim | animbp_create, statemachine_add, state_add, transition_add, montage_create, blendspace_create, sequence_player_add | 7 |
| umg | widget_create, widget_tree_get, widget_add, widget_set_prop, widget_bind | 5 |
| material | material_create, material_node_add, material_connect, mi_create, mi_set_param | 5 |
| niagara | niagara_system_create, emitter_add, module_add, niagara_input_set | 4 |
| landscape | landscape_create, landscape_sculpt, landscape_paint, landscape_layer_add | 4 |
| input | input_action_create, mapping_context_create, mapping_add | 3 |
| data | datatable_create, datatable_rows_get/set, data_asset_create, struct_create, enum_create | 6 |
| audio / sequencer | soundcue_create, sequence_create, sequence_track_add, sequence_key_add | 4 |
| packaging | package_project, cook_content | 2 |
| python / jobs | python_exec, jobs_list, jobs_get, jobs_logs, jobs_cancel | 5 |

合計の目安は 115 個。各ツールの入出力スキーマは `tools_list` が正本。
