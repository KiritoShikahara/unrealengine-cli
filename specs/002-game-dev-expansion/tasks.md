# Tasks: ゲーム制作機能の拡張

**Input**: `specs/002-game-dev-expansion/` 配下の spec.md / plan.md / research.md / data-model.md / contracts/
**Tests**: 必須(FR-044、憲章 III)。各ストーリーでは、テストを先に書いて失敗を確認してから実装する。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 他のタスクと並列に進められる(別ファイルで、依存がない)
- **[Story]**: どのユーザーストーリーのタスクか(US1〜US12)

## Phase 1: Setup

- [x] T001 `Plugin/UnrealCliBridge/UnrealCliBridge.uplugin` に任意依存のプラグイン(GameplayAbilities、PCG、Metasound、GeometryCollectionPlugin、IKRig、ControlRig、PoseSearch、OnlineSubsystem、OnlineSubsystemNull)を `Optional: true` で追加し、無効なプロジェクトでもブリッジが読み込めることを TestProject で確認する(実装メモ: GAS はリフレクションだけで扱い、コンパイル時の依存を持たない方式にしたため不要になった。OnlineSubsystem と GameplayTagsEditor は既定で有効なので通常の依存にした)
- [ ] T002 [P] `Private/Compat/EngineCompat.h` に、5.5 / 5.6 で差のある API の置き場(マクロ `UCLI_ENGINE_AT_LEAST(5, 6)` など)を作る
- [x] T003 [P] `Private/Server/ToolRegistry.cpp` に、ツールごとの必要プラグインを宣言し、無効なら `<group>.plugin_disabled` を返す仕組みを追加する(Automation Spec を先に書く)(実装メモ: GAS はクラスが見つからないとき `gas.not_enabled` を返す `RequireGasClass` で実現。PCG などは US9 で同じ方式を使う)

## Phase 2: Foundational(全ストーリーの前提)

- [x] T004 PIE ワールド解決の Automation Spec(PIE は Automation から起動できないため、E2E `e2e_21_multiplayer_pie` で検証する) を `Private/Tests/PieWorldSpec.cpp` に書く(standalone / server / client:n、存在しない id で `pie.world_not_found`)
- [x] T005 `Private/Tools/PlayTools.cpp` の「最初の PIE ワールド」ヘルパーを、`world` 引数で選べる `ResolvePieWorld` に置き換える(省略時の挙動は変えない)

## Phase 3: US1 - マルチプレイの PIE (P1) 🎯 MVP

- [x] T006 [P] [US1] `pie start --players --net-mode` と `pie worlds`、`--world` の引数対応のモックテストを `tests/us11_net.rs` に書く
- [x] T007 [P] [US1] E2E `e2e_21_multiplayer_pie` を `tests/e2e_editor.rs` に書く(2 人の Listen Server、各ワールドの PlayerController 数、クライアント 1 への入力)
- [x] T008 [US1] `pie_start` に players / netMode を追加し、`ULevelEditorPlaySettings` を一時変更して終了時に戻す(`Private/Tools/PlayTools.cpp`)
- [x] T009 [US1] `pie_worlds` と、既存 PIE ツール(actors、input、wait、screenshot、widgets、click)の `world` 引数を実装する
- [x] T010 [US1] アクター情報に role / remoteRole / owner / netGuid を追加する
- [x] T011 [US1] `src/commands/typed.rs` に `--players`、`--net-mode`、`--world`、`pie worlds` を追加する

## Phase 4: US2 - レプリケーションと RPC (P1)

- [x] T012 [P] [US2] `blueprint replicate` / `blueprint rpc` / `code replicate` / `code rpc` のモックテストを `tests/us11_net.rs` に追加する
- [x] T013 [P] [US2] `code replicate` / `code rpc` のテキスト編集のテスト(ヘッダーと .cpp の入力 → 期待出力、dry-run)を `tests/us11_net.rs` に書く(エディタ不要の CLI 側実装にしたため cargo test で検証。生成コードは TestProject で UE 5.7 のビルドを確認)
- [x] T014 [P] [US2] E2E `e2e_22_replication` を書く(RepNotify 変数と Server RPC の Blueprint、PIE でサーバーの変更がクライアントに届き OnRep が呼ばれる)
- [x] T015 [US2] `bp_set_replication`(CPF_Net、CPF_RepNotify、RepNotifyFunc、ReplicationCondition、OnRep 関数の作成)を `Private/Tools/ReplicationTools.cpp` に実装する
- [x] T016 [US2] `bp_set_rpc`(FUNC_Net* と Reliable)を実装する
- [x] T017 [US2] `code_add_replicated_property` / `code_add_rpc` を実装する(`GetLifetimeReplicatedProps` と include の自動追加、差分の事前表示、dry-run)
- [x] T018 [US2] ネットワーク関連プロパティの別名(NetUpdateFrequency など)が prop で読み書きできることを E2E で確認し、足りなければ `tools/prop.py` に別名を追加する

## Phase 5: US3 - 同期の検証 (P1)

- [x] T019 [P] [US3] `net assert` / `net emulate` のモックテストを書く(不一致時の `net.mismatch` と終了コード 4)
- [x] T020 [P] [US3] E2E `e2e_23_net_assert` を書く(一致、タイムアウト時の不一致、遅延 200ms でも一致まで待つ)
- [x] T021 [US3] `net_assert` を `Private/Tools/NetTools.cpp` に実装する(全ワールドで値を取り、ティッカーで一致を待つ)
- [x] T022 [US3] `net_emulate` を実装する(PIE 開始前は NetworkEmulationSettings、実行中は各 NetDriver の PacketSimulationSettings)
- [x] T023 [US3] `scripts/e2e-net.py` を作る(2 人のゲーム: 移動の同期、スコアのレプリケーション、Server RPC、遅延下の検証)。SC-001

## Phase 6: US4 - 専用サーバー (P2)

- [x] T024 [P] [US4] `server target add` の生成内容、dry-run、インストール版の判定(`InstalledBuild.txt`)で `server.source_engine_required` になることのテストを `tests/us12_server.rs` に書く
- [x] T025 [P] [US4] サーバー記録(`servers/<pid>.json`)と、記録にない PID を `server stop` が拒否するテストを書く
- [x] T026 [US4] `src/commands/server.rs` に `server target add`(`<Project>Server.Target.cs`)を実装する
- [x] T027 [US4] `server build` / `server package` を実装する(001 のビルドエラー解析とパッケージ出力先の安全確認を再利用)
- [x] T028 [US4] `src/servers.rs` に `server start|list|logs|stop` を実装する(ハンドル非継承の起動、`Listening` の待機、`server.port_in_use`、`server.exited`)
- [x] T029 [US4] `pie_connect` を実装する(PIE クライアントから `open <host:port>`)
- [x] T030 [US4] ソース版エンジンでの手動検証手順を quickstart に沿って実施し、結果を記録する(ソース版エンジンがない場合は保留として記録)
  - 結果: ソース版エンジンがないため `server build` / `server package` の実ビルドは保留。インストール版 5.7 で `server build` が即座に `server.source_engine_required` を返すこと、エディタ実行ファイルのサーバーモードでの `server start`(4 秒で待ち受け)→ `pie connect 127.0.0.1:7781` → サーバーログに `Join succeeded` → `server stop` を確認済み

## Phase 7: US5 - オンラインセッション (P2)

- [x] T031 [P] [US5] `online configure` の ini と .uproject の編集、`online check` の不足キー、秘密の伏せ字のテストを `tests/us13_online.rs` に書く
- [x] T032 [P] [US5] E2E `e2e_24_online_null` を書く(Null で作成 → 検索 → 参加 → 破棄)
- [x] T033 [US5] `online_configure` / `online_check` を `Private/Tools/OnlineTools.cpp` に実装する(実装メモ: ini と .uproject の編集はエディタ不要のため CLI 側 `src/commands/online.rs` に置いた)
- [x] T034 [US5] `online_session` を実装する(IOnlineSession の各非同期処理をティッカーで待つ)

## Phase 8: US6 - GAS (P2)

- [x] T035 [P] [US6] `gas enable` の .uproject / .Build.cs 編集のテストを `tests/us14_gas.rs` に書く
- [x] T036 [P] [US6] AttributeSet の生成コードの Automation Spec を書く(実装メモ: 生成コードは `tests/us14_gas.rs` で検査し、TestProject の UHeroAttributes を 5.8 で実際にコンパイルして確認)
- [x] T037 [P] [US6] `scripts/e2e-gas.py` を書く(属性、Effect で Health -10、アビリティの付与と発動)。SC-004(実装メモ: E2E `e2e_25_gas` として実装)
- [x] T038 [US6] `gas enable` を実装する(事前表示と dry-run)
- [x] T039 [US6] `gas_attribute_set_create` を実装する(実装メモ: エディタ不要のため CLI 側 `gas attribute-set` として実装)
- [x] T040 [US6] `gas_ability_create` / `gas_effect_create` / `gas_cue_create` を実装する(Effect の Modifier・期間・スタック・タグ)
- [x] T041 [US6] `gas_runtime`(give、activate、apply、state)を実装する
- [x] T042 [US6] `tags_add|list|rename|remove` を `Private/Tools/TagTools.cpp` に実装する(US10 と共有。テストを先に書く)

## Phase 9: US7 - ライティングとレンダリング (P2)

- [ ] T043 [P] [US7] E2E `e2e_25_lighting` を書く(屋外プリセット、スクリーンショットの平均輝度、ライティングのビルドのジョブ)
- [ ] T044 [US7] `light_preset` を実装する(Directional、Sky Light、SkyAtmosphere、Fog、PostProcessVolume。屋内版も)
- [ ] T045 [US7] `light_build` / `light_hlod_build` をジョブとして実装する(`lighting.build_in_progress`)
- [ ] T046 [US7] `render_settings` を実装する(Lumen、仮想シャドウマップ、Nanite、AA。変更前後の値を返す)

## Phase 10: US8 - 高度なアニメーション (P3)

- [ ] T047 [P] [US8] E2E `e2e_26_ik_retarget` を書く(マネキンの IK Rig 2 つ、Retargeter、1 本のリターゲット)
- [ ] T048 [US8] `anim_ikrig_create` / `anim_retargeter_create` / `anim_retarget` を Python で実装する(`tools/anim_advanced.py`)
- [ ] T049 [US8] `controlrig_create` と `posesearch_create` を実装する

## Phase 11: US9 - MetaSound / Chaos / PCG (P3)

- [ ] T050 [P] [US9] E2E `e2e_27_audio_physics_pcg` を書く(MetaSound の作成と接続、Geometry Collection の分割、PCG で 100 本の配置)
- [ ] T051 [US9] `metasound_create|add_node|connect|set_input` を実装する
- [ ] T052 [US9] `geometry_collection_create` を実装する
- [ ] T053 [US9] `pcg_create|add_node|connect|generate` を実装する

## Phase 12: US10 - ローカライズ、タグ、セーブ (P3)

- [ ] T054 [P] [US10] E2E `e2e_28_localization` を書く(ja の追加、収集、PO の取り込み、コンパイル、`-culture=ja` の表示)
- [ ] T055 [US10] `loc_add_culture|gather|export|import|compile` を実装する(コマンドレットをジョブで実行)
- [ ] T056 [US10] SaveGame のバージョン移行の E2E を e2e_19 に追加し、スキルに手順を書く

## Phase 13: US11 - 見た目とパフォーマンス (P2)

- [ ] T057 [P] [US11] 画像差分の単体テストを `tests/us15_visual.rs` に書く(同一画像で 0%、1 ピクセル変更、解像度違いで `visual.size_mismatch`、しきい値超えで終了コード 4)
- [ ] T058 [P] [US11] E2E `e2e_29_visual_perf` を書く(基準と比較、ライトを消して `visual.mismatch`、10 秒の計測)
- [ ] T059 [US11] `src/commands/visual.rs` に `visual baseline|compare|diff` を実装する(`image` クレート)
- [ ] T060 [US11] `perf_measure` を `Private/Tools/PerfTools.cpp` に実装する(`partial` を含む)
- [ ] T061 [US11] `perf_trace` と `perf trace summary`(Insights の CSV 出力の集計)を実装する

## Phase 14: US12 - 対応範囲 (P2)

- [ ] T062 [US12] UE 5.5 と 5.6 でプラグインをビルドし、コンパイルエラーを `Private/Compat/` の分岐で直す
- [ ] T063 [US12] `scripts/e2e.sh 5.5` / `5.6` を全件成功させる。使えない機能は `MinEngine` と `engine.unsupported` にする
- [ ] T064 [P] [US12] `tests/e2e_fab.rs` を作る(`UNREALENGINE_CLI_E2E_FAB=1` のときだけ。ライブラリ検索、パックと FBX の取り込み、保存、タブが閉じること)

## Phase 15: Polish

- [ ] T065 [P] スキルを追加する: unreal-multiplayer、unreal-dedicated-server、unreal-online-sessions、unreal-gas、unreal-lighting、unreal-advanced-animation、unreal-audio-physics-pcg、unreal-visual-perf-testing(`skills lint` を通す)
- [ ] T066 [P] `tools/catalog.json` と `docs/tools.md` を再生成し、README / README.ja のツール数と「実機で通した E2E」の表を更新する
- [ ] T067 [P] `docs/skills.md`、`CHANGELOG.md`、`CLAUDE.md`(新しい制約)を更新する
- [ ] T068 品質ゲート(fmt、clippy、test、skills lint)と、UE 5.5〜5.8 の E2E を実行する
- [ ] T069 エージェントによる評価: Claude Code に「2 人で遊べる対戦ゲームを作って検証して」と依頼し、人の介入なしで `scripts/e2e-net.py` と同等の検証まで到達するかを記録する

## Dependencies & Execution Order

- Phase 1〜2 → US1 → (US2 → US3)、(US4、US5、US6、US11 の perf)は US1 の後に並列可
- US7〜US10、US11 の visual は Phase 2 の後なら独立
- US12 と Polish は最後
- 各ストーリーの中では「テスト(失敗を確認)→ 実装 → 型付きコマンド → E2E が通る」の順
