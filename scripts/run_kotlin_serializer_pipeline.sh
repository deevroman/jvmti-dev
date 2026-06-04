#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_pipeline.sh"

STAND_DIR="$ROOT_DIR/jar_example/kotlin_serializer"
APP_MAIN="KotlinSerializerRunner"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_kotlin_serializer"
LLM_ONLY=0
SOCKET_MODE=0
SOCKET_HOST=""
SOCKET_PORT=""
SOCKET_TIMEOUT=""
PIPELINE_NAMES=(
  "serialize_profile"
  "extract_name"
  "extract_age"
  "is_enabled"
  "serialize_wrapped"
)
PIPELINE_CONFIGS=(
  "$ROOT_DIR/config_kotlin_serializer.json"
  "$ROOT_DIR/config_kotlin_serializer_extract_name.json"
  "$ROOT_DIR/config_kotlin_serializer_extract_age.json"
  "$ROOT_DIR/config_kotlin_serializer_is_enabled.json"
  "$ROOT_DIR/config_kotlin_serializer_wrapped.json"
)

print_usage() {
  cat <<USAGE
Usage:
  $0 [output_dir] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]
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
      if [[ "$OUTPUT_DIR" == "$ROOT_DIR/.artifacts/pipeline_kotlin_serializer" ]]; then
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
if [[ ! -f "$STAND_DIR/pom.xml" ]]; then
  echo "Kotlin stand pom.xml not found: $STAND_DIR/pom.xml" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
KOTLIN_DEPS_CP_FILE="$OUTPUT_DIR/kotlin.deps.classpath"

echo "[0/2] Compiling Kotlin serializer stand"
mvn -q -f "$STAND_DIR/pom.xml" -DskipTests compile \
  dependency:build-classpath -Dmdep.outputFile="$KOTLIN_DEPS_CP_FILE"

if [[ ! -f "$KOTLIN_DEPS_CP_FILE" ]]; then
  echo "Failed to generate dependencies classpath file: $KOTLIN_DEPS_CP_FILE" >&2
  exit 1
fi

KOTLIN_DEPS_CP="$(cat "$KOTLIN_DEPS_CP_FILE")"
if [[ -z "$KOTLIN_DEPS_CP" ]]; then
  echo "Empty Kotlin dependencies classpath in: $KOTLIN_DEPS_CP_FILE" >&2
  exit 1
fi

APP_CLASSPATH="$STAND_DIR/target/classes:$KOTLIN_DEPS_CP"

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
    --app-main "$APP_MAIN"
    --app-classpath "$APP_CLASSPATH"
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

  echo "[$((index + 1))/${#PIPELINE_NAMES[@]}] Running Kotlin serializer pipeline: $pipeline_name"
  "${CMD[@]}"
done

echo "[2/2] Kotlin serializer pipeline completed successfully."
