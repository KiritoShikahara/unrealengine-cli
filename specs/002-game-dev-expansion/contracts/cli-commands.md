# CLI Commands(追加分)

共通オプション、エンベロープ、終了コードは 001(`specs/001-unreal-cli/contracts/cli-commands.md`)と同じ。

| コマンド | 内容 | ツール |
|---|---|---|
| `pie start --players <n> --net-mode standalone\|listen\|client` | マルチプレイの PIE | `pie_start`(引数追加) |
| `pie worlds` | PIE のワールド一覧 | `pie_worlds` |
| `pie actors\|input\|wait\|screenshot\|widgets\|click ... --world <id>` | ワールドを指定した既存操作 | 既存ツール(`world` 引数追加) |
| `net assert <actor> <property> [--expect <json>] [--timeout <s>]` | 同期アサーション | `net_assert` |
| `net emulate --latency <ms> --jitter <ms> --loss <pct> [--target ...]` / `net emulate --off` | ネットワークの模擬 | `net_emulate` |
| `blueprint replicate <bp> <var> [--notify] [--condition <c>]` | 変数のレプリケーション | `bp_set_replication` |
| `blueprint rpc <bp> <event> --type server\|client\|multicast [--reliable]` | RPC のイベント | `bp_set_rpc` |
| `code replicate <class> <prop> <type> [--notify] [--condition <c>]` | C++ のレプリケートするプロパティ | `code_add_replicated_property` |
| `code rpc <class> <name> --type ... [--reliable] [--validate] [--param name:type]` | C++ の RPC | `code_add_rpc` |
| `server target add` | Server ターゲットの作成 | CLI 内(ファイル操作) |
| `server build [--config Development\|Shipping]` / `server package --out-dir <dir>` | ビルドとパッケージ | CLI 内(UBT / UAT) |
| `server start [--map <m>] [--port <p>] [-- <args>]` / `server list` / `server logs <pid>` / `server stop <pid>` | 専用サーバーの実行 | CLI 内(プロセス管理) |
| `pie connect <host:port>` | PIE クライアントを外部サーバーへ接続 | `pie_connect` |
| `online configure --subsystem null\|eos\|steam [--set key=value]` / `online check` | オンライン構成 | `online_configure`、`online_check` |
| `online session create\|find\|join\|destroy [--world <id>]` | セッション操作 | `online_session` |
| `gas enable` | GAS の有効化 | CLI 内(.uproject と .Build.cs) |
| `gas attributes <class> <name[=default]>...` | AttributeSet の生成 | `gas_attribute_set_create` |
| `gas ability\|effect\|cue create <path> [...]` | GAS アセット | `gas_ability_create`、`gas_effect_create`、`gas_cue_create` |
| `gas give\|activate\|apply\|state <actor> ... [--world <id>]` | PIE での検証 | `gas_runtime` |
| `tags add\|list\|rename\|remove` | Gameplay Tags | `tags_*` |
| `light preset outdoor\|indoor` / `light build` / `light hlod` | ライティング | `light_preset`、`light_build`、`light_hlod_build` |
| `render get\|set <setting> [value]` | レンダリング設定 | `render_settings` |
| `anim ikrig\|retarget\|controlrig\|posesearch ...` | 高度なアニメーション | `anim_ikrig_create` ほか |
| `audio metasound create\|node\|connect\|set` | MetaSound | `metasound_*` |
| `physics collection <path> --mesh <mesh>...` | Geometry Collection(分割は Fracture モードで行う。分割 API はスクリプトから呼べないため) | `geometry_collection_create` |
| `pcg create\|node\|connect\|generate` | PCG(generate は `pcg_status` で完了を待つ) | `pcg_*` |
| `loc add-culture\|gather\|export\|import\|compile` | ローカライズ | `loc_*` |
| `visual baseline <name>` / `visual compare <name> [--threshold <pct>]` / `visual diff <a> <b>` | 画像差分 | CLI 内(画像処理)と `pie_screenshot` / `editor_screenshot` |
| `perf measure --seconds <s>` / `perf trace start\|stop` / `perf trace summary <file>` | 性能計測 | `perf_measure`、`perf_trace` |

## 新しいエラーコード

| コード | 終了コード | 意味 |
|---|---|---|
| `pie.world_not_found` | 2 | `--world` が存在しない(有効な一覧を details に入れる) |
| `net.mismatch` | 4 | タイムアウトまでに一致しなかった |
| `server.source_engine_required` | 4 | インストール版エンジンでは専用サーバーをビルドできない |
| `server.no_target` | 4 | Server ターゲットがない |
| `server.port_in_use` | 4 | ポートが使用中 |
| `server.exited` | 4 | 起動直後に終了した(ログの末尾を details に入れる) |
| `online.missing_settings` | 4 | 必須キーが不足 |
| `online.session_failed` | 4 | セッション操作が失敗 |
| `gas.not_enabled` | 4 | GAS が無効 |
| `lighting.build_in_progress` | 4 | ビルド中 |
| `<group>.plugin_disabled` | 4 | 必要なエンジンプラグインが無効 |
| `visual.size_mismatch` | 2 | 画像の解像度が異なる |
| `visual.mismatch` | 4 | 差分がしきい値を超えた |
| `engine.unsupported` | 4 | このエンジンバージョンでは使えない |
