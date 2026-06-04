#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JAR_PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_jar_pipeline.sh"

RUNNER_SOURCE="$ROOT_DIR/jar_example/json_parser/JsonParserRunner.java"
RUNNER_MAIN="JsonParserRunner"
GSON_VERSION="${GSON_VERSION:-2.11.0}"
GSON_DIR="$ROOT_DIR/jar_example/json_parser"
GSON_JAR="$GSON_DIR/gson-$GSON_VERSION.jar"
GSON_JAR_URL="${GSON_JAR_URL:-https://repo1.maven.org/maven2/com/google/code/gson/gson/$GSON_VERSION/gson-$GSON_VERSION.jar}"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_json_parser"
LLM_ONLY=0
CALLSITE_METRICS_FILE_PATH=""
SOCKET_MODE=0
SOCKET_HOST=""
SOCKET_PORT=""
SOCKET_TIMEOUT=""
PIPELINE_NAMES=(
  "extract_value"
  "parse_reader"
  "parse_json_reader"
  "first_property_name"
  "extract_name_with_reader"
)
PIPELINE_CONFIGS=(
  "$ROOT_DIR/config_json_parser.json"
  "$ROOT_DIR/config_json_parser_parse_reader.json"
  "$ROOT_DIR/config_json_parser_parse_json_reader.json"
  "$ROOT_DIR/config_json_parser_first_property_name.json"
  "$ROOT_DIR/config_json_parser_extract_name_with_reader.json"
)

print_usage() {
  cat <<USAGE
Usage:
  $0 [output_dir] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]

Environment:
  GSON_VERSION  (default: 2.11.0)
  GSON_JAR_URL  (default: Maven Central URL built from GSON_VERSION)
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
      if [[ "$OUTPUT_DIR" == "$ROOT_DIR/.artifacts/pipeline_json_parser" ]]; then
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

if [[ ! -f "$JAR_PIPELINE_SCRIPT" ]]; then
  echo "Jar pipeline script not found: $JAR_PIPELINE_SCRIPT" >&2
  exit 1
fi
if [[ ! -f "$RUNNER_SOURCE" ]]; then
  echo "Runner source not found: $RUNNER_SOURCE" >&2
  exit 1
fi

mkdir -p "$GSON_DIR"
if [[ ! -f "$GSON_JAR" ]]; then
  echo "Downloading Gson jar from: $GSON_JAR_URL"
  curl -fL --retry 2 --connect-timeout 10 -o "$GSON_JAR" "$GSON_JAR_URL"
fi

for index in "${!PIPELINE_NAMES[@]}"; do
  pipeline_name="${PIPELINE_NAMES[$index]}"
  config_file="${PIPELINE_CONFIGS[$index]}"
  pipeline_output_dir="$OUTPUT_DIR/$pipeline_name"

  if [[ ! -f "$config_file" ]]; then
    echo "Config file not found: $config_file" >&2
    exit 1
  fi

  if [[ -z "${CALLSITE_METRICS_FILE:-}" ]]; then
    CALLSITE_METRICS_FILE_PATH="$pipeline_output_dir/callsite_metrics.csv"
  else
    CALLSITE_METRICS_FILE_PATH="$CALLSITE_METRICS_FILE"
  fi

  CMD=(
    "$JAR_PIPELINE_SCRIPT"
    --config-file "$config_file"
    --runner-source "$RUNNER_SOURCE"
    --runner-main "$RUNNER_MAIN"
    --jar-file "$GSON_JAR"
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

  echo "[$((index + 1))/${#PIPELINE_NAMES[@]}] Running JSON parser pipeline: $pipeline_name"
  echo "Call-site metrics file: $CALLSITE_METRICS_FILE_PATH"
  CALLSITE_METRICS_FILE="$CALLSITE_METRICS_FILE_PATH" "${CMD[@]}"
done
