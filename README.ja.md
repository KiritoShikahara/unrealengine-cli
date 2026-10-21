# unrealengine-cli

[English](README.md)

`unrealengine-cli` は、AI エージェント(Claude Code、Codex、Cursor など)から Unreal Editor 5.5〜5.8 を操作するための Rust 製 CLI です。
エディタ側プラグイン **UnrealCliBridge** とループバック TCP で通信し、194 個のエディタツールを公開します。
対象は Blueprint、レベルとアクター、アセット、リフレクションによるプロパティ操作、C++(Live Coding / 再ビルド)、PIE、Automation テスト、
アニメーション、UMG、マテリアル、Niagara、ランドスケープ、Enhanced Input、データアセット、Sequencer、オーディオ、パッケージングです。

```text
エージェント -> スキル(必要時に読み込み) -> unrealengine-cli -> TCP 127.0.0.1 -> UnrealCliBridge(Unreal Editor)
```

## はじめ方

```bash
# 1. インストール(SHA256SUMS を検証してから導入。リリース公開前は、clone して cargo install --path . を使う)
curl -fsSL https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.sh | sh   # macOS / Linux
irm https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.ps1 | iex       # Windows

# 2. プロジェクトのフォルダで実行
unrealengine-cli doctor                 # エンジン、ビルドツール、プラグイン、エディタの状態
unrealengine-cli setup --dry-run        # 追加するファイルを表示
unrealengine-cli setup --build          # プラグインを導入してエディタターゲットをビルド
unrealengine-cli editor open --headless # エディタを起動し、ブリッジの応答を待つ
unrealengine-cli ping

# 3. エージェント用スキル(エディタ不要)
unrealengine-cli skills install claude-code --local
```

Blueprint のみのプロジェクトでは、プラグインのコンパイルに C++ ターゲットが必要です: `unrealengine-cli setup --add-code-module --build`。

## 使い方

```bash
unrealengine-cli tool find blueprint connect          # ツール検索
unrealengine-cli tool describe bp_connect             # 入力スキーマ
unrealengine-cli blueprint create /Game/BP_Hello
unrealengine-cli blueprint add-node /Game/BP_Hello --type Event --member BeginPlay
unrealengine-cli blueprint add-node /Game/BP_Hello --type CallFunction --member PrintString
unrealengine-cli blueprint connect /Game/BP_Hello N1.then N2.execute
unrealengine-cli blueprint compile /Game/BP_Hello
unrealengine-cli pie run --seconds 10 --stop-on-log Hello
```

* すべてのコマンドが `--output json` に対応します: `{ ok, command, data | error { code, message, hint }, warnings, meta }`。
* 終了コード: 0 成功、2 入力、3 接続、4 エディタ、5 タイムアウト、6 ビルド、1 内部エラー。
* `--dry-run` で変更をプレビューできます。エディタ上の変更は取り消せます(`tool call editor_undo`)。
* MCP: `unrealengine-cli mcp configure claude-code` で `unrealengine-cli mcp serve` を登録します。
* Claude Code のフック: `unrealengine-cli hook install claude-code [--global]` で SessionStart フックを追加します。UE プロジェクト内では `hook session-start` が「unrealengine-cli を使う」という文脈を新しいセッションに入れます(それ以外では何も出しません)。
* Windows の Git Bash は `/Game/...` を書き換えますが、unrealengine-cli が元に戻します。

## 実機で検証済みのシナリオ

実際のエディタ(UE 5.7 / 5.8)に対して、CLI の呼び出しだけで次を確認しています。

| シナリオ | スクリプト |
|---|---|
| コイン集めゲーム: スコアを持つ GameMode、HUD、コイン、プレイヤーを追う敵(Behavior Tree + NavMesh)を作り、PIE で遊んで確認 | `scripts/e2e-game.py` |
| シューティング: WASD の Modifier 付き Enhanced Input、キャラクター移動、弾、ダメージ | `scripts/e2e-game2.py` |
| Fab: ライブラリ検索、asset pack と FBX の素材を Add to Project と同じ方法で取り込み・保存 | `unrealengine-cli fab add <uid>`(手動。Fab タブでのサインインが必要) |
| C++: UCLASS の追加、再ビルド、Blueprint から PIE で利用、.cpp の変更を反映(Live Coding または再ビルド) | `scripts/e2e-cpp.py` |
| エディタツール全般: Blueprint、レベル、アセット、マテリアル、入力、データ、UMG、アニメーション、Niagara、ランドスケープ、AI、パッケージングのチェック | `bash scripts/e2e.sh 5.7` |

## エディタの起動モード

| コマンド | ウィンドウ | 備考 |
|---|---|---|
| `editor open` | あり | 通常 |
| `editor open --headless` | なし | オフスクリーン描画。初回はシェーダーをコンパイル |
| `editor open --null-rhi` | なし | 最速。メッシュを持つアクターの生成と PIE はエンジンがクラッシュする |

## 開発

```bash
cargo fmt --all -- --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cargo run -- skills lint --severity error
bash scripts/e2e.sh 5.7     # プラグインのビルド + Automation テスト + エディタ E2E(Windows)
UNREALENGINE_CLI_E2E_RENDER=1 bash scripts/e2e.sh 5.8   # PIE、マルチプレイ、GAS、ライティング、画像比較、ゲームの E2E も実行
UNREALENGINE_CLI_E2E_FAB=1 cargo test --test e2e_fab   # Fab のダウンロード(サインイン済みのエディタ。ライブラリのアイテムのみ)
```

詳しくは [docs/architecture.md](docs/architecture.md)、[docs/tools.md](docs/tools.md)、[docs/skills.md](docs/skills.md)、[docs/editor-compatibility.md](docs/editor-compatibility.md) を参照してください。

## バグ報告

[GitHub の Issue](https://github.com/KiritoShikahara/unrealengine-cli/issues/new?template=bug_report.yml) に、プロジェクトのフォルダで実行した `unrealengine-cli doctor --report` の出力と、失敗したコマンドの `--output json` の結果を貼ってください。
