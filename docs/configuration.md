# Configuration

| Variable | Used by | Effect |
|---|---|---|
| `UNREAL_CLI_HOME` | CLI, plugin | Data directory (instance files, default instance). Default: `%LOCALAPPDATA%/unreal-cli`, `~/Library/Application Support/unreal-cli`, `$XDG_DATA_HOME/unreal-cli`. |
| `UNREAL_CLI_ENGINE_DIR` | CLI | Engine root to use instead of the project's `EngineAssociation`. |
| `UNREAL_CLI_PORT` | plugin | Fixed listen port (default: any free loopback port). |
| `UNREAL_CLI_REPO` | `self update`, install scripts | GitHub `owner/name` to download releases from. |
| `UNREAL_CLI_VERSION` | install scripts | Release tag to install (default: latest). |
| `UNREAL_CLI_KEEP_ARGS` | CLI | Disable the Git Bash path fix (arguments are passed through unchanged). |
| `UNREAL_CLI_E2E`, `UNREAL_CLI_E2E_RENDER` | tests | Enable editor E2E tests; also run PIE (needs a rendering editor). |

Editor command-line switch: `-NoUnrealCliBridge` starts the editor without the bridge server.
