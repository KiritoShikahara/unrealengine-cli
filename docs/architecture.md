# Architecture

```text
unrealengine-cli (Rust)                         Unreal Editor + UnrealCliBridge
  cli.rs / commands/*  --TCP 127.0.0.1-->  BridgeServer (thread per connection)
  transport/frame.rs     u32 BE length       -> queue, drained on the game thread by the core ticker
  transport/client.rs    + UTF-8 JSON        -> ToolRegistry::Dispatch
                                                  schema check, dry-run, undo transaction, jobs
                                                  C++ tools  (Private/Tools/*.cpp)
                                                  Python tools (Content/Python/unreal_cli_bridge)
```

* **Discovery**: on start the plugin writes `<data dir>/instances/<pid>.json` (port, session token, project,
  engine version). The CLI picks an editor by `--instance`, `--project`, the project in the working directory,
  `instances use`, or the only running editor. Stale files are removed when their port no longer answers.
* **Protocol v1** (`specs/001-unreal-cli/contracts/protocol.md`): `hello` with the token, then `call`/`result`
  frames. Long-running tools return a `jobId` (`jobs get|logs|cancel`).
* **Tools** are registered with a JSON Schema. Mutating tools honour `dryRun` and run inside an undo transaction.
  Python tools are discovered from the plugin's Python package the first time Python is ready.
* **Threading**: calls run from the core ticker, between frames, never inside a world tick (switching levels or
  starting PIE from a task-graph task inside a tick group trips engine assertions). Tools that wait (PIE, Live
  Coding, input holds) register their own tickers and reply later, so the editor keeps running meanwhile.
* **Versions**: the plugin reports its uplugin `VersionName` (kept equal to the CLI version by a test); every
  command warns when the running editor's plugin differs from the one the CLI bundles.
* **Paths**: the editor's working directory is the engine's `Binaries` folder, so tools resolve relative file paths
  under the project and refuse to write into an engine install.
* **Envelope**: every command prints `{ ok, command, data | error, warnings, meta }`; exit codes separate input,
  connection, editor, timeout and build failures.
* **Lifecycle**: `setup` copies the bundled plugin sources and enables it in the .uproject; `editor build` runs
  UnrealBuildTool and parses MSVC / clang / UBT diagnostics; `editor open` starts the editor detached (no handle
  inheritance, so callers' shells return) and waits for a ping; `editor close --force` ends an editor that does not
  answer, with its child processes; `code apply` chooses Live Coding (.cpp only) or close → rebuild → reopen (the open
  level is reopened), and with `--force` falls back to a rebuild when Live Coding does not finish.
* **Agents**: skills are bundled in the binary and installed per client; `mcp serve` exposes ping, tool search,
  describe and call over MCP.
