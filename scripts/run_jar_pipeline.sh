#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_pipeline.sh"

CONFIG_FILE=""
RUNNER_SOURCE=""
RUNNER_MAIN=""
JAR_FILE=""
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_jar"
AGENT_LIB="${AGENT_LIB:-}"
AGENT_LOG=""
LLM_ONLY=0
SOCKET_MODE=0
SOCKET_HOST=""
SOCKET_PORT=""
SOCKET_TIMEOUT=""

print_usage() {
  cat <<USAGE
Usage:
  $0 --config-file <path> --runner-source <path> --runner-main <MainClass> --jar-file <path> [--output-dir <path>] [--agent-lib <path>] [--agent-log <path>] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]

Example:
  $0 --config-file $ROOT_DIR/config_josm_program_arguments.json --runner-source $ROOT_DIR/jar_example/josm/JosmProgramArgumentsRunner.java --runner-main JosmProgramArgumentsRunner --jar-file $ROOT_DIR/jar_example/josm/josm.jar --output-dir $ROOT_DIR/.artifacts/pipeline_josm_program_arguments
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config-file)
      CONFIG_FILE="$2"
      shift 2
      ;;
    --runner-source)
      RUNNER_SOURCE="$2"
      shift 2
      ;;
    --runner-main)
      RUNNER_MAIN="$2"
      shift 2
      ;;
    --jar-file)
      JAR_FILE="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --agent-lib)
      AGENT_LIB="$2"
      shift 2
      ;;
    --agent-log)
      AGENT_LOG="$2"
      shift 2
      ;;
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
      echo "Unknown argument: $1" >&2
      print_usage
      exit 2
      ;;
  esac
done

if [[ -z "$CONFIG_FILE" || -z "$RUNNER_SOURCE" || -z "$RUNNER_MAIN" || -z "$JAR_FILE" ]]; then
  print_usage
  exit 2
fi

if [[ ! -f "$PIPELINE_SCRIPT" ]]; then
  echo "Pipeline script not found: $PIPELINE_SCRIPT" >&2
  exit 1
fi
if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "Config file not found: $CONFIG_FILE" >&2
  exit 1
fi
if [[ ! -f "$RUNNER_SOURCE" ]]; then
  echo "Runner source not found: $RUNNER_SOURCE" >&2
  exit 1
fi
if [[ ! -f "$JAR_FILE" ]]; then
  echo "Jar file not found: $JAR_FILE" >&2
  exit 1
fi

if [[ "$OUTPUT_DIR" != /* ]]; then
  OUTPUT_DIR="$ROOT_DIR/$OUTPUT_DIR"
fi

APP_CLASSES_DIR="$OUTPUT_DIR/app_classes"
mkdir -p "$APP_CLASSES_DIR"

echo "[0/2] Compiling jar runner with debug info"
javac -g -cp "$JAR_FILE" -d "$APP_CLASSES_DIR" "$RUNNER_SOURCE"

echo "[1/2] Running generic pipeline against jar classpath"
PIPELINE_CMD=(
  "$PIPELINE_SCRIPT"
  --config-file "$CONFIG_FILE"
  --app-main "$RUNNER_MAIN"
  --app-classpath "$JAR_FILE"
  --output-dir "$OUTPUT_DIR"
)
if [[ -n "$AGENT_LIB" ]]; then
  PIPELINE_CMD+=(--agent-lib "$AGENT_LIB")
fi
if [[ -n "$AGENT_LOG" ]]; then
  PIPELINE_CMD+=(--agent-log "$AGENT_LOG")
fi
if [[ "$LLM_ONLY" -eq 1 ]]; then
  PIPELINE_CMD+=(--llm)
fi
if [[ "$SOCKET_MODE" -eq 1 ]]; then
  PIPELINE_CMD+=(--socket)
fi
if [[ -n "$SOCKET_HOST" ]]; then
  PIPELINE_CMD+=(--socket-host "$SOCKET_HOST")
fi
if [[ -n "$SOCKET_PORT" ]]; then
  PIPELINE_CMD+=(--socket-port "$SOCKET_PORT")
fi
if [[ -n "$SOCKET_TIMEOUT" ]]; then
  PIPELINE_CMD+=(--socket-timeout "$SOCKET_TIMEOUT")
fi

"${PIPELINE_CMD[@]}"

echo "[2/2] Jar pipeline completed successfully."
