# Quickstart 検証ガイド(Windows)

前提: 001 の quickstart が通る環境。UE 5.5〜5.8。専用サーバーの手順だけ、ソース版エンジンが必要。

## 自動(E2E)

1. `UNREALENGINE_CLI_E2E_RENDER=1 bash scripts/e2e.sh 5.7` → 既存 + 追加の E2E が全件成功する。
2. `python scripts/e2e-net.py` → 2 人のマルチプレイ(移動、スコアのレプリケーション、Server RPC、遅延 200ms)の検証が `ALL CHECKS PASSED`(US1〜US3、SC-001)。
3. `python scripts/e2e-gas.py` → 属性、エフェクト、アビリティの検証が合格(US6、SC-004)。
4. `bash scripts/e2e.sh 5.5`、`5.6`、`5.8` → 全件成功(US12、SC-006)。
5. `UNREALENGINE_CLI_E2E_FAB=1 cargo test --test e2e_fab` → サインイン済みのエディタで Fab の取り込みが成功(US12)。

## 手動

1. 専用サーバー(ソース版エンジン): `unrealengine-cli server target add` → `server build` → `server start --map /Game/Maps/Arena --port 7777` → `pie connect 127.0.0.1:7777` → `server logs <pid>` に参加が記録される → `server stop <pid>`(US4、SC-003)。
2. インストール版エンジンで `unrealengine-cli server build` → 5 秒以内に `server.source_engine_required`(SC-003)。
3. EOS / Steam: `online configure --subsystem eos --set ProductId=...` → `online check` が不足キーを返す。秘密の値が出力に出ないことを確認する(US5)。
4. 画像差分: `visual baseline arena` → ライトを消す → `visual compare arena` が `visual.mismatch`(US11、SC-007)。
5. 性能: `pie start` → `perf measure --seconds 10` が 15 秒以内に統計を返す(SC-008)。
