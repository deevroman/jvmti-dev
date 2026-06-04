#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -n "${AGENT_LIB:-}" ]]; then
  if [[ -f "$AGENT_LIB" ]]; then
    printf '%s\n' "$AGENT_LIB"
    exit 0
  fi
  echo "AGENT_LIB points to missing file: $AGENT_LIB" >&2
  exit 1
fi

declare -a candidates=(
  "$ROOT_DIR/src/cmake-build-debug/libdumper.so"
  "$ROOT_DIR/src/cmake-build-debug/libdumper.dylib"
  "$ROOT_DIR/src/cmake-build-release/libdumper.so"
  "$ROOT_DIR/src/cmake-build-release/libdumper.dylib"
  "$ROOT_DIR/src/build/libdumper.so"
  "$ROOT_DIR/src/build/libdumper.dylib"
)

for candidate in "${candidates[@]}"; do
  if [[ -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    exit 0
  fi
done

echo "Could not find built JVMTI agent library. Looked in common build directories under $ROOT_DIR/src." >&2
echo "Pass --agent-lib <path> or set AGENT_LIB to override." >&2
exit 1
