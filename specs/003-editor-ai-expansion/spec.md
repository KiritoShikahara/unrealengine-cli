# 機能仕様: エージェント連携と AI 系エディタ機能の拡張(SessionStart フック、ツール作成スキル、Behavior Tree / Control Rig の強化、State Tree)

**Feature Branch**: `003-editor-ai-expansion`

**Created**: 2026-10-21

**Status**: Draft

**Input**: ユーザー記述: 「外部の Claude Code 向け UE プラグインを参考に機能拡張する。SessionStart フック、ツール/スキル作成スキル、Behavior Tree と Control Rig の強化、State Tree をすべて実装する」

## Clarifications

### 自律着手による前提

- Q: フックの実装は bash スクリプトか? → A: CLI のサブコマンド `unrealengine-cli hook session-start` にする。理由: bash が無い Windows(PowerShell のみ)でも動き、独立性(他製品のスクリプトを持ち込まない)を守れる。
- Q: フックをどこに登録するか? → A: `unrealengine-cli hook install claude-code [--global]` が `.claude/settings.json`(または `~/.claude/settings.json`)の `hooks.SessionStart` に追記する。既存の設定は保持し、同じコマンドが既にあれば何もしない。`--dry-run` で書き込み内容だけを出す。理由: ユーザー設定の上書きを避ける(憲章 V)。
- Q: `.uproject` が見つからないときのフック出力は? → A: 何も出力せず終了コード 0。理由: UE 以外のリポジトリでセッションを邪魔しない。
- Q: ツール作成スキルの範囲は? → A: このリポジトリに C++ / Python ツールを追加する手順(登録、カタログ再生成、型付きコマンド、テスト、エディタ再起動)と、同梱スキルの書き方(Skill Contract v1、lint)を 1 つのスキル `unreal-cli-extend` にまとめる。
- Q: Behavior Tree の強化範囲は? → A: `bt_remove_node`、`bt_set_node_property`(ノードインスタンスのプロパティを UE テキスト形式で設定)、`blackboard_get` を追加する。
- Q: Control Rig の強化範囲は? → A: `controlrig_get`(階層の一覧)、`controlrig_add_bone` / `controlrig_add_control` / `controlrig_add_null`(階層コントローラ経由)を Python ツールで追加する。リググラフ(ユニットノード)の編集は範囲外。
- Q: State Tree の範囲は? → A: `statetree_create`(スキーマ: Component / AIComponent)、`statetree_add_state`、`statetree_add_task`、`statetree_add_transition`、`statetree_get`、`statetree_compile`。StateTree プラグインが無効なら `statetree.plugin_disabled`。

## User Scenarios & Testing *(mandatory)*

### User Story 1 - セッション開始時に UE プロジェクトだと伝わる (Priority: P1)

UE プロジェクトのリポジトリで Claude Code を起動すると、「UE プロジェクトなので unrealengine-cli / ue_* ツールを使う」という短い文脈が自動で入る。

**Independent Test**: `.uproject` を含む一時ディレクトリで `hook session-start` を実行すると、プロジェクト名を含む `hookSpecificOutput.additionalContext` の JSON が出る。含まないディレクトリでは何も出ない。

**Acceptance Scenarios**:

1. **Given** `.uproject` があるディレクトリ(または親)、**When** `hook session-start`、**Then** プロジェクト名・エンジンバージョン(取れれば)・推奨スキルを含む JSON を stdout に出す。
2. **Given** `.uproject` がない、**When** `hook session-start`、**Then** 出力なしで終了コード 0。
3. **Given** 既存の `.claude/settings.json`、**When** `hook install claude-code`、**Then** 既存キーを残して SessionStart を追加し、2 回目は変更なし。

### User Story 2 - エージェントが新しいツールやスキルを追加できる (Priority: P2)

エージェントが `unreal-cli-extend` スキルに従い、C++ / Python ツールと同梱スキルを規約どおりに追加できる。

**Independent Test**: `skills lint --severity error` が通り、スキル本文の手順が参照するパスとコマンドが実在する。

### User Story 3 - Behavior Tree を作るだけでなく直せる (Priority: P2)

**Independent Test**: E2E でノードを追加 → プロパティ設定 → 削除し、`bt_get` で反映を確認。`blackboard_get` がキー一覧を返す。

### User Story 4 - Control Rig の階層を編集できる (Priority: P3)

**Independent Test**: E2E で Control Rig を作成し、ボーン / ヌル / コントロールを追加して `controlrig_get` に出る。

### User Story 5 - State Tree を作って編集できる (Priority: P2)

**Independent Test**: E2E で State Tree を作成し、ステート 2 つ、タスク、遷移を追加し、コンパイル成功と `statetree_get` の構造を確認する。

### Edge Cases

- `settings.json` が不正な JSON → `hook.settings_invalid` で書き込まない。
- 存在しないノード id / ステート名 → `ai.node_not_found` / `statetree.state_not_found`。
- 未知のプロパティ → `prop.not_found`(既存コードと同じ)。

## Requirements *(mandatory)*

- **FR-001**: `hook session-start` は cwd から親へ `.uproject` を探し、Claude Code の SessionStart フック形式の JSON を出す。エディタ接続は不要で 1 秒以内に終わる。
- **FR-002**: `hook install claude-code [--global]` は settings.json を保持的にマージし、冪等で、`--dry-run` に対応する。
- **FR-003**: スキル `unreal-cli-extend` を同梱し、Skill Contract v1 を満たす。
- **FR-004**: `bt_remove_node`、`bt_set_node_property`、`blackboard_get` を追加する。
- **FR-005**: `controlrig_get`、`controlrig_add_bone`、`controlrig_add_control`、`controlrig_add_null` を追加する。
- **FR-006**: `statetree_create`、`statetree_add_state`、`statetree_add_task`、`statetree_add_transition`、`statetree_get`、`statetree_compile` を追加し、スキル `unreal-state-tree` を同梱する。
- **FR-007**: 追加ツールはツールカタログ・README のツール数・既存スキル(`unreal-ai`、`unreal-advanced-animation`)に反映する。

## Success Criteria

- **SC-001**: 品質ゲート(fmt、clippy、test、skills lint)がすべて通る。
- **SC-002**: 追加ツールの E2E が UE 5.7 で通る。

## 実装中の設計判断

- State Tree の編集とコンパイルには StateTreeEditorModule の C++ API が必要で、StateTree / GameplayStateTree は既定で無効のプラグインだった。任意依存にするとブリッジ DLL の読み込みが不安定になるため、`UnrealCliBridge.uplugin` の必須依存(`Enabled: true`)にした(Niagara や EnhancedInput と同じ扱い)。リンクには `PropertyBindingUtils` モジュールも必要だった。
- Control Rig の階層は Python の `ControlRigBlueprint.hierarchy` プロパティと `RigHierarchyController` で操作する(`get_hierarchy()` は存在しない)。
- Behavior Tree の `WaitTime` のような「値または BB キー」型は、`bt_set_node_property` の戻り値が `(DefaultValue=4.000000)` のような構造体テキストになる。Blackboard には常に `SelfActor` キーがある。
- skills lint は本文の「unrealengine-cli <単語>」をコマンド参照とみなすため、`unreal-cli-extend` の説明文ではその並びを避けた。
