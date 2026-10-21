# Tasks: エージェント連携と AI 系エディタ機能の拡張

## Phase 1: US1 - SessionStart フック (P1) 🎯 MVP

- [x] T001 [US1] `tests/us13_hook.rs` に session-start(.uproject あり / なし / 親ディレクトリ)と install(新規・既存キー保持・冪等・不正 JSON・dry-run)のテストを書き、失敗を確認する
- [x] T002 [US1] `src/hook.rs` を実装し、`src/cli.rs` に `hook` コマンドを追加する
- [x] T003 [US1] README / README.ja.md と `unrealengine-cli-usage` スキルに `hook` を追記する

## Phase 2: US2 - ツール作成スキル (P2)

- [x] T004 [US2] `skills/unreal-cli-extend/SKILL.md` を書き、`skills lint --severity error` を通す

## Phase 3: US3 - Behavior Tree の強化 (P2)

- [ ] T005 [P] [US3] `bt_remove_node` / `bt_set_node_property` / `blackboard_get` の E2E を `tests/e2e_editor.rs` に書く
- [ ] T006 [US3] `Private/Tools/AiTools.cpp` に 3 ツールを実装し、`skills/unreal-ai` を更新する

## Phase 4: US4 - Control Rig の階層 (P3)

- [ ] T007 [P] [US4] `controlrig_get` / `controlrig_add_bone` / `controlrig_add_control` / `controlrig_add_null` の E2E を書く
- [ ] T008 [US4] `tools/anim_advanced.py` に実装し、`skills/unreal-advanced-animation` を更新する

## Phase 5: US5 - State Tree (P2)

- [ ] T009 [P] [US5] State Tree の E2E を書く(作成 → ステート 2 つ → タスク → 遷移 → コンパイル → get)
- [ ] T010 [US5] uplugin に StateTree / GameplayStateTree を Optional 追加、Build.cs に依存を追加する
- [ ] T011 [US5] `Private/Tools/StateTreeTools.cpp` に 6 ツールを実装する
- [ ] T012 [US5] `skills/unreal-state-tree/SKILL.md` を書く

## Phase 6: 仕上げ

- [ ] T013 `scripts/gen-catalog.sh` でカタログを再生成し、README のツール数を更新する
- [ ] T014 品質ゲートと E2E(UE 5.7)を通す
