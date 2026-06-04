#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_pipeline.sh"

RUNNER_SOURCE="$ROOT_DIR/jar_example/klaw_compareutils/KlawCompareUtilsRunner.java"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_klaw_compareutils"
KLAW_REPO_DIR="${KLAW_REPO_DIR:-$ROOT_DIR/jar_example/klaw_compareutils/klaw}"
KLAW_REPO_URL="${KLAW_REPO_URL:-https://github.com/aiven-open/klaw.git}"
LLM_ONLY=0
SOCKET_MODE=0
SOCKET_HOST=""
SOCKET_PORT=""
SOCKET_TIMEOUT=""
PIPELINE_NAMES=(
  "evaluate_false"
  "evaluate_true"
  "compare_equal"
  "evaluate_trimmed_false"
  "false_and_true"
)
PIPELINE_CONFIGS=(
  "$ROOT_DIR/config_klaw_compareutils.json"
  "$ROOT_DIR/config_klaw_compareutils_true.json"
  "$ROOT_DIR/config_klaw_compareutils_equal.json"
  "$ROOT_DIR/config_klaw_compareutils_trimmed_false.json"
  "$ROOT_DIR/config_klaw_compareutils_false_and_true.json"
)

print_usage() {
  cat <<USAGE
Usage:
  $0 [output_dir] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]

Environment:
  KLAW_REPO_DIR  (default: $ROOT_DIR/jar_example/klaw_compareutils/klaw)
  KLAW_REPO_URL  (default: https://github.com/aiven-open/klaw.git)
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --llm)
      LLM_ONLY=1
      shift
      ;;
    --socket)
      SOCKET_MODE=1
      shift
      ;;
    --socket-host)
      SOCKET_HOST="$2"
      shift 2
      ;;
    --socket-port)
      SOCKET_PORT="$2"
      shift 2
      ;;
    --socket-timeout)
      SOCKET_TIMEOUT="$2"
      shift 2
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      if [[ "$OUTPUT_DIR" == "$ROOT_DIR/.artifacts/pipeline_klaw_compareutils" ]]; then
        OUTPUT_DIR="$1"
      else
        echo "Unknown argument: $1" >&2
        print_usage
        exit 2
      fi
      shift
      ;;
  esac
done

if [[ "$OUTPUT_DIR" != /* ]]; then
  OUTPUT_DIR="$ROOT_DIR/$OUTPUT_DIR"
fi

if [[ ! -f "$PIPELINE_SCRIPT" ]]; then
  echo "Pipeline script not found: $PIPELINE_SCRIPT" >&2
  exit 1
fi
if [[ ! -f "$RUNNER_SOURCE" ]]; then
  echo "Runner source not found: $RUNNER_SOURCE" >&2
  exit 1
fi

KLAW_COMPARE_UTILS_SOURCE="$KLAW_REPO_DIR/core/src/main/java/io/aiven/klaw/helpers/CompareUtils.java"
if [[ ! -f "$KLAW_COMPARE_UTILS_SOURCE" ]]; then
  if [[ -d "$KLAW_REPO_DIR/.git" ]]; then
    echo "Incomplete Klaw checkout detected in $KLAW_REPO_DIR, recreating it"
    rm -rf "$KLAW_REPO_DIR"
  fi
  echo "Cloning Klaw repository into $KLAW_REPO_DIR"
  git clone --depth 1 "$KLAW_REPO_URL" "$KLAW_REPO_DIR"
fi

if [[ ! -f "$KLAW_COMPARE_UTILS_SOURCE" ]]; then
  echo "Klaw source file not found: $KLAW_COMPARE_UTILS_SOURCE" >&2
  exit 1
fi

APP_CLASSES_DIR="$OUTPUT_DIR/app_classes"
mkdir -p "$APP_CLASSES_DIR"

echo "[0/2] Compiling Klaw class and runner with debug info"
javac -g -d "$APP_CLASSES_DIR" "$KLAW_COMPARE_UTILS_SOURCE" "$RUNNER_SOURCE"

for index in "${!PIPELINE_NAMES[@]}"; do
  pipeline_name="${PIPELINE_NAMES[$index]}"
  config_file="${PIPELINE_CONFIGS[$index]}"
  pipeline_output_dir="$OUTPUT_DIR/$pipeline_name"

  if [[ ! -f "$config_file" ]]; then
    echo "Config file not found: $config_file" >&2
    exit 1
  fi

  CMD=(
    "$PIPELINE_SCRIPT"
    --config-file "$config_file"
    --app-main "KlawCompareUtilsRunner"
    --app-classpath "$APP_CLASSES_DIR"
    --output-dir "$pipeline_output_dir"
  )

  if [[ "$LLM_ONLY" -eq 1 ]]; then
    CMD+=(--llm)
  fi
  if [[ "$SOCKET_MODE" -eq 1 ]]; then
    CMD+=(--socket)
  fi
  if [[ -n "$SOCKET_HOST" ]]; then
    CMD+=(--socket-host "$SOCKET_HOST")
  fi
  if [[ -n "$SOCKET_PORT" ]]; then
    CMD+=(--socket-port "$SOCKET_PORT")
  fi
  if [[ -n "$SOCKET_TIMEOUT" ]]; then
    CMD+=(--socket-timeout "$SOCKET_TIMEOUT")
  fi

  echo "[$((index + 1))/${#PIPELINE_NAMES[@]}] Running Klaw pipeline: $pipeline_name"
  "${CMD[@]}"
done

echo "[2/2] Klaw stand pipeline completed successfully."
