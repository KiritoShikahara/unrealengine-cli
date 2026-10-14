# 実装計画: ゲーム制作機能の拡張

**Branch**: `002-game-dev-expansion` | **Date**: 2026-10-15 | **Spec**: [spec.md](spec.md)

## Summary

001 の CLI(Rust)とエディタプラグイン(C++ / Python)に、マルチプレイ(複数ワールドの PIE、レプリケーションと RPC の編集、同期アサーション、ネットワークの模擬)、専用サーバー(ビルド、パッケージ、起動、接続)、オンラインセッション、GAS、ライティング、高度なアニメーション、MetaSound / Chaos / PCG、タグとローカライズ、画像差分と性能計測を加える。あわせて UE 5.5 / 5.6 の検証と Fab の E2E を行う。通信、エンベロープ、ジョブ、スキル、MCP は 001 のものを使う。

## Technical Context

**Language/Version**: Rust 1.85+(edition 2024)、C++20(UE 5.5〜5.8 の Editor モジュール)、Python 3(エディタ内)
**Primary Dependencies**: 001 と同じ。追加: Rust 側 `image`(画像差分)。UE 側 `GameplayAbilities`、`GameplayTags`、`GameplayTasks`、`OnlineSubsystem`、`OnlineSubsystemUtils`、`PCG`、`MetasoundEngine` / `MetasoundFrontend`、`GeometryCollectionEngine`、`IKRig`、`ControlRig`、`PoseSearch`(いずれも任意依存。プラグインが無効なプロジェクトでも、ブリッジは読み込める)
**Storage**: ファイルのみ。追加: `%LOCALAPPDATA%/unrealengine-cli/servers/<pid>.json`、`<Project>/Saved/UnrealCli/Baselines/<name>.png`
**Testing**: cargo test(モック Bridge)、UE Automation Spec、E2E(`tests/e2e_editor.rs` と `scripts/e2e-net.py`、`scripts/e2e-gas.py`)
**Target Platform**: Windows x64(001 と同じ)
**Performance Goals**: 同期アサーションは一致していれば 1 秒以内、性能計測は計測時間 + 5 秒以内
**Constraints**: プラグインの任意依存を必須にしない(無効なら `<group>.plugin_disabled`)。専用サーバーはソース版エンジンだけ
**Scale/Scope**: ツール約 60 個、型付きコマンド 12 グループ、スキル 8 個の追加

## Constitution Check

| 原則 | 評価 |
|---|---|
| I. 独立性 | PASS: エンジン同梱のプラグインと自前のコードだけ |
| II. CLI ファースト | PASS: すべて型付きコマンドと `tool call` で使え、エラーコードを contracts で定義 |
| III. テストファースト | PASS: tasks で各ストーリーのテストを実装より先に置く |
| IV. 契約テスト | PASS: ツールカタログの一致テスト、README のツール数テスト、エンベロープの契約を流用 |
| V. 安全 | PASS: ini / .uproject / .Build.cs / Target.cs の変更は事前表示と dry-run。サーバーの停止は自分で起動した記録の PID だけ。秘密の値は出力しない |
| VI. シンプルさ | PASS: ネットワーク設定や GAS の Effect の詳細は既存の prop で扱い、専用ツールは頻度の高い操作に限る |

## Project Structure

### Documentation

```text
specs/002-game-dev-expansion/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── cli-commands.md
│   └── tool-catalog.md
└── tasks.md
```

### Source Code(追加・変更)

```text
src/commands/
├── typed.rs            # pie(--players, --net-mode, --world), net, online, gas, tags, light, render, anim, audio, physics, pcg, loc, perf
├── server.rs           # server target add|build|package|start|list|logs|stop(CLI 内で完結)
└── visual.rs           # visual baseline|compare|diff(image クレート)
src/servers.rs          # 専用サーバーの記録と停止
tests/
├── us11_net.rs         # net / pie --world のモックテスト
├── us12_server.rs      # Target.cs の生成、インストール版の判定、サーバー記録
├── us13_online.rs      # 設定の書き込み、秘密の伏せ字
├── us14_gas.rs         # gas enable の .uproject / .Build.cs 編集
├── us15_visual.rs      # 画像差分
└── e2e_editor.rs       # e2e_21 以降を追加
Plugin/UnrealCliBridge/Source/UnrealCliBridge/Private/Tools/
├── NetTools.cpp        # pie_worlds, world 解決, net_assert, net_emulate, pie_connect
├── ReplicationTools.cpp# bp_set_replication, bp_set_rpc, code_add_replicated_property, code_add_rpc
├── OnlineTools.cpp     # online_*
├── GasTools.cpp        # gas_*
├── TagTools.cpp        # tags_*
├── LightingTools.cpp   # light_*, render_settings
├── AudioPhysicsPcgTools.cpp # metasound_*, geometry_collection_create, pcg_*
├── LocalizationTools.cpp    # loc_*
└── PerfTools.cpp       # perf_*
Plugin/.../Content/Python/unreal_cli_bridge/tools/anim_advanced.py  # IK Rig, Retarget, Control Rig, Pose Search
scripts/e2e-net.py, scripts/e2e-gas.py
skills/unreal-multiplayer, unreal-dedicated-server, unreal-online-sessions, unreal-gas, unreal-lighting, unreal-advanced-animation, unreal-audio-physics-pcg, unreal-visual-perf-testing
```

**Structure Decision**: 001 と同じく、Cargo クレート 1 つと UE プラグイン 1 つ。任意依存のエンジンプラグインは `.uplugin` の `Plugins` に `Optional: true` で列挙し、モジュールは実行時に存在を確かめてから使う。

## 実装順(依存関係)

1. US1(ワールドの解決)を最初に作る。US2、US3、US5、US6 の PIE 検証がこれに依存する。
2. US2 → US3 → `scripts/e2e-net.py`(SC-001)。
3. US4 と US5 は US1 の後なら並行して進められる。
4. US6〜US10 は互いに独立。
5. US11 の画像差分は独立(エディタ不要)。性能計測は US1 の後。
6. US12 は最後に、全機能をまとめて 5.5〜5.8 で検証する。

## Complexity Tracking

| 違反の可能性 | 理由 | 採用しなかった、より単純な案 |
|---|---|---|
| 任意依存のエンジンプラグインが多い | GAS、PCG、MetaSound などは別プラグインで、プロジェクトによっては無効 | 必須依存にすると、無効なプロジェクトでブリッジ自体が読み込めなくなる |
