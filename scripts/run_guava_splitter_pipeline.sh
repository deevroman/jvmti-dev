#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JAR_PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_jar_pipeline.sh"

RUNNER_SOURCE="$ROOT_DIR/jar_example/guava_splitter/GuavaSplitterRunner.java"
RUNNER_MAIN="GuavaSplitterRunner"
GUAVA_VERSION="${GUAVA_VERSION:-33.2.1-jre}"
GUAVA_DIR="$ROOT_DIR/jar_example/guava_splitter"
GUAVA_JAR="$GUAVA_DIR/guava-$GUAVA_VERSION.jar"
GUAVA_JAR_URL="${GUAVA_JAR_URL:-https://repo1.maven.org/maven2/com/google/guava/guava/$GUAVA_VERSION/guava-$GUAVA_VERSION.jar}"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_guava_splitter"
LLM_ONLY=0
SOCKET_MODE=0
SOCKET_HOST=""
SOCKET_PORT=""
SOCKET_TIMEOUT=""
PIPELINE_NAMES=(
  "split_csv"
  "count_tokens"
  "first_token"
  "key_value_pairs"
  "fixed_length"
)
PIPELINE_CONFIGS=(
  "$ROOT_DIR/config_guava_splitter.json"
  "$ROOT_DIR/config_guava_splitter_count_tokens.json"
  "$ROOT_DIR/config_guava_splitter_first_token.json"
  "$ROOT_DIR/config_guava_splitter_key_value_pairs.json"
  "$ROOT_DIR/config_guava_splitter_fixed_length.json"
)

print_usage() {
  cat <<USAGE
Usage:
  $0 [output_dir] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]

Environment:
  GUAVA_VERSION  (default: 33.2.1-jre)
  GUAVA_JAR_URL  (default: Maven Central URL built from GUAVA_VERSION)
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
      if [[ "$OUTPUT_DIR" == "$ROOT_DIR/.artifacts/pipeline_guava_splitter" ]]; then
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

mkdir -p "$GUAVA_DIR"
if [[ ! -f "$GUAVA_JAR" ]]; then
  echo "Downloading Guava jar from: $GUAVA_JAR_URL"
  curl -fL --retry 2 --connect-timeout 10 -o "$GUAVA_JAR" "$GUAVA_JAR_URL"
fi

for index in "${!PIPELINE_NAMES[@]}"; do
  pipeline_name="${PIPELINE_NAMES[$index]}"
  config_file="${PIPELINE_CONFIGS[$index]}"
  pipeline_output_dir="$OUTPUT_DIR/$pipeline_name"

  if [[ ! -f "$config_file" ]]; then
    echo "Config file not found: $config_file" >&2
    exit 1
  fi

  CMD=(
    "$JAR_PIPELINE_SCRIPT"
    --config-file "$config_file"
    --runner-source "$RUNNER_SOURCE"
    --runner-main "$RUNNER_MAIN"
    --jar-file "$GUAVA_JAR"
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

  echo "[$((index + 1))/${#PIPELINE_NAMES[@]}] Running Guava splitter pipeline: $pipeline_name"
  "${CMD[@]}"
done
