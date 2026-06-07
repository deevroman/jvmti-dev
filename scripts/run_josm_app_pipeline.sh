#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESOLVE_AGENT_LIB_SCRIPT="$ROOT_DIR/scripts/resolve_agent_lib.sh"

CONFIG_FILE="$ROOT_DIR/config_josm_program_arguments.json"
JAR_FILE="$ROOT_DIR/jar_example/josm/josm.jar"
APP_MAIN="org.openstreetmap.josm.gui.MainApplication"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_josm_app_program_arguments"
AGENT_LIB="${AGENT_LIB:-}"
JOSM_PREF="${JOSM_PREF:-/tmp/josm/settings}"
JOSM_HOME="${JOSM_HOME:-/tmp/josm}"
JOSM_ARGS_RAW="${JOSM_ARGS:---debug --offline=ALL}"
read -r -a JOSM_ARGS_ARRAY <<< "$JOSM_ARGS_RAW"
WAIT_SECONDS="${WAIT_SECONDS:-30}"
SOCKET_MODE=0
SOCKET_HOST="127.0.0.1"
SOCKET_PORT=9009
SOCKET_TIMEOUT=5

print_usage() {
  cat <<USAGE
Usage:
  $0 [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>] [output_dir]

Environment:
  JOSM_ARGS      Arguments passed to the real JOSM application. Default: --debug --offline=ALL
  JOSM_PREF      JOSM preferences directory. Default: /tmp/josm/settings
  JOSM_HOME      JOSM home directory. Default: /tmp/josm
  WAIT_SECONDS   How long to keep JOSM running before terminating it. Default: 30
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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
    --*)
      echo "Unknown argument: $1" >&2
      print_usage
      exit 2
      ;;
    *)
      if [[ "$OUTPUT_DIR" != "$ROOT_DIR/.artifacts/pipeline_josm_app_program_arguments" ]]; then
        echo "Unexpected extra positional argument: $1" >&2
        print_usage
        exit 2
      fi
      OUTPUT_DIR="$1"
      shift
      ;;
  esac
done

if [[ "$OUTPUT_DIR" != /* ]]; then
  OUTPUT_DIR="$ROOT_DIR/$OUTPUT_DIR"
fi

if [[ -z "$AGENT_LIB" ]]; then
  AGENT_LIB="$("$RESOLVE_AGENT_LIB_SCRIPT")"
fi

if ! [[ "$SOCKET_PORT" =~ ^[0-9]+$ ]] || ((SOCKET_PORT <= 0 || SOCKET_PORT > 65535)); then
  echo "Invalid --socket-port value: $SOCKET_PORT" >&2
  exit 2
fi

if ! [[ "$SOCKET_TIMEOUT" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "Invalid --socket-timeout value: $SOCKET_TIMEOUT" >&2
  exit 2
fi

LOGS_DIR="$OUTPUT_DIR/logs"
DUMPS_DIR="$OUTPUT_DIR/dumps"
AGENT_LOG="$LOGS_DIR/agent.log"
APP_LOG="$LOGS_DIR/app.log"
TMP_CONFIG_INFO="$OUTPUT_DIR/config_info.txt"

mkdir -p "$LOGS_DIR" "$DUMPS_DIR"

if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "Config file not found: $CONFIG_FILE" >&2
  exit 1
fi
if [[ ! -f "$JAR_FILE" ]]; then
  echo "Jar file not found: $JAR_FILE" >&2
  exit 1
fi
if [[ ! -f "$AGENT_LIB" ]]; then
  echo "Agent library not found: $AGENT_LIB" >&2
  exit 1
fi

python3 - "$CONFIG_FILE" "$OUTPUT_DIR" > "$TMP_CONFIG_INFO" <<'PY'
import json
import sys
from pathlib import Path

cfg_path = Path(sys.argv[1]).resolve()
out_dir = Path(sys.argv[2]).resolve()
dumps_dir = out_dir / "dumps"
dumps_dir.mkdir(parents=True, exist_ok=True)

cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
if not isinstance(cfg, dict):
    raise SystemExit("Config must be a JSON object")

raw_dump = cfg.get("dump") or cfg.get("dump_path") or "dump.json"
dump_path = Path(raw_dump)
if not dump_path.is_absolute():
    dump_path = (dumps_dir / dump_path).resolve()
dump_path.parent.mkdir(parents=True, exist_ok=True)
cfg["dump"] = str(dump_path)
if "dump_path" in cfg:
    cfg["dump_path"] = str(dump_path)

raw_llm_dump = cfg.get("llm_dump") or cfg.get("llm_dump_path") or cfg.get("dump_llm")
if raw_llm_dump:
    llm_dump_path = Path(raw_llm_dump)
    if not llm_dump_path.is_absolute():
        llm_dump_path = (dumps_dir / llm_dump_path).resolve()
    llm_dump_path.parent.mkdir(parents=True, exist_ok=True)
    cfg["llm_dump"] = str(llm_dump_path)
    if "llm_dump_path" in cfg:
        cfg["llm_dump_path"] = str(llm_dump_path)
    if "dump_llm" in cfg:
        cfg["dump_llm"] = str(llm_dump_path)

effective_cfg = out_dir / "config.effective.json"
effective_cfg.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")

print(effective_cfg)
print(dump_path)
PY

EFFECTIVE_CONFIG_FILE="$(sed -n '1p' "$TMP_CONFIG_INFO")"
DUMP_PATH="$(sed -n '2p' "$TMP_CONFIG_INFO")"

if [[ -z "$EFFECTIVE_CONFIG_FILE" || -z "$DUMP_PATH" ]]; then
  echo "Failed to prepare effective config" >&2
  exit 1
fi

echo "[1/2] Running full JOSM application with JVMTI agent"
echo "Main class: $APP_MAIN"
echo "Classpath jar: $JAR_FILE"
echo "JOSM prefs: $JOSM_PREF"
echo "JOSM home: $JOSM_HOME"
echo "Args: ${JOSM_ARGS_ARRAY[*]}"
echo "Artifacts directory: $OUTPUT_DIR"
if [[ "$SOCKET_MODE" -eq 1 ]]; then
  echo "Socket mode enabled: $SOCKET_HOST:$SOCKET_PORT"
  echo "Waiting for external runtime config payload..."
  echo "Example:"
  echo "  python3 scripts/send_runtime_config.py --host $SOCKET_HOST --port $SOCKET_PORT --payload-file $EFFECTIVE_CONFIG_FILE"
fi

AGENT_OPTIONS="out:$AGENT_LOG"
if [[ "$SOCKET_MODE" -eq 1 ]]; then
  AGENT_OPTIONS="$AGENT_OPTIONS,control_port:$SOCKET_PORT"
else
  AGENT_OPTIONS="$AGENT_OPTIONS,config_file:$EFFECTIVE_CONFIG_FILE"
fi

mkdir -p "$JOSM_HOME" "$JOSM_PREF"

(
  java \
    "-Djosm.pref=$JOSM_PREF" \
    "-Djosm.home=$JOSM_HOME" \
    "-agentpath:$AGENT_LIB=$AGENT_OPTIONS" \
    -cp "$JAR_FILE" "$APP_MAIN" "${JOSM_ARGS_ARRAY[@]}" > "$APP_LOG" 2>&1 || true
) 2>/dev/null &
APP_PID=$!

cleanup() {
  if kill -0 "$APP_PID" >/dev/null 2>&1; then
    kill -TERM "$APP_PID" >/dev/null 2>&1 || true
    for _ in {1..100}; do
      if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
        break
      fi
      sleep 0.2
    done
    if kill -0 "$APP_PID" >/dev/null 2>&1; then
      kill -INT "$APP_PID" >/dev/null 2>&1 || true
      for _ in {1..50}; do
        if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
          break
        fi
        sleep 0.2
      done
    fi
    if kill -0 "$APP_PID" >/dev/null 2>&1; then
      kill -9 "$APP_PID" >/dev/null 2>&1 || true
    fi
    wait "$APP_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

for ((i = 1; i <= WAIT_SECONDS; i++)); do
  if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
    wait "$APP_PID" 2>/dev/null || true
    break
  fi
  sleep 1
done

cleanup
trap - EXIT

if [[ ! -f "$DUMP_PATH" ]]; then
  echo "Expected dump file not found: $DUMP_PATH" >&2
  echo "Check logs: $APP_LOG and $AGENT_LOG" >&2
  exit 1
fi

if [[ "$(wc -c < "$DUMP_PATH")" -le 2 ]]; then
  echo "Dump file is empty: $DUMP_PATH" >&2
  echo "Check logs: $APP_LOG and $AGENT_LOG" >&2
  exit 1
fi

echo "[2/2] Dump generation completed successfully"
echo "Dump JSON: $DUMP_PATH"
echo "Likely LLM dump: ${DUMP_PATH%.json}.llm.txt"
echo "Artifacts are in: $OUTPUT_DIR"
