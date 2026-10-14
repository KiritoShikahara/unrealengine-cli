# Data Model: unrealengine-cli

## EditorInstance
| フィールド | 型 | 説明 |
|---|---|---|
| pid | u32 | エディタのプロセス ID |
| port | u16 | 待ち受けポート |
| token | string | セッショントークン(起動ごとに乱数で生成) |
| project | path | .uproject の絶対パス |
| engineVersion | string | 例: 5.7.1 |
| pluginVersion | semver | プラグインのバージョン |
| protocolVersion | u32 | プロトコルのバージョン |
| startedAt | RFC3339 | 起動時刻 |

状態: `starting → ready → busy(モーダル/コンパイル中) → ready → exited`

## Tool
| フィールド | 型 | 説明 |
|---|---|---|
| name | string | `snake_case`、全体で一意(例: `blueprint_add_node`) |
| group | enum | editor / project / blueprint / level / asset / property / code / runtime / anim / umg / material / niagara / landscape / input / data / audio / sequencer / packaging / python / jobs |
| summary | string | 1 行の説明 |
| description | string | 詳しい説明 |
| inputSchema | JSON Schema | 入力 |
| outputSchema | JSON Schema | 出力 |
| mutating | bool | 変更を伴うか(true なら dry-run と Undo が必須) |
| longRunning | bool | ジョブとして実行するか |
| minEngine | string | 対応する最小バージョン |

## Job
| フィールド | 型 | 説明 |
|---|---|---|
| id | string | ジョブ ID |
| kind | string | ツール名 |
| state | enum | queued / running / succeeded / failed / cancelled / timeout |
| progress | 0..1 または null | 進捗 |
| log | array | ログ行(順番付き) |
| result | object または null | 結果 |
| error | ErrorInfo または null | エラー |

## Envelope(全コマンド共通)
`{ ok, command, data?, error?: ErrorInfo, warnings[], meta: { durationMs, instance?, cliVersion } }`

## ErrorInfo
`{ code, message, hint?, details? }`。code は `domain.reason` 形式(例: `connect.no_instance`、`blueprint.compile_errors`、`build.failed`)。

## Skill
frontmatter: `name`、`description`(発動条件)、`tools[]`(使うツール)、`version`。本文には「いつ使うか / 手順 / 検証 / 失敗時」の必須見出しを置く。

## BuildDiagnostic
`{ file, line, column?, severity, code, message }`
