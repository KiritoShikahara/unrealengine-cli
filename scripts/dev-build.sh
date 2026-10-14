#!/usr/bin/env bash
# Builds TestProjectEditor against an installed engine and prints compiler diagnostics only.
# Usage: scripts/dev-build.sh [5.7|5.8]
set -u
VER="${1:-5.7}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPROJECT="$(cygpath -w "$ROOT/TestProject/TestProject.uproject")"
sed -i "s/\"EngineAssociation\": \"[0-9.]*\"/\"EngineAssociation\": \"$VER\"/" "$ROOT/TestProject/TestProject.uproject"
VSLANG=1033 "/c/Program Files/Epic Games/UE_$VER/Engine/Build/BatchFiles/Build.bat" TestProjectEditor Win64 Development \
  -Project="$UPROJECT" -WaitMutex > "$ROOT/target/ue-build-$VER.log" 2>&1
CODE=$?
iconv -f CP932 -t UTF-8 "$ROOT/target/ue-build-$VER.log" 2>/dev/null | grep -aE ": (fatal )?error|: warning C|Result:" | sed 's/.*UnrealCliBridge\\Source\\//' | head -40
exit $CODE
