# Bridge Protocol v1

- 接続先: `127.0.0.1:<port>`(ループバック以外での待ち受けは禁止)
- フレーム: `u32 長さ(BE)` + UTF-8 JSON。上限は 64 MiB
- ハンドシェイク(接続後の最初のフレーム):
  - 要求: `{"type":"hello","token":"...","protocol":1,"client":"unrealengine-cli/0.1.0"}`
  - 応答: `{"type":"welcome","protocol":1,"engineVersion":"5.7.1","pluginVersion":"0.1.0","project":"..."}`
  - トークン不一致やプロトコル不一致の場合は `{"type":"reject","error":{...}}` を返して切断する
- 要求: `{"type":"call","id":"<uuid>","tool":"<name>","args":{},"dryRun":false,"timeoutMs":30000}`
- 応答: `{"type":"result","id":"...","ok":true,"data":{}}` または `{"type":"result","id":"...","ok":false,"error":{"code","message","hint"}}`
- ジョブ: `longRunning` なツールは `{"ok":true,"data":{"jobId":"..."}}` を返す。結果は `jobs_get` / `jobs_logs` / `jobs_cancel` で取得し、取り消す
- 予約ツール: `system_ping`、`tools_list`(スキーマ付き)
- 互換性: フィールドの追加は互換。削除と意味の変更は protocol の番号を上げる
