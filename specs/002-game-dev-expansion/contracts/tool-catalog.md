# Tool Catalog(追加分)

スキーマと登録の規則は 001 の `contracts/tool-catalog.md` に従う。`mutating` のツールは dry-run と Undo(可能なもの)に対応する。

| グループ | ツール | mutating | longRunning | 実装 |
|---|---|---|---|---|
| runtime | `pie_start`(players、netMode 追加)、`pie_worlds`、`pie_connect`、既存 PIE ツールの `world` 引数 | - | - | C++ |
| net | `net_assert`、`net_emulate` | net_emulate | - | C++ |
| blueprint | `bp_set_replication`、`bp_set_rpc` | yes | - | C++ |
| code | `code_add_replicated_property`、`code_add_rpc` | yes(ファイル) | - | C++(テキスト編集) |
| online | `online_configure`、`online_check`、`online_session` | online_configure | online_session | C++ |
| gas | `gas_attribute_set_create`、`gas_ability_create`、`gas_effect_create`、`gas_cue_create`、`gas_runtime` | 作成系 | - | C++ と Python |
| tags | `tags_add`、`tags_list`、`tags_rename`、`tags_remove` | 変更系 | - | C++ |
| lighting | `light_preset`、`light_build`、`light_hlod_build`、`render_settings` | yes | build 系 | C++ |
| anim | `anim_ikrig_create`、`anim_retargeter_create`、`anim_retarget`、`controlrig_create`、`posesearch_create` | yes | anim_retarget | Python |
| audio | `metasound_create`、`metasound_add_node`、`metasound_connect`、`metasound_set_input` | yes | - | C++ |
| physics | `geometry_collection_create` | yes | - | C++ |
| pcg | `pcg_create`、`pcg_add_node`、`pcg_connect`、`pcg_generate` | yes | pcg_generate | C++ |
| loc | `loc_add_culture`、`loc_gather`、`loc_export`、`loc_import`、`loc_compile` | yes | gather / compile | C++(コマンドレット) |
| perf | `perf_measure`、`perf_trace` | - | perf_measure | C++ |

CLI だけで完結するもの(エディタ不要): `server target add|build|package|start|list|logs|stop`、`gas enable`、`visual baseline|compare|diff`、`perf trace summary`。
