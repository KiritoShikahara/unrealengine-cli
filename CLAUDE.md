# CLAUDE.md

`unrealengine-cli` 開発ガイド。仕様は `specs/001-unreal-cli/`、原則は `.specify/memory/constitution.md`。

## 構成

- `src/`: Rust CLI(`cli.rs` がコマンド定義、`commands/` が実装、`transport/` がブリッジ通信)
- `Plugin/UnrealCliBridge/`: エディタプラグイン。C++ ツールは `Source/.../Private/Tools/`、Python ツールは `Content/Python/unreal_cli_bridge/tools/`
- `skills/`: 同梱スキルの正本(Skill Contract v1、`unrealengine-cli skills lint` で検証)
- `tools/catalog.json`: オフライン用ツールカタログ(`scripts/gen-catalog.sh` で再生成)
- `TestProject/`: E2E 用 UE プロジェクト(`Plugins/UnrealCliBridge` はリポジトリの `Plugin/` へのジャンクション)

## 品質ゲート

```bash
cargo fmt --all -- --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cargo run -- skills lint --severity error
bash scripts/e2e.sh 5.7   # Windows + UE 5.7
```

## ルール

- TDD: テストを先に書き、失敗を確認してから実装する
- エンジン API を使う前に、該当ヘッダーを grep してシグネチャを確認する
- 他製品のコードや名前を持ち込まない(`tests/independence.rs` が検査する)
- Windows で `setup` / `install` を名前に含むテストバイナリは UAC に止められるので使わない
- Live Coding のロックはエンジン単位(同じ UE バージョンの全プロジェクト共通)。動作中は同じエンジンのビルドが失敗する
- 数十分かかる E2E(`scripts/e2e-cpp.py` など)はバックグラウンドで実行し、ログファイルを確認する(実行中は `cargo build` で CLI を上書きしない)
- `editor open --null-rhi` ではメッシュを持つアクターの生成と PIE がエンジン側でクラッシュする。PIE の E2E は `UNREALENGINE_CLI_E2E_RENDER=1 bash scripts/e2e.sh 5.7`(描画あり)で回す
