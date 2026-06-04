#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/spring_login_example"
PIPELINE_SCRIPT="$ROOT_DIR/scripts/run_pipeline.sh"
RESOLVE_AGENT_LIB_SCRIPT="$ROOT_DIR/scripts/resolve_agent_lib.sh"

CONFIG_FILE="$ROOT_DIR/config_spring_login.json"
APP_MAIN="com.example.springlogin.LoginApplication"
AGENT_LIB="${AGENT_LIB:-}"
LOGIN_VALUE="${LOGIN_VALUE:-guest}"
PASSWORD_VALUE="${PASSWORD_VALUE:-bad}"
EXPECTED_LOGIN_RESULT="${EXPECTED_LOGIN_RESULT:-}"
LLM_ONLY=0
OUTPUT_DIR=""
SOCKET_MODE=0
SOCKET_HOST="127.0.0.1"
SOCKET_PORT=9009
SOCKET_TIMEOUT=5

JSON_JAR="$ROOT_DIR/tests_generator/json-20230227.jar"
JUNIT_JAR="$ROOT_DIR/tests_generator/lib/junit-platform-console-standalone-1.9.3.jar"

LOGS_DIR=""
DUMPS_DIR=""
GENERATOR_CLASSES_DIR=""
GEN_TESTS_DIR=""
TEST_CLASSES_DIR=""
AGENT_LOG=""
APP_LOG=""
RESPONSE_FILE=""

print_usage() {
  cat <<USAGE
Usage:
  $0 [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>] [output_dir]

Examples:
  $0
  $0 .artifacts/pipeline_spring_login
  $0 --llm .artifacts/pipeline_spring_login
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
    --*)
      echo "Unknown argument: $1" >&2
      print_usage
      exit 2
      ;;
    *)
      if [[ -n "$OUTPUT_DIR" ]]; then
        echo "Unexpected extra positional argument: $1" >&2
        print_usage
        exit 2
      fi
      OUTPUT_DIR="$1"
      shift
      ;;
  esac
done

if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline_spring_login"
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
GENERATOR_CLASSES_DIR="$OUTPUT_DIR/generator_classes"
GEN_TESTS_DIR="$OUTPUT_DIR/generated_tests"
TEST_CLASSES_DIR="$OUTPUT_DIR/test_classes"
AGENT_LOG="$LOGS_DIR/agent.log"
APP_LOG="$LOGS_DIR/app.log"
RESPONSE_FILE="$LOGS_DIR/login_response.txt"

mkdir -p "$LOGS_DIR" "$DUMPS_DIR" "$GENERATOR_CLASSES_DIR" "$GEN_TESTS_DIR" "$TEST_CLASSES_DIR"

if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "Config file not found: $CONFIG_FILE" >&2
  exit 1
fi
if [[ ! -f "$AGENT_LIB" ]]; then
  echo "Agent library not found: $AGENT_LIB" >&2
  exit 1
fi
if [[ "$LLM_ONLY" -ne 1 ]]; then
  if [[ ! -f "$JSON_JAR" ]]; then
    echo "JSON jar not found: $JSON_JAR" >&2
    exit 1
  fi
  if [[ ! -f "$JUNIT_JAR" ]]; then
    echo "JUnit console jar not found: $JUNIT_JAR" >&2
    exit 1
  fi
fi

echo "[0/4] Building Spring application"
mvn -q -f "$APP_DIR/pom.xml" -DskipTests package
mvn -q -f "$APP_DIR/pom.xml" -DskipTests dependency:build-classpath -Dmdep.outputFile="$APP_DIR/target/dependency.classpath"

APP_CP_FILE="$APP_DIR/target/dependency.classpath"
if [[ ! -f "$APP_CP_FILE" ]]; then
  echo "Dependency classpath file not found: $APP_CP_FILE" >&2
  exit 1
fi

APP_DEPS_CP="$(cat "$APP_CP_FILE")"
APP_RUNTIME_CP="$APP_DIR/target/classes:$APP_DEPS_CP"

TMP_CONFIG_INFO="$OUTPUT_DIR/config_info.txt"
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

REQUEST_BODY="$(python3 - "$LOGIN_VALUE" "$PASSWORD_VALUE" <<'PY'
import json
import sys

print(json.dumps({"login": sys.argv[1], "password": sys.argv[2]}))
PY
)"

echo "[1/4] Starting Spring app with JVMTI agent"
AGENT_OPTIONS="out:$AGENT_LOG"
if [[ "$SOCKET_MODE" -eq 1 ]]; then
  AGENT_OPTIONS="$AGENT_OPTIONS,control_port:$SOCKET_PORT"
else
  AGENT_OPTIONS="$AGENT_OPTIONS,config_file:$EFFECTIVE_CONFIG_FILE"
fi

java \
  "-agentpath:$AGENT_LIB=$AGENT_OPTIONS" \
  -cp "$APP_RUNTIME_CP" "$APP_MAIN" > "$APP_LOG" 2>&1 &
APP_PID=$!

if [[ "$SOCKET_MODE" -eq 1 ]]; then
  echo "Socket mode enabled: $SOCKET_HOST:$SOCKET_PORT"
  echo "Waiting for external runtime config payload..."
  echo "Example:"
  echo "  python3 scripts/send_runtime_config.py --host $SOCKET_HOST --port $SOCKET_PORT --payload-file $EFFECTIVE_CONFIG_FILE"
fi

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
}
trap cleanup EXIT

echo "Waiting for /login endpoint on 127.0.0.1:18080 ..."
READY=0
for _ in {1..60}; do
  if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
    echo "Spring app terminated unexpectedly. Check $APP_LOG" >&2
    exit 1
  fi

  code="$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:18080/login || true)"
  if [[ "$code" == "405" || "$code" == "400" || "$code" == "415" || "$code" == "200" || "$code" == "404" ]]; then
    READY=1
    break
  fi
  sleep 1
done

if [[ "$READY" -ne 1 ]]; then
  echo "Spring app did not become ready in time. Check $APP_LOG" >&2
  exit 1
fi

echo "[2/4] Calling /login"
HTTP_CODE="$(curl -s -o "$RESPONSE_FILE" -w "%{http_code}" \
  -H "Content-Type: application/json" \
  -d "$REQUEST_BODY" \
  http://127.0.0.1:18080/login || true)"
echo "HTTP code: $HTTP_CODE"
RESPONSE_BODY="$(cat "$RESPONSE_FILE" 2>/dev/null || true)"
echo "Response body: $RESPONSE_BODY"

if [[ -n "$EXPECTED_LOGIN_RESULT" ]]; then
  if [[ "$RESPONSE_BODY" != "$EXPECTED_LOGIN_RESULT" ]]; then
    echo "Unexpected /login response body. Expected: $EXPECTED_LOGIN_RESULT, got: $RESPONSE_BODY" >&2
    exit 1
  fi
fi

sleep 1
cleanup
trap - EXIT

if [[ ! -f "$DUMP_PATH" ]]; then
  echo "Expected dump file not found: $DUMP_PATH" >&2
  echo "App log: $APP_LOG" >&2
  exit 1
fi

if [[ "$LLM_ONLY" -eq 1 ]]; then
  echo "[3/4] LLM-only mode: skipping test generation and execution"
  echo "Spring login pipeline completed successfully."
  echo "Artifacts are in: $OUTPUT_DIR"
  exit 0
fi

echo "[3/4] Running test generator"
pushd "$ROOT_DIR" >/dev/null
javac -cp "$JSON_JAR" -d "$GENERATOR_CLASSES_DIR" tests_generator/JsonToJUnitGenerator.java tests_generator/Main.java
java -cp "$GENERATOR_CLASSES_DIR:$JSON_JAR" Main "$DUMP_PATH" "$GEN_TESTS_DIR"
popd >/dev/null

TEST_CLASS="$(python3 - "$DUMP_PATH" <<'PY'
import json
import sys
from pathlib import Path

states = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
class_name = states[0]["class"]
simple_name = class_name.split("/")[-1]
print(f"{simple_name}Test")
PY
)"
TEST_JAVA="$GEN_TESTS_DIR/${TEST_CLASS}.java"

if [[ ! -f "$TEST_JAVA" ]]; then
  echo "Generated test file not found: $TEST_JAVA" >&2
  exit 1
fi

echo "[4/4] Compiling and running generated test: $TEST_CLASS"
javac -cp "$JUNIT_JAR:$APP_RUNTIME_CP:$GEN_TESTS_DIR" -d "$TEST_CLASSES_DIR" "$TEST_JAVA"
java -jar "$JUNIT_JAR" \
  --class-path "$TEST_CLASSES_DIR:$APP_RUNTIME_CP" \
  --select-class "$TEST_CLASS"

echo "Spring login pipeline completed successfully."
echo "Artifacts are in: $OUTPUT_DIR"
