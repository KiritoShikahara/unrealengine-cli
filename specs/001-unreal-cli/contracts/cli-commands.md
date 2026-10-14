# CLI Commands

共通オプション: `--output text|json`、`--project <path>`、`--instance <pid>`、`--timeout-ms`、`--dry-run`、`-v`

## 終了コード
| コード | 意味 |
|---|---|
| 0 | 成功 |
| 2 | 入力の誤り(引数、スキーマ違反) |
| 3 | 接続失敗(インスタンスなし、認証失敗、プロトコル不一致) |
| 4 | エディタ側の失敗 |
| 5 | タイムアウト |
| 6 | ビルド失敗 |
| 1 | 想定外の内部エラー |

## コマンド
| コマンド | 内容 | 接続 |
|---|---|---|
| `setup [--build]` | プラグイン導入と有効化 | 不要 |
| `editor open\|close\|build\|status` | エディタのライフサイクル | 不要 |
| `doctor` | 環境診断 | 任意 |
| `instances list\|use` | 接続先の管理 | 不要 |
| `ping` / `logs [--since --severity --category]` | 疎通とログ | 必要 |
| `tool list [--group] / find <q> / describe <name> / call <name> --args <json\|@file>` | 汎用のツール実行 | describe までは不要 |
| `blueprint\|level\|asset\|prop\|pie\|test ...` | よく使う操作の型付きサブコマンド | 必要 |
| `code search\|read\|edit\|apply [--plan --force]` | C++ の編集と反映 | apply のみ必要 |
| `python exec <code\|@file>` | エディタ内で Python を実行 | 必要 |
| `jobs list\|get\|logs\|cancel` | ジョブ | 必要 |
| `batch <file>` | 複数のツールを順に実行(失敗したら停止) | 必要 |
| `skills install <client> [--local\|--global] / refresh / uninstall / list / lint` | スキル管理 | 不要 |
| `mcp serve / configure <client>` | MCP | serve は必要に応じて |
| `reference search <q>` | エンジンソースと Python スタブの検索 | 不要 |
| `self update` | 自己更新 | 不要 |
