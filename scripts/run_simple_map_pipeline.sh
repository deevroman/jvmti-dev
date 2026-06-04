#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/.artifacts/pipeline_simple_map}"

"$ROOT_DIR/scripts/run_pipeline.sh" \
  --config-file "$ROOT_DIR/config_simple_map.json" \
  --app-main SimpleMapExample \
  --app-classpath "$ROOT_DIR/java_examples" \
  --output-dir "$OUTPUT_DIR"
