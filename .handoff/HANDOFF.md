# 引き継ぎ (2026-11-24 12:13 / 006-handoff)

次の一手: `git switch main && git pull` のあと、UE 5.7 でエディタを開いて PCG の E2E 失敗を再現する（`UNREALENGINE_CLI_E2E=1 cargo test -q --test e2e_editor -- --test-threads=1`）

## 完了
- スキルを Claude Code プラグインとして配布: `.claude-plugin/`、CLI の自動導入（`skills/unrealengine-cli-usage/SKILL.md`）、`.agents/skills` symlink（PR #23）
- SessionStart フックをプラグインに同梱: `hooks/hooks.json`。「unrealengine-cli で BP を操作できる」前提が毎回入る（PR #23）
- スキルと `scripts/e2e.sh` は既定でエディタのウィンドウを開く（ウィンドウなしは `UNREALENGINE_CLI_E2E_NULL_RHI=1`）（PR #23）
- Issue #24〜#26 修正: 配列関数ノード、`bp_set_pin` の Enum 表示名と Rotator 書式（PR #27）
- BP ノードを重ならない位置に置く: `BlueprintTools.cpp` の `NodeSize` / `PositionNode`（PR #28）

## 残り（優先順）
- `e2e_28_audio_physics_pcg`: 単体では成功、全件連続実行だと `pcg.timeout`（generated=false, generating=false）。前に走るテストの影響。`e2e_2` 範囲で絞り込み途中
- `e2e_30_localization`: 1 回だけ失敗（ja のプレビューが原文のまま）。2 回目は成功。不安定さの原因は未調査
- BP の自動整列（実行の流れに沿って左→右に並べる）は未実装。必要なら新ツールとして追加

## 再開に必要なもの
- Rust: `winget install Rustlang.Rustup`（`%USERPROFILE%\.cargo\bin` を PATH に）
- UE 5.7（E2E 用）
- `TestProject/Plugins/UnrealCliBridge` は `Plugin/UnrealCliBridge` へのジャンクション（git 外）。無ければ PowerShell で `New-Item -ItemType Junction -Path TestProject\Plugins\UnrealCliBridge -Target (Resolve-Path Plugin\UnrealCliBridge)`
- `TestProject.uproject` の `EngineAssociation` は `scripts/dev-build.sh <ver>` が書き換える。別バージョンのビルドが残っているとエディタが即終了するので `bash scripts/dev-build.sh 5.7` してから開く
- Claude Code プラグイン: `claude plugin marketplace add KiritoShikahara/unrealengine-cli` → `claude plugin install unrealengine-cli@unrealengine-cli`
