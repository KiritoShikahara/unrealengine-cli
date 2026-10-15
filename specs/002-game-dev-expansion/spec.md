# 機能仕様: ゲーム制作機能の拡張(マルチプレイ・サーバー、GAS、ライティング、アニメーション、品質)

**Feature Branch**: `002-game-dev-expansion`

**Created**: 2026-10-15

**Status**: Draft

**Input**: ユーザー記述: 「UE5 でゲームを制作するにあたって、さらに拡張すべき機能をすべて実装する。サーバー関係(マルチプレイ、レプリケーション、専用サーバー、オンラインサービス)、GAS、ライティング、アニメーション、MetaSound / Chaos / PCG、ローカライズなど、品質(画像差分、パフォーマンス計測、UE 5.5 / 5.6 検証、Fab の E2E)を含む」

## Clarifications

### Session 2026-10-15

- Q: 範囲は? → A: 提案した 3 区分(サーバー・マルチプレイ、ゲーム制作で足りないもの、品質面)をすべて実装する。
- Q: 専用サーバーのビルドは、ソース版エンジンが必要。インストール版ではどうするか? → A(既定値として採用): インストール版では、ビルドの前に「ソース版エンジンが必要」という構造化エラーを返す。ソース版エンジンが登録されていれば、それを使ってビルドする。
- Q: オンラインサービス(EOS / Steam)の実アカウントを使った検証は? → A(既定値として採用): 自動テストは OnlineSubsystem Null(ローカル)で行う。EOS / Steam は設定の書き込みと検証までを自動化し、実アカウントでの接続は手動の確認手順として残す。

## 位置づけ

- 001(`specs/001-unreal-cli`)の CLI とプラグインを拡張する。通信、エンベロープ、ジョブ、スキル契約、MCP の仕組みは 001 のものをそのまま使う。
- 憲章(独立性、CLI ファースト、テストファースト、契約テスト、安全、シンプルさ)に従う。専用ツールは頻度の高い操作に限り、それ以外は汎用プロパティ操作と Python 実行で済ませる。

## User Scenarios & Testing *(mandatory)*

### User Story 1 - マルチプレイの PIE を起動して両側を観察する (Priority: P1)

エージェントが、プレイヤー数とネットモード(Standalone / Listen Server / Client)を指定して PIE を起動する。サーバーとクライアントのそれぞれのワールドについて、アクター、プレイヤー、ログ、スクリーンショットを取得し、各クライアントへ入力を送る。

**Why this priority**: マルチプレイのゲームを作るときに、最初に必要になる検証手段だから。これがないと、レプリケーションの実装を確認できない。

**Independent Test**: TestProject で 2 人(Listen Server)の PIE を起動し、サーバーとクライアントそれぞれのワールドから PlayerController が 2 つ見えること、クライアント 1 の入力でそのポーンだけが動くことを確認できる。

**Acceptance Scenarios**:

1. **Given** エディタが起動している、**When** プレイヤー 2・Listen Server で PIE を起動する、**Then** ワールドの一覧(サーバー 1、クライアント 1)と各ワールドのネットモード・プレイヤー数が返る。
2. **Given** マルチプレイの PIE 中、**When** `--world client:1` を付けてアクター一覧を取る、**Then** そのクライアントから見えるアクターだけが返り、各アクターの Role / RemoteRole / 所有者が含まれる。
3. **Given** マルチプレイの PIE 中、**When** クライアント 1 にキー入力を送る、**Then** 入力はクライアント 1 の PlayerController にだけ届く。
4. **Given** プレイヤー 2・Client モード(専用サーバーを内部で立てる)、**When** PIE を起動する、**Then** サーバーワールドとクライアント 2 つが返る。

---

### User Story 2 - レプリケーションと RPC を作る (Priority: P1)

エージェントが Blueprint と C++ で、アクター・コンポーネントのレプリケーション設定、変数のレプリケーション(Replicated / RepNotify と条件)、RPC(Server / Client / NetMulticast、Reliable)を作る。

**Why this priority**: マルチプレイの実装そのもの。US1 とあわせて「作る → 確認する」が回る。

**Independent Test**: Blueprint に RepNotify の Health 変数と Server RPC を追加してコンパイルが通り、US1 の PIE でサーバーの変更がクライアントに反映され、OnRep が呼ばれたことをログで確認できる。

**Acceptance Scenarios**:

1. **Given** Blueprint、**When** 変数を RepNotify に設定する、**Then** OnRep 関数が作られ、コンパイルが通る。
2. **Given** Blueprint、**When** Server / Client / Multicast のカスタムイベントを作る、**Then** イベントのレプリケーション設定と Reliable が反映される。
3. **Given** C++ クラス、**When** レプリケートするプロパティを追加する、**Then** `UPROPERTY(Replicated)` / `ReplicatedUsing`、`GetLifetimeReplicatedProps`、`DOREPLIFETIME` のひな形が生成され、ビルドが通る。
4. **Given** アクター、**When** `bReplicates` や NetUpdateFrequency、NetCullDistance、`bAlwaysRelevant` を設定する、**Then** 値が保存される。

---

### User Story 3 - マルチプレイの同期を自動で検証する (Priority: P1)

エージェントが「サーバーとクライアントで、このアクターのこのプロパティが一致する」といったアサーションを PIE 中に実行し、合否と差分を受け取る。ネットワークの遅延やパケットロスを模擬した状態でも検証できる。

**Why this priority**: 同期の不具合は目視では見落としやすく、自動で判定できると開発のループが速くなる。

**Independent Test**: 2 人の PIE でサーバーのスコアを変更し、`net assert` が 1 秒以内に全クライアントの一致を確認する。遅延 200ms を設定しても一致まで待って成功する。

**Acceptance Scenarios**:

1. **Given** マルチプレイの PIE 中、**When** アクターとプロパティを指定して比較する、**Then** ワールドごとの値と一致・不一致が返る。
2. **Given** 一致しない状態が続く、**When** タイムアウトする、**Then** `net.mismatch` と各ワールドの最終値が返る。
3. **Given** マルチプレイの PIE 中、**When** 遅延・パケットロスを設定する、**Then** 以降の通信にその条件が適用され、設定値を取得できる。

---

### User Story 4 - 専用サーバーをビルドして起動する (Priority: P2)

エージェントがプロジェクトに Server ターゲットを追加し、専用サーバーをビルド・パッケージ化・起動する。起動したサーバーにクライアント(エディタの PIE、またはパッケージ版のゲーム)を接続し、ログを取得して停止する。

**Why this priority**: 製品のマルチプレイでは専用サーバーが標準だが、ソース版エンジンが必要で利用者が限られるため P2。

**Independent Test**: ソース版エンジンの環境で、Server ターゲットを追加 → ビルド → 起動 → PIE クライアントから接続し、サーバーログに参加プレイヤーが出る。インストール版エンジンでは、ビルド前に `server.source_engine_required` が返る。

**Acceptance Scenarios**:

1. **Given** Server ターゲットのないプロジェクト、**When** `server target add` を実行する、**Then** `<Project>Server.Target.cs` が作られ、変更内容が事前に表示される(`--dry-run` で書き込まない)。
2. **Given** インストール版エンジン、**When** サーバーをビルドする、**Then** ビルドを始める前に `server.source_engine_required` と、ソース版エンジンの用意方法が返る。
3. **Given** ビルド済みのサーバー、**When** `server start --map <Map> --port <Port>` を実行する、**Then** サーバーがバックグラウンドで起動し、待ち受けを確認してから PID とポートが返る。
4. **Given** 起動中のサーバー、**When** PIE クライアントを接続する、**Then** サーバーログに接続が記録され、`server logs` で取得できる。`server stop` で終了する。

---

### User Story 5 - オンラインセッション(作成・検索・参加) (Priority: P2)

エージェントが OnlineSubsystem / Online Services の設定(Null、EOS、Steam)をプロジェクトに書き込み、Blueprint でセッションの作成・検索・参加・破棄を組む。ローカルでは Null サブシステムで動作を確認する。

**Why this priority**: ロビーやマッチメイキングに必要。ただし外部アカウントに依存する部分があるため P2。

**Independent Test**: Null サブシステムで 2 人の PIE を起動し、サーバーが作ったセッションをクライアントが検索して参加できる。

**Acceptance Scenarios**:

1. **Given** プロジェクト、**When** `online configure --subsystem null|eos|steam` を実行する、**Then** 必要なプラグインの有効化と DefaultEngine.ini の設定が、事前表示のうえで書き込まれる。
2. **Given** EOS や Steam で、必須の設定値(製品 ID など)が足りない、**When** 構成を検証する、**Then** 不足しているキーの一覧が返る。秘密情報(クライアントシークレットなど)は出力に含めない。
3. **Given** Null サブシステム、**When** セッションの作成・検索・参加を PIE で実行する、**Then** 各段階の成否とセッション情報が返る。

---

### User Story 6 - Gameplay Ability System(GAS)を組む (Priority: P2)

エージェントが GAS を有効化し、AttributeSet(C++)、Gameplay Ability、Gameplay Effect、Gameplay Cue、Gameplay Tag を作る。アクターに AbilitySystemComponent を付け、アビリティの付与・発動を PIE で検証する。

**Why this priority**: アクション・RPG 系のゲームで事実上の標準。マルチプレイ(予測とレプリケーション)とも関係が深い。

**Independent Test**: Health を持つ AttributeSet と、Health を 10 減らす Gameplay Effect を作り、PIE で適用すると属性値が 10 減る。

**Acceptance Scenarios**:

1. **Given** プロジェクト、**When** GAS を有効化する、**Then** プラグインとモジュール依存(`GameplayAbilities`、`GameplayTags`、`GameplayTasks`)が追加される。
2. **Given** 属性の一覧、**When** AttributeSet を作る、**Then** アクセサマクロ、レプリケーション、OnRep を含む C++ が生成され、ビルドが通る。
3. **Given** Gameplay Effect、**When** Modifier(属性、演算、大きさ)、期間、スタックを設定する、**Then** アセットに保存される。
4. **Given** PIE 中、**When** アビリティを付与して発動する、**Then** 発動の成否、適用されたエフェクト、変化後の属性値が返る。

---

### User Story 7 - ライティングとレンダリング設定 (Priority: P2)

エージェントがライト(Directional、Point、Spot、Rect、Sky)、SkyAtmosphere、ExponentialHeightFog、PostProcessVolume を配置・設定し、Lumen と Nanite の設定、ライティングのビルド、World Partition と HLOD のビルドを行う。

**Why this priority**: 見た目の品質を左右し、ほぼすべてのゲームで必要。

**Independent Test**: 空のレベルに屋外ライティング一式を 1 コマンドで作り、スクリーンショットの平均輝度が所定の範囲に入る。

**Acceptance Scenarios**:

1. **Given** 空のレベル、**When** 屋外ライティングのプリセットを適用する、**Then** Directional Light、Sky Light、SkyAtmosphere、Fog、PostProcessVolume が作られる。
2. **Given** レベル、**When** ライティングをビルドする、**Then** ジョブとして実行され、所要時間と警告(重なったライトマップなど)が返る。
3. **Given** プロジェクト、**When** Lumen / 仮想シャドウマップ / Nanite の設定を変える、**Then** プロジェクト設定に保存され、変更前後の値が返る。
4. **Given** World Partition のレベル、**When** HLOD をビルドする、**Then** ジョブとして実行され、結果が返る。

---

### User Story 8 - 高度なアニメーション(Control Rig、IK Retarget、Motion Matching) (Priority: P3)

エージェントが IK Rig と IK Retargeter を作ってアニメーションをリターゲットし、Control Rig を作成し、Pose Search(Motion Matching)の Database と Schema を作る。

**Why this priority**: キャラクターの品質を大きく上げるが、使う場面は限られる。

**Independent Test**: マネキンの IK Rig 2 つと Retargeter を作り、アニメーション 1 本を別スケルトンへリターゲットした結果のアセットが作られる。

**Acceptance Scenarios**:

1. **Given** スケルタルメッシュ、**When** IK Rig を作る、**Then** リターゲット用のチェーンが自動で設定される。
2. **Given** 2 つの IK Rig、**When** Retargeter を作ってアニメーションを指定する、**Then** リターゲット後のアニメーションが保存される。
3. **Given** スケルトン、**When** Control Rig を作る、**Then** ボーンの階層を読み込んだ Control Rig アセットが作られる。
4. **Given** アニメーションの一覧、**When** Pose Search Database を作る、**Then** Schema と Database が作られ、インデックスのビルド結果が返る。

---

### User Story 9 - サウンド・物理・手続き生成(MetaSound、Chaos、PCG) (Priority: P3)

エージェントが MetaSound Source を作ってノードを接続し、Chaos の Geometry Collection(破壊)を作り、PCG グラフで地形への配置を生成する。

**Why this priority**: 演出とレベル制作の効率を上げる。ゲームに必須ではない。

**Independent Test**: PCG グラフで地面に木を 100 本配置し、生成されたインスタンスの数を取得できる。

**Acceptance Scenarios**:

1. **Given** プロジェクト、**When** MetaSound Source を作ってノード(オシレーター、エンベロープなど)を接続する、**Then** アセットが保存され、PIE で再生できる。
2. **Given** スタティックメッシュ、**When** Geometry Collection を作って分割する、**Then** 分割数を指定でき、PIE で破壊できる。
3. **Given** レベル、**When** PCG グラフを作り、PCG コンポーネントで生成する、**Then** 生成数と所要時間が返る。

---

### User Story 10 - ローカライズ、Gameplay Tags、セーブデータの移行 (Priority: P3)

エージェントが Gameplay Tag を追加・一覧・リネームし、ローカライズのターゲットを作って文字列を収集・翻訳を取り込み・コンパイルする。SaveGame クラスのバージョンを管理し、古いセーブデータを移行する。

**Why this priority**: 製品化の段階で必要になる。

**Independent Test**: 日本語のカルチャを追加し、テキストを収集して翻訳(PO)を取り込み、PIE で `-culture=ja` の表示が切り替わる。

**Acceptance Scenarios**:

1. **Given** プロジェクト、**When** Gameplay Tag を追加する、**Then** タグの ini に追加され、一覧で取得できる。リネーム時はリダイレクトが追加される。
2. **Given** ローカライズのターゲット、**When** 収集 → 翻訳の取り込み → コンパイルを実行する、**Then** 各段階がジョブとして実行され、件数と警告が返る。
3. **Given** バージョンを持つ SaveGame、**When** 古い形式のセーブを読み込む、**Then** 移行処理を通した結果を検証できる。

---

### User Story 11 - 見た目とパフォーマンスの自動検証 (Priority: P2)

エージェントが基準のスクリーンショットと現在の画面の差分を取り、表示崩れを検出する。PIE 中のフレーム時間(Game / Draw / GPU)、メモリ、描画コール数を計測し、Unreal Insights のトレースを取得して要約する。

**Why this priority**: エージェントは画面を直接見られないため、数値で判定できる手段が品質に直結する。

**Independent Test**: 同じシーンの 2 枚は差分 0 で、ライトを消すと差分の割合がしきい値を超える。PIE を 10 秒計測すると平均・p95 のフレーム時間が返る。

**Acceptance Scenarios**:

1. **Given** 基準画像、**When** 現在の画面と比較する、**Then** 差分の割合、最大差、差分画像のパスが返り、しきい値を超えると 0 以外の終了コードになる。
2. **Given** PIE 中、**When** N 秒計測する、**Then** フレーム時間(平均、p95、最大)、ヒッチの回数、描画コール数、メモリ使用量が返る。
3. **Given** PIE 中、**When** トレースを取得する、**Then** .utrace のパスと、重い関数の上位一覧が返る。

---

### User Story 12 - 対応範囲の検証(UE 5.5 / 5.6、Fab の E2E) (Priority: P2)

UE 5.5 と 5.6 でプラグインをビルドし、E2E を実行する。Fab の取り込みについて、サインイン済みの環境で実行する E2E を用意する。

**Why this priority**: 001 で未検証のまま残っている対応範囲を埋める。

**Independent Test**: `bash scripts/e2e.sh 5.5` と `5.6` が全件成功する。`UNREALENGINE_CLI_E2E_FAB=1` を付けると、Fab の E2E が実行される。

**Acceptance Scenarios**:

1. **Given** UE 5.5 / 5.6、**When** プラグインをビルドする、**Then** コンパイルエラーなくビルドできる。新しい API は互換レイヤーで分岐する。
2. **Given** UE 5.5 / 5.6、**When** E2E を実行する、**Then** 全件成功する。そのバージョンで使えない機能は、`engine.unsupported` で明示的に失敗する。
3. **Given** Fab にサインイン済みのエディタ、**When** Fab の E2E を実行する、**Then** ライブラリの検索、パック 1 件と FBX 1 件の取り込み、保存の確認が成功する。

### Edge Cases

- マルチプレイの PIE で、`--world` に存在しないワールドを指定した → `pie.world_not_found` と、有効なワールドの一覧を返す。
- シングルプレイの PIE で `--world client:1` を指定した → `pie.world_not_found`。
- 専用サーバーのポートが使用中 → `server.port_in_use`。
- 専用サーバーが起動直後に終了した → `server.exited` と、ログの末尾を返す。
- インストール版エンジンで専用サーバーをビルドしようとした → ビルドを始める前に `server.source_engine_required` を返す。
- EOS / Steam の設定にシークレットを含めた → 出力とログに値を出さない(`***` で伏せる)。
- GAS が無効なプロジェクトで GAS のツールを使った → `gas.not_enabled` と有効化コマンドを返す。
- ライティングのビルド中に別のビルドを頼んだ → `lighting.build_in_progress`。
- 画像差分で解像度が異なる → `visual.size_mismatch`(自動では拡大縮小しない)。
- プラグインが無効なエンジン機能(PCG、MetaSound、Pose Search など)のツールを使った → `<group>.plugin_disabled` と有効化方法を返す。
- 計測中に PIE が終了した → それまでのサンプルで結果を返し、`partial: true` を付ける。
- UE 5.5 / 5.6 にない API を使うツール → そのバージョンでは `engine.unsupported` を返す(ビルドは通す)。

## Requirements *(mandatory)*

### Functional Requirements

#### マルチプレイの PIE(US1)

- **FR-001**: システムは、プレイヤー数(1〜4)とネットモード(standalone、listen、client)を指定して PIE を起動できなければならない。
- **FR-002**: システムは、PIE 中のワールド一覧(識別子、ネットモード、プレイヤー数、クライアント番号)を返さなければならない。
- **FR-003**: 既存の PIE ツール(actors、input、wait、screenshot、widgets、click、eval 系)は `world` 引数(`server`、`client:<n>`)を受け付け、省略時は従来どおり最初のワールドを対象にしなければならない。
- **FR-004**: アクター一覧は、各アクターの Role、RemoteRole、所有者、NetMode を含まなければならない。

#### レプリケーションと RPC(US2)

- **FR-005**: システムは、Blueprint の変数に Replicated / RepNotify と複製条件(COND_OwnerOnly など)を設定できなければならない。RepNotify では OnRep 関数を作らなければならない。
- **FR-006**: システムは、Blueprint のカスタムイベントに RPC 種別(Server、Client、NetMulticast)と Reliable を設定できなければならない。
- **FR-007**: システムは、C++ クラスにレプリケートするプロパティと RPC のひな形(宣言、`GetLifetimeReplicatedProps`、`_Implementation`、`_Validate`)を追加できなければならない。
- **FR-008**: アクター・コンポーネントのネットワーク設定(bReplicates、bReplicateMovement、NetUpdateFrequency、NetCullDistanceSquared、bAlwaysRelevant、NetDormancy)は、既存の prop ツールで読み書きできなければならない(必要なら名前の別名を追加する)。

#### 同期の検証(US3)

- **FR-009**: システムは、アクター(ラベル、クラス、またはネット GUID)とプロパティを指定して、全ワールドの値を比較し、一致するまで待つアサーションを提供しなければならない。
- **FR-010**: システムは、ネットワークエミュレーション(遅延、ジッター、パケットロス)を PIE に設定・解除できなければならない。

#### 専用サーバー(US4)

- **FR-011**: システムは、Server ターゲット(`<Project>Server.Target.cs`)を作れなければならない。変更内容を事前に表示し、`--dry-run` に対応しなければならない。
- **FR-012**: システムは、専用サーバーのビルド前にエンジンがソース版かどうかを判定し、インストール版では `server.source_engine_required` で止めなければならない。
- **FR-013**: システムは、サーバーのビルドとパッケージ化(Server 構成の Cook と Stage)を、ジョブとして実行しなければならない。出力先の安全確認は、001 のパッケージングと同じ規則に従う。
- **FR-014**: システムは、サーバーの起動(マップ、ポート、追加引数)、待ち受けの確認、ログ取得、停止を提供しなければならない。起動したサーバーは CLI のインスタンス管理とは別に記録し、PID が別プロジェクトのものを誤って停止してはならない。
- **FR-015**: システムは、PIE またはパッケージ版のクライアントを、指定したサーバーへ接続させられなければならない。

#### オンラインセッション(US5)

- **FR-016**: システムは、Null、EOS、Steam のサブシステム設定を、プラグインの有効化と ini の編集としてプロジェクトに書き込めなければならない。
- **FR-017**: システムは、構成の検証で不足キーの一覧を返し、秘密情報の値を出力してはならない。
- **FR-018**: システムは、PIE 中にセッションの作成・検索・参加・破棄を実行し、結果を返さなければならない(Null で自動テストする)。

#### GAS(US6)

- **FR-019**: システムは、GAS の有効化(プラグインとモジュール依存の追加)を、事前表示つきで行えなければならない。
- **FR-020**: システムは、AttributeSet の C++ を、属性の一覧から生成できなければならない(アクセサ、レプリケーション、OnRep を含む)。
- **FR-021**: システムは、Gameplay Ability、Gameplay Effect(Modifier、期間、スタック、タグ)、Gameplay Cue の作成と設定を提供しなければならない。
- **FR-022**: システムは、PIE 中にアビリティの付与・発動・エフェクトの適用と、属性値・付与済みタグの取得を提供しなければならない。

#### ライティングとレンダリング(US7)

- **FR-023**: システムは、屋外・屋内のライティングプリセットを 1 回の操作でレベルに適用できなければならない。
- **FR-024**: システムは、ライティングのビルドと HLOD のビルドを、ジョブとして実行しなければならない。
- **FR-025**: システムは、Lumen、仮想シャドウマップ、Nanite、アンチエイリアスなどの主要なレンダリング設定を、名前付きで読み書きできなければならない。

#### 高度なアニメーション(US8)

- **FR-026**: システムは、IK Rig(チェーンの自動設定)、IK Retargeter、アニメーションの一括リターゲットを提供しなければならない。
- **FR-027**: システムは、Control Rig のアセットをスケルトンから作れなければならない。
- **FR-028**: システムは、Pose Search の Schema と Database を作り、アニメーションを登録できなければならない。

#### サウンド・物理・手続き生成(US9)

- **FR-029**: システムは、MetaSound Source の作成、ノードの追加、接続、入力値の設定を提供しなければならない。
- **FR-030**: システムは、スタティックメッシュから Geometry Collection を作り、分割できなければならない。
- **FR-031**: システムは、PCG グラフの作成、ノードの追加と接続、PCG コンポーネントでの生成と結果の取得を提供しなければならない。

#### ローカライズ、タグ、セーブ(US10)

- **FR-032**: システムは、Gameplay Tag の追加、一覧、リネーム(リダイレクト付き)、削除を提供しなければならない。
- **FR-033**: システムは、ローカライズのターゲットとカルチャの追加、収集、PO の書き出しと取り込み、コンパイルを、ジョブとして提供しなければならない。
- **FR-034**: スキルは、SaveGame のバージョン管理と移行の手順を示さなければならない。移行の検証は E2E で行う。

#### 見た目とパフォーマンス(US11)

- **FR-035**: システムは、2 枚の画像の差分(差分ピクセルの割合、最大差、差分画像)を計算し、しきい値で合否を返さなければならない。基準画像の保存と比較を 1 コマンドで行えなければならない。
- **FR-036**: システムは、PIE 中のフレーム時間(Game、Draw、GPU)、ヒッチ、描画コール数、メモリを N 秒間計測し、統計を返さなければならない。
- **FR-037**: システムは、Unreal Insights のトレースの開始・停止と、トレースの要約(重い CPU スコープの上位)を提供しなければならない。

#### 対応範囲(US12)

- **FR-038**: プラグインは UE 5.5〜5.8 でビルドできなければならない。新しい API は `Private/Compat/` で分岐し、使えない機能は `engine.unsupported` を返す。
- **FR-039**: E2E は、UE 5.5 / 5.6 / 5.7 / 5.8 で実行できなければならない。
- **FR-040**: Fab の E2E は、環境変数で明示したときだけ実行しなければならない。ユーザーのライブラリを変更してはならない(`--acquire-free` を使わない)。

#### 共通

- **FR-041**: 新しいツールはすべて、001 のツール登録・スキーマ・エンベロープ・エラーコード(次の手順のヒント付き)・dry-run・Undo の規則に従わなければならない。
- **FR-042**: 新しいツール群ごとに、型付きの CLI サブコマンド(`net`、`server`、`online`、`gas`、`light`、`anim`、`audio`、`pcg`、`tags`、`loc`、`visual`、`perf`)を用意しなければならない。
- **FR-043**: 新しい領域ごとにスキルを追加し、`skills lint` を通さなければならない。
- **FR-044**: 自動テストは必須とする。各ツールに少なくとも 1 本の自動テスト(CLI のモックテスト、または E2E)を持たせる。テストは実装より先に書き、失敗を確認する。
- **FR-045**: README、`docs/tools.md`、`tools/catalog.json`、CHANGELOG を更新し、README のツール数は自動テストでカタログと一致させなければならない。

### Key Entities

- **PIE ワールド**: 識別子(`server`、`client:<n>`、`standalone`)、ネットモード、クライアント番号、プレイヤー数。
- **ネットワーク条件**: 遅延(ms)、ジッター(ms)、パケットロス(%)。
- **同期アサーション**: 対象アクター、プロパティのパス、期待(全ワールド一致、または値)、タイムアウト。結果はワールドごとの値と合否。
- **専用サーバー**: ビルド構成、実行ファイル、マップ、ポート、PID、ログファイル、起動元プロジェクト。
- **オンライン構成**: サブシステム種別、有効化するプラグイン、ini のキーと値(秘密は伏せる)、不足キー。
- **GAS アセット**: AttributeSet(属性名と既定値)、Ability、Effect(Modifier、期間、スタック、タグ)、Cue、Tag。
- **画像差分**: 基準画像、比較画像、しきい値、差分割合、差分画像。
- **性能サンプル**: 計測時間、フレーム時間の統計、ヒッチ数、描画コール、メモリ、トレースのパス。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: エージェントが、2 人のマルチプレイのゲーム(移動の同期、スコアのレプリケーション、Server RPC)を、ゼロから作ってアサーションで検証するまでを、人の手を介さずに完了できる(`scripts/e2e-net.py` が全項目合格)。
- **SC-002**: 同期アサーションは、一致した状態であれば 1 秒以内に成功を返す。
- **SC-003**: ソース版エンジンの環境で、専用サーバーのビルド → 起動 → クライアント接続 → 停止が 1 本のスクリプトで成功する。インストール版エンジンでは、ビルドを始める前(5 秒以内)に `server.source_engine_required` を返す。
- **SC-004**: GAS の E2E(属性、エフェクトの適用、アビリティの発動)が全項目合格する。
- **SC-005**: 新しいツールはすべて自動テストを持ち、品質ゲート(fmt、clippy、test、skills lint、E2E)がすべて通る。
- **SC-006**: E2E が UE 5.5 / 5.6 / 5.7 / 5.8 で全件成功する。
- **SC-007**: 同じシーンの画像差分は 0.1% 以下になり、意図した変更(ライトの消灯など)は 5% 以上の差分として検出される。
- **SC-008**: 10 秒間の性能計測が、計測時間 + 5 秒以内に結果を返す。

## Assumptions

- 専用サーバーのビルドにはソース版エンジン(GitHub から取得してビルドしたもの)が必要。インストール版エンジンのユーザーは、Listen Server と PIE の Client モードでマルチプレイを開発する。
- EOS / Steam の実アカウントでの接続は、自動テストの対象外とする(手動の確認手順を quickstart に記載)。
- 新しい機能は、エンジン同梱のプラグイン(GameplayAbilities、PCG、Metasound、ControlRig、IKRig、PoseSearch、GeometryCollection、OnlineSubsystem)を使う。プロジェクトで無効なら、ツールが有効化方法を案内する。
- Windows で検証する。macOS / Linux は、001 と同じく CLI 単体の動作だけを保証する。
- 001 の制約(`--null-rhi` ではメッシュと PIE がクラッシュする)により、PIE・ライティング・画像差分の E2E は描画ありで実行する。
- Live Coding のロックはエンジン単位のため、C++ を生成する E2E(GAS、レプリケーション)は、同じエンジンを使う他のビルドと並列に実行しない。
