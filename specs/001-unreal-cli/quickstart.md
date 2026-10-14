# Quickstart 検証ガイド(Windows)

前提: UE 5.5〜5.8、Visual Studio 2022(C++ ワークロード)、Rust 1.85 以降が入っていること。

1. `cargo build --release`
2. `target\release\unrealengine-cli.exe doctor --project TestProject` → エンジンが検出される(US2)
3. `unrealengine-cli setup --project TestProject --dry-run` → 変更予定だけが表示され、ファイルは変わらない
4. `unrealengine-cli setup --project TestProject --build` → ビルドが成功する
5. `unrealengine-cli editor open --project TestProject` → `unrealengine-cli ping --output json` で `ok: true` が返る(US1)
6. `unrealengine-cli blueprint create /Game/E2E/BP_Hello --parent Actor` のあと、BeginPlay → PrintString を接続して `blueprint compile` と `blueprint save` を実行 → エラー 0(US3)
7. `unrealengine-cli level actor spawn /Game/E2E/BP_Hello` → `unrealengine-cli pie run --seconds 5 --stop-on-log Hello` で `endedReason: log` が返る(US4、US6)
8. `unrealengine-cli skills install claude-code --local` → `.claude/skills/unreal-*` が配置される。`unrealengine-cli skills lint` が通る(US8)
9. エディタを閉じて `unrealengine-cli ping` を実行 → 終了コード 3、`error.code = connect.no_instance`、hint 付き

自動化版: `scripts/e2e.ps1 -Engine 5.7`
