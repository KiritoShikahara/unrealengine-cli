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

- ブランチ: `main` は安定版。作業は feature ブランチ(`002-game-dev-expansion` など)で行い、まとまった単位でプルリクエストを作り、CI が通ってから `main` にマージする。`main` へ直接 push しない。リリースのタグは `main` にだけ打つ
- TDD: テストを先に書き、失敗を確認してから実装する
- エンジン API を使う前に、該当ヘッダーを grep してシグネチャを確認する
- 他製品のコードや名前を持ち込まない(`tests/independence.rs` が検査する)
- Windows で `setup` / `install` を名前に含むテストバイナリは UAC に止められるので使わない
- Live Coding のロックはエンジン単位(同じ UE バージョンの全プロジェクト共通)。動作中は同じエンジンのビルドが失敗する
- 数十分かかる E2E(`scripts/e2e-cpp.py` など)はバックグラウンドで実行し、ログファイルを確認する(実行中は `cargo build` で CLI を上書きしない)
- 同じエンジンの別プロジェクト(ユーザーのエディタ)で Live Coding が有効だと、そのエンジンのビルドが全部止まる。ユーザーのエディタは閉じず、別のエンジンバージョンで進めるか、閉じてもらう
- 新しい Python ツールと C++ ツールの登録は起動時だけ。追加したらエディタを再起動する(Live Coding では反映されない)
- C++ の raw 文字列 `TEXT(R"(...)")` の中に `)"` を書かない(JSON の説明に括弧つきの文字列を入れると文字列が途中で閉じる)
- E2E の副産物(`TestProject/Content/Localization`、`Config/Localization`、`__ExternalActors__`、Fab の取り込み先)はコミットしない
- `editor open --null-rhi` ではメッシュを持つアクターの生成と PIE がエンジン側でクラッシュする。PIE の E2E は `UNREALENGINE_CLI_E2E_RENDER=1 bash scripts/e2e.sh 5.7`(描画あり)で回す
