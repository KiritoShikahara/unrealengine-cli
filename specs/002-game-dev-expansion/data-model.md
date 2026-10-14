# Data Model: ゲーム制作機能の拡張

## PieWorld
| フィールド | 型 | 説明 |
|---|---|---|
| id | string | `standalone`、`server`、`client:<n>`(n は 1 から) |
| netMode | enum | Standalone / ListenServer / DedicatedServer / Client |
| pieInstance | int | エンジンの PIE インスタンス番号 |
| players | int | ローカルプレイヤー数 |
| mapName | string | ワールドのマップ |

## NetActorInfo(既存の PIE アクター情報に追加)
| フィールド | 型 | 説明 |
|---|---|---|
| role | enum | Authority / AutonomousProxy / SimulatedProxy / None |
| remoteRole | enum | 同上 |
| owner | string | 所有アクターのラベル(なければ null) |
| netGuid | string | ネット GUID(未割り当てなら null) |

## NetworkEmulation
| フィールド | 型 | 説明 |
|---|---|---|
| latencyMs | int | 片道の遅延 |
| jitterMs | int | 遅延のばらつき |
| lossPercent | number | パケットロス(0〜100) |
| target | enum | server / clients / all |

## SyncAssertion
| フィールド | 型 | 説明 |
|---|---|---|
| actor | string | ラベル、クラス名、またはネット GUID |
| property | string | プロパティのパス(例: `Health`、`RootComponent.RelativeLocation.X`) |
| expect | any | 省略時は「全ワールドで一致」 |
| timeoutSeconds | number | 既定 5 |
| result | object | `{ ok, worlds: [{ id, value }], waitedSeconds }` |

## DedicatedServer
| フィールド | 型 | 説明 |
|---|---|---|
| pid | u32 | プロセス ID |
| project | path | 起動元の .uproject |
| executable | path | サーバーの実行ファイル |
| map | string | 起動マップ |
| port | u16 | 待ち受けポート |
| logFile | path | ログのパス |
| startedAt | RFC3339 | 起動時刻 |

状態: `starting → listening → (stopping) → exited`

## OnlineConfig
| フィールド | 型 | 説明 |
|---|---|---|
| subsystem | enum | null / eos / steam |
| plugins | string[] | 有効化するプラグイン |
| settings | map | ini の節とキー(秘密の値は出力で `***`) |
| missing | string[] | 不足している必須キー |

## GasAttributeSet
| フィールド | 型 | 説明 |
|---|---|---|
| className | string | 例: `UMyAttributeSet` |
| attributes | `{ name, default, replicated }[]` | 属性 |

## GasEffect
| フィールド | 型 | 説明 |
|---|---|---|
| duration | enum | Instant / HasDuration / Infinite |
| durationSeconds | number | HasDuration のとき |
| modifiers | `{ attribute, op(Add/Multiply/Divide/Override), magnitude }[]` | |
| stacking | `{ type, limit }` | |
| grantedTags | string[] | |

## ImageDiff
| フィールド | 型 | 説明 |
|---|---|---|
| baseline | path | 基準画像 |
| actual | path | 比較画像 |
| tolerance | int | 1 ピクセルの許容差(0〜255、既定 8) |
| threshold | number | 合格とする差分割合の上限(%、既定 0.1) |
| diffPercent | number | 差分ピクセルの割合 |
| maxDelta | int | 最大差 |
| diffImage | path | 差分画像 |

## PerfSample
| フィールド | 型 | 説明 |
|---|---|---|
| seconds | number | 計測時間 |
| frameMs / gameMs / renderMs / gpuMs | `{ avg, p95, max }` | フレーム時間の統計 |
| hitches | int | 閾値(既定 50ms)を超えたフレーム数 |
| drawCalls | `{ avg, max }` | |
| memoryMB | `{ used, peak }` | |
| partial | bool | PIE が途中で終わった |
| trace | path | トレースを取った場合の .utrace |
