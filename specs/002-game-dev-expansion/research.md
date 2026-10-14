# Research: ゲーム制作機能の拡張

## R1. マルチプレイの PIE
- **Decision**: `ULevelEditorPlaySettings`(`PlayNetMode`、`PlayNumberOfClients`、`bLaunchSeparateServer`、`ServerPort`、`bRunUnderOneProcess`)を PIE 開始前に一時的に書き換え、終了後に元へ戻す。ワールドは `GEngine->GetWorldContexts()` の PIE コンテキストを `PIEInstance` と `NetMode` で識別し、`server` / `client:<n>` / `standalone` に対応づける。
- **Rationale**: エディタの「Multiplayer Options」と同じ仕組みで、1 プロセス内で全ワールドに同期アクセスできる。
- **Alternatives**: 別プロセスのクライアント(`bRunUnderOneProcess=false`)は、ワールドへの直接アクセスができないため既定にしない(パッケージ版の接続確認でだけ使う)。

## R2. 既存 PIE ツールのワールド指定
- **Decision**: 現在「最初の PIE ワールド」を返している共通ヘルパーを、`world` 引数で選べる関数に置き換える。省略時の挙動は変えない(後方互換)。入力は、そのワールドのローカル PlayerController に送る。

## R3. レプリケーションと RPC(Blueprint)
- **Decision**: 変数は `FBPVariableDescription` の `PropertyFlags`(`CPF_Net`、`CPF_RepNotify`)、`RepNotifyFunc`、`ReplicationCondition` を設定し、RepNotify では `OnRep_<Var>` 関数グラフを作る。カスタムイベントは `UK2Node_CustomEvent::FunctionFlags`(`FUNC_Net`、`FUNC_NetServer`、`FUNC_NetClient`、`FUNC_NetMulticast`、`FUNC_NetReliable`)を設定する。
- **Rationale**: エディタの詳細パネルが変更するのと同じフィールドで、コンパイル結果が一致する。

## R4. レプリケーションと RPC(C++)
- **Decision**: ヘッダーと .cpp に、決まった形のコード(`UPROPERTY(Replicated / ReplicatedUsing=OnRep_X)`、`GetLifetimeReplicatedProps` の `DOREPLIFETIME[_CONDITION]`、`UFUNCTION(Server, Reliable[, WithValidation])` と `_Implementation` / `_Validate`)を挿入する。既存の `code edit` と同じく、変更前に差分を表示する。`GetLifetimeReplicatedProps` がなければ作り、`#include "Net/UnrealNetwork.h"` を追加する。
- **Rationale**: C++ の生成はテキスト編集で足り、ビルドで検証できる。

## R5. 同期アサーション
- **Decision**: 各ワールドでアクターを解決し(ラベル → クラス → ネット GUID の順)、プロパティのパスを既存の prop 読み取り関数で文字列化して比較する。一致するまでティッカーで待つ(既定 5 秒)。
- **Network emulation**: `ULevelEditorPlaySettings::NetworkEmulationSettings`(PIE 開始前)と、実行中は各 NetDriver の `FPacketSimulationSettings`(`PktLag`、`PktLagVariance`、`PktLoss`)を設定する。

## R6. 専用サーバー
- **Decision**: インストール版かどうかは `Engine/Build/InstalledBuild.txt` の有無で判定する(インストール版には Server ターゲット用のバイナリがない)。ビルドは `Build.bat <Project>Server Win64 Development`、パッケージは `RunUAT BuildCookRun -server -noclient -serverplatform=Win64`。起動は `<Project>Server.exe <Map> -port=<p> -log` をハンドルを継承しないプロセスとして開始し、ログファイルの `Listening` 行(または UDP ポートの使用)で待ち受けを確認する。
- **Process records**: 起動したサーバーは `%LOCALAPPDATA%/unrealengine-cli/servers/<pid>.json`(プロジェクト、ポート、ログのパス、開始時刻)に記録し、`server stop` はこの記録にある PID だけを止める。
- **Alternatives**: 001 の `editor` インスタンス管理と共有すると、エディタの選択ロジック(別プロジェクトに接続しない)に影響するため分ける。

## R7. オンラインセッション
- **Decision**: 設定は `DefaultEngine.ini` の `[OnlineSubsystem] DefaultPlatformService=Null|EOS|Steam` と、サブシステムごとの節(`[OnlineSubsystemEOS]`、`[OnlineSubsystemSteam]` など)、`.uproject` のプラグイン有効化として書き込む。セッションの操作は、PIE ワールドの `IOnlineSessionPtr`(`CreateSession`、`FindSessions`、`JoinSession`、`DestroySession`)をティッカーで待つ C++ ツールで行う。
- **Secrets**: 値を出力するときは、キー名に `Secret`、`Key`、`Password`、`Token` を含むものを `***` に置き換える。

## R8. GAS
- **Decision**: 有効化は `.uproject` に `GameplayAbilities` プラグインを追加し、`.Build.cs` に `GameplayAbilities`、`GameplayTags`、`GameplayTasks` を追加する(C++ の変更なので `code apply` の判定で再ビルドになる)。AttributeSet は C++ のひな形(`ATTRIBUTE_ACCESSORS`、`FGameplayAttributeData`、レプリケーション)を生成する。Ability / Effect / Cue は、それぞれ `UGameplayAbility`、`UGameplayEffect`、`AGameplayCueNotify_Actor` の Blueprint として作り、Effect の Modifier などは既存の prop ツール(CDO の配列と構造体)で設定する。PIE の検証は、アクターの `UAbilitySystemComponent` に対する C++ ツール(付与、発動、適用、属性・タグの取得)で行う。

## R9. ライティングとレンダリング
- **Decision**: プリセットはアクターの生成と prop 設定の組み合わせとして実装する。ライティングのビルドは `FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildLighting)` をジョブで実行し、HLOD は `WorldPartitionBuilderCommandlet`(`-Builder=WorldPartitionHLODsBuilder`)を別プロセスで実行する。レンダリング設定は `URendererSettings` の名前付きプロパティを読み書きし、`DefaultEngine.ini` に保存する。

## R10. 高度なアニメーション
- **Decision**: エディタの Python API(`unreal.IKRigController`、`unreal.IKRetargeterController`、`unreal.IKRetargetBatchOperation`、`unreal.ControlRigBlueprintFactory`、Pose Search の各アセットクラス)を使う Python ツールにする。C++ でしか届かない操作が見つかった場合だけ C++ にする。

## R11. MetaSound / Chaos / PCG
- **Decision**: MetaSound は `UMetaSoundBuilderSubsystem`(ビルダー API。ノードの追加と接続、アセットへの書き出し)、Geometry Collection は `FractureEditor` のエディタ API、PCG は `UPCGGraph` の C++ API(`AddNodeOfType`、`AddEdge`)と `UPCGComponent::Generate` を使う。プラグインが無効なら `<group>.plugin_disabled` を返す。

## R12. ローカライズとタグ
- **Decision**: Gameplay Tag は `UGameplayTagsManager` と `Config/DefaultGameplayTags.ini` の編集(リネーム時は `GameplayTagRedirects`)。ローカライズは `Config/Localization/<Target>_*.ini` を作り、`UnrealEditor-Cmd -run=GatherText -config=...` をジョブとして実行する。

## R13. 画像差分と性能計測
- **Decision**: 画像差分は CLI 側(Rust、`image` クレート)で計算する。エディタ不要で、CI でも使える。比較は RGB の差の絶対値を許容値(既定 8/255)で判定し、差分ピクセルの割合を返す。性能計測は PIE 中に `FApp::GetDeltaTime` と `GGameThreadTime`、`GRenderThreadTime`、`RHIGetGPUFrameCycles`、描画コール数(`GNumDrawCallsRHI`)を毎フレーム集計する C++ ツール。トレースは `Trace.Start` / `Trace.Stop` コンソールコマンドで .utrace を作り、要約は `UnrealInsights.exe -OpenTraceFile=... -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimers ..."` で CSV に出してから集計する。

## R14. UE 5.5 / 5.6
- **Decision**: 5.5 と 5.6 をインストールし、`dev-build.sh` でビルドが通るまで `Private/Compat/` に分岐を足す。使えない機能は `MinEngine` をツール定義に設定し、レジストリが `engine.unsupported` を返す。
