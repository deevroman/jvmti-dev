#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESOLVE_AGENT_LIB_SCRIPT="$ROOT_DIR/scripts/resolve_agent_lib.sh"

KLAW_DIR="$ROOT_DIR/jar_example/klaw_compareutils/klaw"
KLAW_JAR="$KLAW_DIR/core/target/klaw-2.10.4.jar"
CONFIG_FILE="$ROOT_DIR/config_klaw_add_new_team.json"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_klaw_add_new_team"
AGENT_LIB="${AGENT_LIB:-}"
PORT="${KLAW_PORT:-9097}"
SUPERADMIN_PASSWORD="${KLAW_SUPERADMIN_PASSWORD:-superadmin}"
CORAL_ENABLED="${KLAW_CORAL_ENABLED:-false}"
WAIT_TIMEOUT_SECONDS="${WAIT_TIMEOUT_SECONDS:-1800}"
OPEN_BROWSER=1
BUILD_APP=1

print_usage() {
  cat <<USAGE
Usage:
  $0 [--port <port>] [--password <password>] [--wait-timeout <sec>] [--no-browser] [--no-build] [output_dir]

Starts the real Klaw Core application with the JVMTI dumper attached to:
  io.aiven.klaw.service.UsersTeamsControllerService.addNewTeam(TeamModel, boolean)

The script opens Klaw in a browser and waits until you manually trigger addNewTeam from the UI.

Environment:
  AGENT_LIB                 Optional explicit path to libdumper
  KLAW_PORT                 Default: 9097
  KLAW_SUPERADMIN_PASSWORD  Default: superadmin
  KLAW_CORAL_ENABLED        Default: false
  WAIT_TIMEOUT_SECONDS      Default: 1800
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    --password)
      SUPERADMIN_PASSWORD="$2"
      shift 2
      ;;
    --wait-timeout)
      WAIT_TIMEOUT_SECONDS="$2"
      shift 2
      ;;
    --no-browser)
      OPEN_BROWSER=0
      shift
      ;;
    --no-build)
      BUILD_APP=0
      shift
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
      if [[ "$OUTPUT_DIR" != "$ROOT_DIR/.artifacts/pipeline_klaw_add_new_team" ]]; then
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

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || ((PORT <= 0 || PORT > 65535)); then
  echo "Invalid --port value: $PORT" >&2
  exit 2
fi

if ! [[ "$WAIT_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]]; then
  echo "Invalid --wait-timeout value: $WAIT_TIMEOUT_SECONDS" >&2
  exit 2
fi

if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "Config file not found: $CONFIG_FILE" >&2
  exit 1
fi
if [[ ! -f "$AGENT_LIB" ]]; then
  echo "Agent library not found: $AGENT_LIB" >&2
  exit 1
fi

LOGS_DIR="$OUTPUT_DIR/logs"
DUMPS_DIR="$OUTPUT_DIR/dumps"
DATA_DIR="$OUTPUT_DIR/data"
AGENT_LOG="$LOGS_DIR/agent.log"
APP_LOG="$LOGS_DIR/app.log"
TMP_CONFIG_INFO="$OUTPUT_DIR/config_info.txt"
PID_FILE="$OUTPUT_DIR/klaw.pid"

mkdir -p "$LOGS_DIR" "$DUMPS_DIR" "$DATA_DIR"

if [[ "$BUILD_APP" -eq 1 || ! -f "$KLAW_JAR" ]]; then
  echo "[0/4] Building Klaw Core"
  mvn -q -f "$KLAW_DIR/core/pom.xml" -DskipTests package
fi

if [[ ! -f "$KLAW_JAR" ]]; then
  echo "Klaw jar not found after build: $KLAW_JAR" >&2
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
else:
    llm_dump_path = dump_path.with_suffix(".llm.txt")

llm_dump_path.parent.mkdir(parents=True, exist_ok=True)
cfg["llm_dump"] = str(llm_dump_path)
if "llm_dump_path" in cfg:
    cfg["llm_dump_path"] = str(llm_dump_path)
if "dump_llm" in cfg:
    cfg["dump_llm"] = str(llm_dump_path)

effective_cfg = out_dir / "config.effective.json"
effective_cfg.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

print(effective_cfg)
print(dump_path)
print(llm_dump_path)
PY

EFFECTIVE_CONFIG_FILE="$(sed -n '1p' "$TMP_CONFIG_INFO")"
DUMP_PATH="$(sed -n '2p' "$TMP_CONFIG_INFO")"
LLM_DUMP_PATH="$(sed -n '3p' "$TMP_CONFIG_INFO")"

if [[ -z "$EFFECTIVE_CONFIG_FILE" || -z "$DUMP_PATH" || -z "$LLM_DUMP_PATH" ]]; then
  echo "Failed to prepare effective config" >&2
  exit 1
fi

rm -f "$DUMP_PATH" "$LLM_DUMP_PATH"

DB_PATH="$DATA_DIR/klawprodb"
JDBC_URL="jdbc:h2:file:$DB_PATH;DB_CLOSE_ON_EXIT=FALSE;DB_CLOSE_DELAY=-1;MODE=MySQL;CASE_INSENSITIVE_IDENTIFIERS=TRUE;"
AGENT_OPTIONS="out:$AGENT_LOG,config_file:$EFFECTIVE_CONFIG_FILE"

echo "[1/4] Starting Klaw Core with JVMTI agent"
echo "URL: http://localhost:$PORT/"
echo "Login: superadmin"
echo "Password: $SUPERADMIN_PASSWORD"
echo "Dump JSON: $DUMP_PATH"
echo "Dump LLM: $LLM_DUMP_PATH"
echo "Logs: $LOGS_DIR"
echo

(
  cd "$KLAW_DIR"
  exec java \
    "-agentpath:$AGENT_LIB=$AGENT_OPTIONS" \
    "-Dserver.port=$PORT" \
    "-Dspring.datasource.url=$JDBC_URL" \
    "-Dklaw.superadmin.default.password=$SUPERADMIN_PASSWORD" \
    "-Dklaw.coral.enabled=$CORAL_ENABLED" \
    -jar "$KLAW_JAR" > "$APP_LOG" 2>&1
) &
APP_PID=$!
echo "$APP_PID" > "$PID_FILE"

cleanup() {
  if kill -0 "$APP_PID" >/dev/null 2>&1; then
    kill -TERM "$APP_PID" >/dev/null 2>&1 || true
    for _ in {1..150}; do
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
  fi
  wait "$APP_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "[2/4] Waiting for Klaw on http://localhost:$PORT/login"
READY=0
for _ in {1..120}; do
  if curl -fsS "http://localhost:$PORT/login" >/dev/null 2>&1; then
    READY=1
    break
  fi
  if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
    echo "Klaw process exited before becoming ready. Check: $APP_LOG" >&2
    tail -n 160 "$APP_LOG" >&2 || true
    exit 1
  fi
  sleep 1
done

if [[ "$READY" -ne 1 ]]; then
  echo "Timed out waiting for Klaw. Check: $APP_LOG" >&2
  exit 1
fi

echo "[3/4] Klaw is ready"
if [[ "$OPEN_BROWSER" -eq 1 ]]; then
  if command -v open >/dev/null 2>&1; then
    open "http://localhost:$PORT/"
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "http://localhost:$PORT/" >/dev/null 2>&1 || true
  else
    echo "No browser opener found. Open manually: http://localhost:$PORT/"
  fi
else
  echo "Browser opening disabled. Open manually: http://localhost:$PORT/"
fi

echo
echo "Trigger addNewTeam manually in the UI."
echo "The script will stop Klaw after dump is written or after ${WAIT_TIMEOUT_SECONDS}s."
echo

echo "[4/4] Waiting for dump"
for ((i = 1; i <= WAIT_TIMEOUT_SECONDS; i++)); do
  if [[ -f "$DUMP_PATH" && "$(wc -c < "$DUMP_PATH")" -gt 2 ]] && python3 - "$DUMP_PATH" <<'PY'
import json
import sys
from pathlib import Path

try:
    data = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
except Exception:
    raise SystemExit(1)

if isinstance(data, list) and any(item.get("state_type") == "method_exit" for item in data if isinstance(item, dict)):
    raise SystemExit(0)
raise SystemExit(1)
PY
  then
    echo "Dump captured."
    echo "Dump JSON: $DUMP_PATH"
    echo "Dump LLM: $LLM_DUMP_PATH"
    echo "Agent log: $AGENT_LOG"
    echo "App log: $APP_LOG"
    exit 0
  fi
  if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
    echo "Klaw process exited before dump was captured. Check: $APP_LOG and $AGENT_LOG" >&2
    exit 1
  fi
  sleep 1
done

echo "Timed out waiting for dump. Klaw will be stopped. Check: $APP_LOG and $AGENT_LOG" >&2
exit 1
