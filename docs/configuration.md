# Configuration

| Variable | Used by | Effect |
|---|---|---|
| `UNREALENGINE_CLI_HOME` | CLI, plugin | Data directory (instance files, default instance). Default: `%LOCALAPPDATA%/unrealengine-cli`, `~/Library/Application Support/unrealengine-cli`, `$XDG_DATA_HOME/unrealengine-cli`. |
| `UNREALENGINE_CLI_ENGINE_DIR` | CLI | Engine root to use instead of the project's `EngineAssociation`. |
| `UNREALENGINE_CLI_PORT` | plugin | Fixed listen port (default: any free loopback port). |
| `UNREALENGINE_CLI_REPO` | `self update`, install scripts | GitHub `owner/name` to download releases from. |
| `UNREALENGINE_CLI_NO_AUTO_UPDATE` | CLI | Disable the daily background update (an installed binary checks once a day and replaces itself with a newer verified release; `CI` also disables it). |
| `UNREALENGINE_CLI_INSTALL_DIR` | install scripts | Install directory instead of the default. |
| `UNREALENGINE_CLI_NO_MODIFY_PATH` | `install.ps1` | Do not add the install directory to the user `Path`. |
| `UNREALENGINE_CLI_VERSION` | install scripts | Release tag to install (default: latest). |
| `UNREALENGINE_CLI_KEEP_ARGS` | CLI | Disable the Git Bash path fix (arguments are passed through unchanged). |
| `UNREALENGINE_CLI_E2E`, `UNREALENGINE_CLI_E2E_RENDER` | tests | Enable editor E2E tests; also run PIE (needs a rendering editor). |

Editor command-line switch: `-NoUnrealCliBridge` starts the editor without the bridge server.
