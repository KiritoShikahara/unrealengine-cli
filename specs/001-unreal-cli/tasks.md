# Tasks: unreal-cli

**Input**: `specs/001-unreal-cli/` 配下の spec.md / plan.md / research.md / data-model.md / contracts/
**Tests**: 必須(FR-050、憲章 III)。各ストーリーでは、テストを先に書いて失敗を確認してから実装する。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 他のタスクと並列に進められる(別ファイルで、依存がない)
- **[Story]**: どのユーザーストーリーのタスクか(US1〜US9)

## Phase 1: Setup

- [x] T001 `Cargo.toml` と `src/main.rs` を作成する(crate 名 `unreal-cli`、edition 2024)
- [x] T002 [P] `rustfmt.toml`、`clippy.toml`、`.gitignore` を作成する
- [x] T003 [P] `.github/workflows/ci.yml` を作成する(fmt / clippy -D warnings / test。OS は windows・macos・ubuntu)
- [x] T004 [P] `Plugin/UnrealCliBridge/UnrealCliBridge.uplugin` と `Source/UnrealCliBridge/UnrealCliBridge.Build.cs` を作成する
- [x] T005 [P] E2E 用の `TestProject/TestProject.uproject`(C++ の空プロジェクト)を作成する
- [x] T006 [P] `CLAUDE.md` と `README.md` / `README.ja.md` の骨格を作成する

## Phase 2: Foundational(全ストーリーの前提)

- [x] T007 [P] エンベロープのスキーマ準拠テストを `tests/contract_envelope.rs` に書く(`contracts/cli-envelope.schema.json` で検証)
- [x] T008 [P] フレーム読み書きとハンドシェイクのテストを `tests/contract_protocol.rs` に書く
- [x] T009 `src/core/envelope.rs`、`src/core/error.rs`、`src/core/exit_code.rs` を実装する
- [x] T010 `src/cli.rs` に clap の共通オプション(`--output`、`--project`、`--instance`、`--timeout-ms`、`--dry-run`、`-v`)を実装する
- [x] T011 `src/transport/frame.rs` と `src/transport/client.rs` を実装する(hello / call / result)
- [x] T012 [P] テスト用のモック Bridge を `tests/support/mock_bridge.rs` に実装する
- [x] T013 `Private/Server/BridgeServer.cpp` を実装する(127.0.0.1 で待ち受け、トークン認証、ゲームスレッドへの転送)
- [x] T014 `Private/Server/ToolRegistry.cpp` を実装する(登録、`tools_list`、引数のスキーマ検証)
- [x] T015 `Private/Server/JobManager.cpp` を実装し、jobs_* ツールを公開する
- [x] T016 `Private/Server/InstanceFile.cpp` を実装する(`%LOCALAPPDATA%/unreal-cli/instances/<pid>.json` の書き込みと削除)
- [x] T017 [P] `Private/Compat/EngineCompat.h` に 5.5〜5.8 の分岐の置き場を作る

## Phase 3: US1 - 接続と状態確認 (P1) 🎯 MVP

- [x] T018 [P] [US1] `instances list` / `ping` / `logs` の統合テストを `tests/us1_connect.rs` に書く(モック使用。未起動時は終了コード 3 と `connect.no_instance`)
- [x] T019 [P] [US1] `system_ping` / `editor_logs` の Automation Spec を `Private/Tests/SystemToolsSpec.cpp` に書く
- [x] T020 [US1] `src/instances/mod.rs` を実装する(検出、PID の生存確認、.uproject からの選択)
- [x] T021 [US1] `Private/Tools/SystemTools.cpp` を実装する(system_ping、editor_info、editor_logs と since カーソル)
- [x] T022 [US1] `src/commands/{ping,logs,instances}.rs` を実装する

## Phase 4: US2 - 導入・起動・ビルド (P1)

- [x] T023 [P] [US2] エンジン検出(レジストリと LauncherInstalled.dat)のテストを `tests/us2_engine.rs` に書く
- [x] T024 [P] [US2] MSVC / clang のビルドエラー解析のテストを `tests/us2_build_parse.rs` に書く(fixtures にログを置く)
- [x] T025 [P] [US2] setup の dry-run で変更がないことのテストを `tests/us2_setup.rs` に書く
- [x] T026 [US2] `src/engine/locate.rs` と `src/engine/build.rs` を実装する(Build.bat、BuildDiagnostic)
- [x] T027 [US2] `src/setup/mod.rs` を実装する(include_dir で同梱したプラグインの展開、.uproject の編集、変更の事前表示)
- [x] T028 [US2] `src/engine/editor.rs` を実装する(`editor open|close|status`、ping が通るまで待機)
- [x] T029 [US2] `src/commands/doctor.rs` を実装する

## Phase 5: US3 - Blueprint (P1)

- [x] T030 [P] [US3] bp_* ツールの Automation Spec を `Private/Tests/BlueprintToolsSpec.cpp` に書く(BeginPlay → PrintString、コンパイルエラー時の保存拒否)
- [x] T031 [P] [US3] `blueprint` サブコマンドの統合テストを `tests/us3_blueprint.rs` に書く
- [x] T032 [US3] `Private/Tools/BlueprintTools.cpp` を実装する(tool-catalog.md の 17 個。変更は FScopedTransaction で記録)
- [x] T033 [US3] `src/commands/blueprint.rs` と `src/tools/call.rs`(`tool list/find/describe/call`)を実装する

**Checkpoint**: P1 完了。quickstart.md の手順 1〜6 が通る。

## Phase 6: US4 - レベル・アクター・アセット・プロパティ (P2)

- [x] T034 [P] [US4] level / asset / property の Automation Spec を `Private/Tests/LevelAssetSpec.cpp` に書く(存在しないプロパティ名で候補を返すことも確認)
- [x] T035 [US4] `Private/Tools/LevelTools.cpp` を実装する
- [x] T036 [US4] `Private/Tools/AssetTools.cpp` を実装する
- [x] T037 [US4] `Private/Tools/PropertyTools.cpp` を実装する(リフレクション、ドット区切りパス、配列の添字)
- [x] T038 [US4] `src/commands/{level,asset,prop}.rs` を実装する

## Phase 7: US5 - C++ の編集と反映 (P2)

- [x] T039 [P] [US5] 反映方法の判定(live か rebuild か)のテストを `tests/us5_apply_plan.rs` に書く
- [x] T040 [US5] `src/code/{search,read,edit}.rs` を実装する
- [x] T041 [US5] `src/code/apply.rs` を実装する(ハッシュの差分、未保存アセットの確認、--force)
- [x] T042 [US5] `Private/Tools/CodeTools.cpp` を実装する(code_live_compile、class_create)

## Phase 8: US6 - PIE と自動テスト (P2)

- [x] T043 [P] [US6] PIE と automation の Automation Spec を `Private/Tests/RuntimeToolsSpec.cpp` に書く
- [x] T044 [US6] `Private/Tools/RuntimeTools.cpp` を実装する(pie_run の stopOnLog、automation_run をジョブとして実行)
- [x] T045 [US6] `src/commands/{pie,test}.rs` を実装する

## Phase 9: US8 - スキル・ツール探索・MCP (P2)

- [x] T046 [P] [US8] skills install / lint のテストを `tests/us8_skills.rs` に書く(5 クライアント × local/global。lint 12 ルール)
- [x] T047 [P] [US8] MCP の tools/list と call のテストを `tests/us8_mcp.rs` に書く
- [x] T048 [US8] `src/skills/{install,lint}.rs` を実装する
- [x] T049 [US8] `src/mcp/{serve,configure}.rs` を実装する
- [x] T050 [US8] `tools/catalog.json` の生成スクリプトと、実エディタとの一致を確かめる契約テストを作る
- [x] T051 [P] [US8] スキル 19 個を `skills/unreal-*/SKILL.md` に作成する(skill-contract.md に準拠)

## Phase 10: US7 - 専門領域 (P3)

各グループで「Automation Spec を書く → 実装 → E2E で確認」の順に進める。

- [x] T052 [P] [US7] Python 実行(`python_exec`、タイムアウト)を `Private/Tools/PythonTools.cpp` に実装し、テストを書く
- [x] T053 [P] [US7] アニメーションを `Private/Tools/AnimTools.cpp` に実装する
- [x] T054 [P] [US7] UMG を `Private/Tools/UmgTools.cpp` に実装する
- [x] T055 [P] [US7] マテリアルを `Private/Tools/MaterialTools.cpp` に実装する
- [x] T056 [P] [US7] Niagara を `Private/Tools/NiagaraTools.cpp` に実装する
- [x] T057 [P] [US7] ランドスケープを `Private/Tools/LandscapeTools.cpp` に実装する
- [x] T058 [P] [US7] Enhanced Input とデータを `Private/Tools/InputDataTools.cpp` に実装する
- [x] T059 [P] [US7] オーディオと Sequencer を `Private/Tools/SequencerAudioTools.cpp` に実装する
- [x] T060 [P] [US7] パッケージングを `Private/Tools/PackagingTools.cpp` に実装する(ジョブとして実行)
- [x] T061 [US7] `reference search` を `src/reference/mod.rs` に実装する(エンジンソースと Python スタブ)

## Phase 11: US9 - 配布 (P3)

- [x] T062 [P] [US9] チェックサム不一致のテストを `tests/us9_install.rs` に書く
- [x] T063 [US9] `scripts/install.ps1` と `scripts/install.sh` を作成する
- [x] T064 [US9] `src/update/mod.rs` を実装する(`self update`、`UNREAL_CLI_NO_AUTO_UPDATE`)
- [x] T065 [US9] `.github/workflows/release.yml` を作成する(4 ターゲット、SHA256SUMS)。winget と Homebrew の定義も用意する

## Phase 12: Polish

- [ ] T066 (一部完了: UE 5.7 / 5.8 は PASS。5.5 / 5.6 は未インストールのため未検証) `scripts/e2e.ps1` を作成し、UE 5.5 / 5.6 / 5.7 / 5.8 で受け入れ E2E を実行する。結果は `docs/editor-compatibility.md` に記録する
- [x] T067 [P] `docs/architecture.md`、`docs/tools.md`(ツール一覧から自動生成)、`docs/skills.md` を書く
- [x] T068 [P] 性能テストを作る(p95 が 300ms 以下、CLI の起動が 30ms 以下)
- [ ] T069 (未実施: 独立したエージェントセッションでの評価が必要。タスク一覧は docs/eval.md) エージェントによる代表タスク 10 件の評価を `docs/eval.md` に記録する(SC-005)
- [x] T070 独立性を確認する: リポジトリ全体で `unity` / `ue-cli` への参照がないことを grep で検証し、CI に組み込む

## Dependencies

- Phase 1 → Phase 2 → US1 → US2 → US3(ここまでが MVP)
- US4・US5・US6・US8 は US3 の後なら並列に進められる
- US7 は US4(プロパティ操作)の後。US9 はいつでも進められる
- Polish は全ストーリーの後

## 実装戦略

1. MVP: Phase 1〜5(T001〜T033)。quickstart の手順 1〜6 で確認する
2. P2 を追加: US4 → US6 → US5 → US8
3. P3 と Polish
