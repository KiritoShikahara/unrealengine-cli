# Research: unrealengine-cli

## R1. 通信方式
- **Decision**: 127.0.0.1 上の TCP。フレームは「4 バイトの長さ(ビッグエンディアン)」と UTF-8 JSON。ポートは自動で割り当て、インスタンスファイルに記録する。
- **Rationale**: エンジン標準の `FSocket` / `FTcpListener` だけで実装でき、追加の依存が不要。
- **Alternatives**: Remote Control API(HTTP)は外部プラグインに依存し、ジョブやスキーマ公開の自由度が低いため不採用。

## R2. ゲームスレッドとの整合
- **Decision**: 受信はワーカースレッドで行い、ツールの実行は `FTSTicker` 経由でゲームスレッドに送る。一定時間を超える処理はジョブとして実行する。
- **Rationale**: UObject の操作はゲームスレッドでしか安全に行えない。

## R3. インスタンス検出
- **Decision**: プラグインが起動時に `%LOCALAPPDATA%/unrealengine-cli/instances/<pid>.json`(ポート、トークン、プロジェクト、エンジンバージョン)を書き、CLI は PID の生存を確認して使う。接続先は「カレントディレクトリから辿れる .uproject」を優先する。

## R4. エンジン検出とビルド
- **Decision**: .uproject の `EngineAssociation` を、Windows レジストリ(`HKLM\SOFTWARE\EpicGames\Unreal Engine`、`HKCU\...\Builds`)と `LauncherInstalled.dat` で解決する。ビルドは `Build.bat <Proj>Editor Win64 Development -Project=...`、エラーは MSVC / clang の出力を正規表現で分解する。

## R5. C++ の反映
- **Decision**: 変更が .cpp だけなら `ILiveCodingModule::Compile`。ヘッダー、.Build.cs、.Target.cs、.uproject、.uplugin が変わったら、エディタを終了 → ビルド → 再起動。変更の検出は、前回ビルド時のファイルハッシュとの差分で行う。

## R6. ツール定義の正本
- **Decision**: プラグイン側の C++ ツール登録を正本とし、`tools.list` で JSON Schema を返す。CLI はビルド時にスナップショット(`tools/catalog.json`)を同梱し、オフラインでも検索できるようにする。契約テストで、同梱カタログと実エディタのカタログが一致することを確認する。

## R7. UE 5.5〜5.8 の差分
- **Decision**: `ENGINE_MAJOR_VERSION` / `ENGINE_MINOR_VERSION` のマクロで分岐し、差分は `Private/Compat/` に集める。CI は 4 バージョンでプラグインをビルドする(self-hosted の Windows ランナー)。

## R8. MCP
- **Decision**: `unrealengine-cli mcp serve` で stdio MCP サーバーを起動する。公開するのはよく使うツールと、`find_tools` / `call` の 2 つだけにし、コンテキストの消費を抑える。`mcp configure <client>` でクライアント設定を書き込む。

## R9. 配布
- **Decision**: GitHub Releases(SHA256SUMS 付き)、`install.ps1` / `install.sh`、winget、Homebrew tap。リリースは cargo-dist ではなく、独自の GitHub Actions で行う(成果物の配置を自分で管理するため)。

## R10. リファレンス参照(FR-033)
- **Decision**: v1 では、ローカルに入っているエンジンソース(`Engine/Source`)と Python API スタブ(`Intermediate/PythonStub/unreal.py`)を、読み取り専用で検索できるようにする。Web ドキュメントの取得は扱わない。
