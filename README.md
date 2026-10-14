# unrealengine-cli

[日本語](README.ja.md)

`unrealengine-cli` is a Rust CLI that lets AI agents (Claude Code, Codex, Cursor, ...) drive Unreal Editor 5.5–5.8.
It talks to an editor plugin, **UnrealCliBridge**, over a loopback TCP connection and exposes 137 editor tools:
Blueprints, levels and actors, assets, reflected properties, C++ (Live Coding / rebuild), PIE, Automation tests,
animation, UMG, materials, Niagara, landscapes, Enhanced Input, data assets, Sequencer, audio and packaging.

```text
Agent -> skills (loaded on demand) -> unrealengine-cli -> TCP 127.0.0.1 -> UnrealCliBridge (Unreal Editor)
```

## Quick start

```bash
# 1. Install (verifies SHA256SUMS before installing; needs a published release —
#    until then: cargo install --path . from a clone)
curl -fsSL https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.sh | sh   # macOS / Linux
irm https://raw.githubusercontent.com/KiritoShikahara/unrealengine-cli/main/scripts/install.ps1 | iex       # Windows

# 2. In your project folder
unrealengine-cli doctor                 # engine, build tools, plugin, editor
unrealengine-cli setup --dry-run        # shows the files it will add
unrealengine-cli setup --build          # installs the plugin and builds the editor target
unrealengine-cli editor open --headless # starts the editor and waits for the bridge
unrealengine-cli ping

# 3. Agent skills (no editor needed)
unrealengine-cli skills install claude-code --local
```

Blueprint-only projects need a C++ target to compile the plugin: `unrealengine-cli setup --add-code-module --build`.

## Using it

```bash
unrealengine-cli tool find blueprint connect          # search tools
unrealengine-cli tool describe bp_connect             # input schema
unrealengine-cli blueprint create /Game/BP_Hello
unrealengine-cli blueprint add-node /Game/BP_Hello --type Event --member BeginPlay
unrealengine-cli blueprint add-node /Game/BP_Hello --type CallFunction --member PrintString
unrealengine-cli blueprint connect /Game/BP_Hello N1.then N2.execute
unrealengine-cli blueprint compile /Game/BP_Hello
unrealengine-cli pie run --seconds 10 --stop-on-log Hello
unrealengine-cli tool call landscape_create --args '{"components":2}'
```

* Every command supports `--output json`: `{ ok, command, data | error { code, message, hint }, warnings, meta }`.
* Exit codes: 0 ok, 2 input, 3 connection, 4 editor, 5 timeout, 6 build, 1 internal.
* `--dry-run` previews any change; editor changes are undoable (`tool call editor_undo`).
* MCP clients: `unrealengine-cli mcp configure claude-code` registers `unrealengine-cli mcp serve`.
* Git Bash on Windows rewrites `/Game/...` arguments; unrealengine-cli restores them.

## Editor modes

| Command | Window | Notes |
|---|---|---|
| `editor open` | yes | normal |
| `editor open --headless` | no | offscreen rendering; first launch compiles shaders |
| `editor open --null-rhi` | no | fastest; spawning mesh actors and PIE crash the engine in this mode |

## Development

```bash
cargo fmt --all -- --check
cargo clippy --all-targets -- -D warnings
cargo test --all-targets
cargo run -- skills lint --severity error
bash scripts/e2e.sh 5.7     # plugin build + Automation tests + editor E2E (Windows)
bash scripts/gen-catalog.sh # refresh tools/catalog.json from a running editor
```

See [docs/configuration.md](docs/configuration.md), [docs/architecture.md](docs/architecture.md), [docs/tools.md](docs/tools.md), [docs/skills.md](docs/skills.md)
and [docs/editor-compatibility.md](docs/editor-compatibility.md).

## Reporting bugs

Open a [GitHub issue](https://github.com/KiritoShikahara/unrealengine-cli/issues/new?template=bug_report.yml) and paste the
output of `unrealengine-cli doctor --report` (run in the project folder) with the failing command's `--output json` result.

## License

MIT
