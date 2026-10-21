# 実装計画: エージェント連携と AI 系エディタ機能の拡張

**Branch**: `003-editor-ai-expansion` | **Date**: 2026-10-21 | **Spec**: [spec.md](spec.md)

## Summary

CLI に `hook` コマンド群(SessionStart の文脈出力と settings.json への登録)を加え、ツール追加用の同梱スキルを書き、Behavior Tree(C++)・Control Rig(Python)・State Tree(C++)のツールを追加する。通信、エンベロープ、スキル契約、MCP は 001 / 002 のものを使う。

## Technical Context

**Language/Version**: Rust 1.85+(edition 2024)、C++20(UE 5.7 / 5.8 Editor モジュール)、Python 3
**Primary Dependencies**: 既存のみ。UE 側に `StateTreeModule` / `StateTreeEditorModule` / `GameplayStateTreeModule`(StateTree・GameplayStateTree プラグイン、uplugin で Optional)
**Testing**: cargo test(hook はファイル入出力の単体・統合テスト、ツールはモック Bridge)、E2E(`tests/e2e_editor.rs`)
**Constraints**: フックはエディタ不要・ネットワーク不要。settings.json の既存内容を壊さない

## Constitution Check

| 原則 | 評価 |
|---|---|
| I. 独立性 | PASS: 参考プラグインのコード・名前は使わず、フックは自前の Rust 実装 |
| II. CLI ファースト | PASS: すべて CLI コマンド / `tool call` で使え、JSON を返す |
| III. テストファースト | PASS: tasks で各ストーリーのテストを先に置く |
| IV. 契約テスト | PASS: カタログ一致テストと README のツール数テストを更新 |
| V. 安全 | PASS: settings.json はマージのみ・dry-run・不正 JSON なら書かない |
| VI. シンプルさ | PASS: リググラフ編集など頻度の低い操作は範囲外(Python 実行で可能) |

## 設計

- `src/hook.rs`: `find_uproject(cwd)`、`session_context(uproject) -> Value`、`merge_settings(existing: Value, command) -> (Value, changed)`。`src/cli.rs` に `Command::Hook { HookCmd::SessionStart | Install { client, global } }`。
- フックの登録コマンドは `"<現在の exe の絶対パス>" hook session-start`。
- BT: `Private/Tools/AiTools.cpp` に追記。プロパティ設定は既存の prop ヘルパー(`FProperty::ImportText`)と同じ方式。
- Control Rig: `tools/anim_advanced.py` に `URigHierarchyController` 経由の関数を追加。
- State Tree: `Private/Tools/StateTreeTools.cpp` を新規。`UStateTreeEditorData` を編集し `FStateTreeCompiler` でコンパイル。
