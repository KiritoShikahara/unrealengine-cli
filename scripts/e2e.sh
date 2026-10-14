#!/usr/bin/env bash
# Acceptance E2E: builds the plugin for an engine, runs its Automation tests, opens the editor
# headless and runs tests/e2e_editor.rs against it.
# Usage: scripts/e2e.sh [5.7|5.8]
set -uo pipefail
VER="${1:-5.7}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
U="./target/debug/unrealengine-cli --project TestProject"
cargo build -q || exit 1
$U editor close --force >/dev/null 2>&1 || true
# Test assets from earlier runs (possibly saved by another engine version) break loading and cooking.
rm -rf TestProject/Content/E2E TestProject/Content/CoinGame
bash scripts/dev-build.sh "$VER" || { echo "plugin build failed for $VER"; exit 1; }
bash scripts/run-automation.sh "$VER" || { echo "automation tests failed for $VER"; exit 1; }
# UNREALENGINE_CLI_E2E_RENDER=1 opens a rendering editor and also runs PIE and Blueprint actor spawning.
MODE=--null-rhi
[ "${UNREALENGINE_CLI_E2E_RENDER:-}" = 1 ] && MODE=""
$U editor open $MODE --wait 2400 --output json >/dev/null || { echo "editor did not start"; exit 1; }
UNREALENGINE_CLI_E2E=1 cargo test -q --test e2e_editor --test e2e_mcp_editor -- --test-threads=1 2>&1 | tail -n 30
CODE=${PIPESTATUS[0]}
if [ "$CODE" = 0 ] && [ -z "$MODE" ]; then
  python scripts/e2e-game.py 2>&1 | tail -n 3
  CODE=${PIPESTATUS[0]}
  if [ "$CODE" = 0 ]; then
    python scripts/e2e-game2.py 2>&1 | tail -n 3
    CODE=${PIPESTATUS[0]}
  fi
fi
$U editor close --force >/dev/null 2>&1 || true
# Leave the project built for its default engine (5.7) so the next `editor open` does not stop at a rebuild prompt.
if [ "$VER" != 5.7 ]; then
  bash scripts/dev-build.sh 5.7 >/dev/null 2>&1 || true
fi
exit "$CODE"
