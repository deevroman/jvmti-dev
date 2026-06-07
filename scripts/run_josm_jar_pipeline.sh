#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "scripts/run_josm_jar_pipeline.sh is deprecated; use scripts/run_josm_app_pipeline.sh." >&2
exec "$ROOT_DIR/scripts/run_josm_app_pipeline.sh" "$@"
