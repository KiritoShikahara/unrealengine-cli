#!/usr/bin/env bash
# Regenerates tools/catalog.json from a running editor (the bundled offline tool catalog).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
cargo build -q
./target/debug/unreal-cli --project TestProject --output json tool call tools_list \
  | python -c "import json,sys; d=json.load(sys.stdin); assert d['ok'], d; json.dump(d['data'], open('tools/catalog.json','w',encoding='utf-8'), indent=1, ensure_ascii=False); print(len(d['data']), 'tools')"
