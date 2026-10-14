#!/usr/bin/env bash
# Writes docs/tools.md from tools/catalog.json.
set -euo pipefail
cd "$(dirname "$0")/.."
python - <<'EOF'
import json, collections
tools = json.load(open("tools/catalog.json", encoding="utf-8"))
groups = collections.defaultdict(list)
for t in tools:
    groups[t["group"]].append(t)
out = ["# Tool catalog", "", f"{len(tools)} tools. Generated from tools/catalog.json by scripts/gen-docs.sh.", "",
       "Call any tool with `unreal-cli tool call <name> --args '<json>'`; see its schema with `unreal-cli tool describe <name>`.", ""]
for g in sorted(groups):
    out += [f"## {g}", "", "| Tool | Changes | Summary |", "|---|---|---|"]
    for t in sorted(groups[g], key=lambda t: t["name"]):
        out.append(f"| `{t['name']}` | {'yes' if t['mutating'] else ''} | {t['summary'].replace('|', '/')} |")
    out.append("")
open("docs/tools.md", "w", encoding="utf-8", newline="\n").write("\n".join(out))
print(len(tools), "tools documented")
EOF
