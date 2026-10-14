#!/usr/bin/env bash
# Acceptance E2E: builds the plugin for an engine, runs its Automation tests, opens the editor
# headless and runs tests/e2e_editor.rs against it.
# Usage: scripts/e2e.sh [5.7|5.8]
set -uo pipefail
VER="${1:-5.7}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
U="./target/debug/unreal-cli --project TestProject"
cargo build -q || exit 1
$U editor close --force >/dev/null 2>&1 || true
bash scripts/dev-build.sh "$VER" || { echo "plugin build failed for $VER"; exit 1; }
bash scripts/run-automation.sh "$VER" || { echo "automation tests failed for $VER"; exit 1; }
$U editor open --null-rhi --output json >/dev/null || { echo "editor did not start"; exit 1; }
UNREAL_CLI_E2E=1 cargo test -q --test e2e_editor -- --test-threads=1 2>&1 | tail -n 30
CODE=${PIPESTATUS[0]}
$U editor close --force >/dev/null 2>&1 || true
exit "$CODE"
