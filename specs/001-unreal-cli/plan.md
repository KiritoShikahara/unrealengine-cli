# 実装計画: unreal-cli

**Branch**: `001-unreal-cli` | **Date**: 2026-10-14 | **Spec**: [spec.md](spec.md)

## Summary

Rust 製の単一バイナリ `unreal-cli` と、UE エディタに入れる C++ Editor プラグイン `UnrealCliBridge` を作る。両者は 127.0.0.1 上の TCP で、長さプレフィックス付き JSON をやり取りする。ツールはプラグイン側で登録し、スキーマを CLI へ公開する。CLI はそのスキーマから、型付きサブコマンド、`tool call`、MCP ツールを生成する。スキルはバイナリに同梱し、オフラインで導入できるようにする。

## Technical Context

**Language/Version**: Rust 1.85+(edition 2024)、C++20(UE 5.5〜5.8 の Editor モジュール)
**Primary Dependencies**: clap、tokio、serde / serde_json、schemars、rmcp(MCP)、reqwest(更新)、sha2。UE 側は Sockets、Json、UnrealEd、BlueprintGraph、Kismet、PythonScriptPlugin、LiveCoding
**Storage**: ファイルのみ(`%LOCALAPPDATA%/unreal-cli/` に設定、インスタンス登録、キャッシュ)
**Testing**: cargo test(ユニット、統合。統合テストではモック Bridge を使う)、insta(出力スナップショット)、UE Automation Spec(プラグイン)、E2E スクリプト(実エディタ)
**Target Platform**: Windows x64 が本命。macOS(arm64/x64)と Linux x64 は CLI 単体の動作だけ
**Project Type**: CLI とエディタプラグイン
**Performance Goals**: 単発ツール呼び出しの p95 が 300ms 以下、CLI の起動が 30ms 以下
**Constraints**: ループバック通信だけ。接続にはセッショントークンが必要。ゲームスレッドを止める処理はジョブ化する
**Scale/Scope**: ツール 100 個以上、スキル約 20 個

## Constitution Check

| 原則 | 評価 |
|---|---|
| I. 独立性 | PASS: 新規のコードだけ。他リポジトリへの依存は 0 |
| II. CLI ファースト | PASS: 共通エンベロープと終了コードを contracts で定義 |
| III. テストファースト | PASS: tasks で各ストーリーのテストを実装より先に置く |
| IV. 契約テスト | PASS: protocol / envelope / tool-schema / skill の契約を用意 |
| V. 安全 | PASS: ループバック、トークン、dry-run、Undo を必須化 |
| VI. シンプルさ | PASS: 汎用プロパティ操作と Python 実行を入れ、専用ツールの数を抑える |

Phase 1 後の再評価も全て PASS。

## Project Structure

### Documentation

```text
specs/001-unreal-cli/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── protocol.md
│   ├── cli-envelope.schema.json
│   ├── cli-commands.md
│   ├── tool-catalog.md
│   └── skill-contract.md
└── tasks.md
```

### Source Code

```text
Cargo.toml
src/
├── main.rs / cli.rs          # clap 定義
├── core/                     # envelope, error, exit_code, config, paths
├── transport/                # TCP クライアント, フレーミング, ハンドシェイク
├── instances/                # エディタ検出・選択
├── engine/                   # エンジン検出, UBT ビルド, 起動/終了, エラー解析
├── setup/                    # プラグイン配置, .uproject 編集, doctor
├── tools/                    # スキーマキャッシュ, typed コマンド, tool call/find
├── code/                     # C++ 検索・編集, apply_code 判定
├── jobs/
├── mcp/                      # MCP サーバー
├── skills/                   # install / refresh / lint
└── update/                   # self-update, checksum
tests/                        # 統合テスト(mock_bridge 使用)
skills/unreal-*/SKILL.md      # 同梱スキルの正本
Plugin/UnrealCliBridge/
├── UnrealCliBridge.uplugin
└── Source/UnrealCliBridge/
    ├── Private/Server/       # TCP サーバー, ディスパッチ, ジョブ
    ├── Private/Tools/        # グループ別ツール実装
    └── Private/Tests/        # Automation Spec
TestProject/                  # E2E 用 UE プロジェクト
scripts/                      # install.ps1 / install.sh / e2e.ps1
docs/
```

**Structure Decision**: Cargo クレート 1 つと UE プラグイン 1 つ。プラグインのソースは `include_dir` でバイナリに同梱し、setup が対象プロジェクトに展開する。

## Complexity Tracking

違反なし。

## 実装時の判断(2026-10-14)

- 専門領域のツールの多くは、プラグイン同梱の Python パッケージ(`Content/Python/unreal_cli_bridge`)で実装した。エディタの Python API で足りるものは Python、グラフ操作など Python から届かないもの(Blueprint、AnimGraph、UMG ツリー、struct/enum、Niagara、ランドスケープ、PIE、Live Coding)は C++ にした。
- MCP は rmcp を使わず、stdio の JSON-RPC を直接実装した(公開するのは 4 ツール: ping、find_tools、describe_tool、call)。
- 通信は std の同期 TCP で実装した(tokio は不要だった)。
- `editor open` は Windows でハンドルを継承しない起動(CreateProcessW、bInheritHandles=FALSE)にした。継承すると、呼び出し元のシェルがエディタの終了まで戻らない。
- 起動モードは 3 つ: 通常、`--headless`(-RenderOffscreen)、`--null-rhi`。`--null-rhi` では、メッシュを持つアクターの生成と PIE がエンジン側でクラッシュする。
- Git Bash のパス変換(`/Game/...` → `C:/Program Files/Git/Game/...`)を CLI 側で元に戻す。
