#!/usr/bin/env bash
# Runs the plugin's Automation tests headless and prints a summary.
# Usage: scripts/run-automation.sh [5.7|5.8] [filter]
set -u
VER="${1:-5.7}"
FILTER="${2:-UnrealCliBridge}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPORT="$ROOT/target/automation-$VER"
rm -rf "$REPORT"
mkdir -p "$REPORT"
"/c/Program Files/Epic Games/UE_$VER/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "$(cygpath -w "$ROOT/TestProject/TestProject.uproject")" \
  -ExecCmds="Automation RunTests $FILTER;Quit" -unattended -nullrhi -nosplash -nosound -NoUnrealCliBridge \
  -ReportExportPath="$(cygpath -w "$REPORT")" -log="automation-$VER.log" > "$REPORT/stdout.log" 2>&1
python - "$REPORT/index.json" <<'EOF'
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding="utf-8-sig"))
except Exception as e:
    print("no report:", e); sys.exit(1)
failed = [t for t in d["tests"] if t["state"] not in ("Success", "SuccessWithWarnings")]
print("total=%d succeeded=%d withWarnings=%d failed=%d notRun=%d" % (len(d["tests"]), d["succeeded"], d.get("succeededWithWarnings", 0), d["failed"], d.get("notRun", 0)))
for t in failed:
    print("FAIL", t["fullTestPath"])
    for e in t["entries"]:
        if e["event"]["type"] in ("Error", "Warning"):
            print("   ", e["event"]["type"], e["event"]["message"][:300])
sys.exit(1 if failed or d["succeeded"] == 0 else 0)
EOF
