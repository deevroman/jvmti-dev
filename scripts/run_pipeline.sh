#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESOLVE_AGENT_LIB_SCRIPT="$ROOT_DIR/scripts/resolve_agent_lib.sh"

CONFIG_FILE=""
APP_MAIN=""
APP_CLASSPATH="$ROOT_DIR/java_examples"
AGENT_LIB="${AGENT_LIB:-}"
OUTPUT_DIR="$ROOT_DIR/.artifacts/pipeline"
AGENT_LOG=""
LLM_ONLY=0
SOCKET_MODE=0
SOCKET_HOST="127.0.0.1"
SOCKET_PORT=9009
SOCKET_TIMEOUT=5

JSON_JAR="$ROOT_DIR/tests_generator/json-20230227.jar"
JUNIT_JAR="$ROOT_DIR/tests_generator/lib/junit-platform-console-standalone-1.9.3.jar"
MOCKITO_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'mockito-core-*.jar' | head -n 1)"
BYTE_BUDDY_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'byte-buddy-*.jar' ! -name 'byte-buddy-agent-*.jar' | head -n 1)"
BYTE_BUDDY_AGENT_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'byte-buddy-agent-*.jar' | head -n 1)"
OBJENESIS_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'objenesis-*.jar' | head -n 1)"

print_usage() {
  cat <<USAGE
Usage:
  $0 --config-file <path> --app-main <MainClass> [--app-classpath <path>] [--agent-lib <path>] [--output-dir <path>] [--agent-log <path>] [--llm] [--socket] [--socket-host <host>] [--socket-port <port>] [--socket-timeout <sec>]

Example:
  $0 --config-file $ROOT_DIR/config.json --app-main Calculator --app-classpath $ROOT_DIR/java_examples --output-dir $ROOT_DIR/.artifacts/pipeline
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --config-file)
      CONFIG_FILE="$2"
      shift 2
      ;;
    --app-main)
      APP_MAIN="$2"
      shift 2
      ;;
    --app-classpath)
      APP_CLASSPATH="$2"
      shift 2
      ;;
    --agent-lib)
      AGENT_LIB="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
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

if [[ -z "$CONFIG_FILE" || -z "$APP_MAIN" ]]; then
  print_usage
  exit 2
fi

if [[ -z "$AGENT_LIB" ]]; then
  AGENT_LIB="$("$RESOLVE_AGENT_LIB_SCRIPT")"
fi

if [[ "$OUTPUT_DIR" != /* ]]; then
  OUTPUT_DIR="$ROOT_DIR/$OUTPUT_DIR"
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
APP_CLASSES_DIR="$OUTPUT_DIR/app_classes"
GENERATOR_CLASSES_DIR="$OUTPUT_DIR/generator_classes"
GEN_TESTS_DIR="$OUTPUT_DIR/generated_tests"
TEST_CLASSES_DIR="$OUTPUT_DIR/test_classes"
MOCKITO_EXT_DIR="$OUTPUT_DIR/mockito_ext"
APP_LOG="$LOGS_DIR/app.stdout.log"

if [[ -z "$AGENT_LOG" ]]; then
  AGENT_LOG="$LOGS_DIR/agent.log"
fi
if [[ "$AGENT_LOG" != /* ]]; then
  AGENT_LOG="$ROOT_DIR/$AGENT_LOG"
fi

mkdir -p "$LOGS_DIR" "$DUMPS_DIR" "$APP_CLASSES_DIR" "$GENERATOR_CLASSES_DIR" "$GEN_TESTS_DIR" "$TEST_CLASSES_DIR"

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
  echo "Failed to prepare effective runtime config" >&2
  exit 1
fi

RUNTIME_CLASSPATH="$APP_CLASSES_DIR:$APP_CLASSPATH"

if [[ "$SOCKET_MODE" -eq 1 ]]; then
  echo "[1/3] Running JVM with JVMTI agent in socket mode (payload source: $CONFIG_FILE)"
else
  echo "[1/3] Running JVM with JVMTI agent using config file: $CONFIG_FILE"
fi
echo "Artifacts directory: $OUTPUT_DIR"

APP_SOURCE="$APP_CLASSPATH/$APP_MAIN.java"
if [[ -f "$APP_SOURCE" ]]; then
  echo "Compiling application sources with debug info into: $APP_CLASSES_DIR"
  find "$APP_CLASSPATH" -name "*.java" ! -name "*Test.java" -print0 | \
    xargs -0 javac -g -d "$APP_CLASSES_DIR" -cp "$APP_CLASSPATH"
fi

AGENT_OPTIONS="out:$AGENT_LOG"
if [[ "$SOCKET_MODE" -eq 1 ]]; then
  AGENT_OPTIONS="$AGENT_OPTIONS,control_port:$SOCKET_PORT"
else
  AGENT_OPTIONS="$AGENT_OPTIONS,config_file:$EFFECTIVE_CONFIG_FILE"
fi

if [[ "$SOCKET_MODE" -eq 1 ]]; then
  echo "Socket mode enabled: $SOCKET_HOST:$SOCKET_PORT"
  echo "Waiting for external runtime config payload..."
  echo "Example:"
  echo "  python3 scripts/send_runtime_config.py --host $SOCKET_HOST --port $SOCKET_PORT --payload-file $EFFECTIVE_CONFIG_FILE"
  java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+PrintInlining \
    "-agentpath:$AGENT_LIB=$AGENT_OPTIONS" \
    -cp "$RUNTIME_CLASSPATH" "$APP_MAIN" > "$APP_LOG" 2>&1 &
  APP_PID=$!

  cleanup_app() {
    if kill -0 "$APP_PID" >/dev/null 2>&1; then
      kill -TERM "$APP_PID" >/dev/null 2>&1 || true
      for _ in {1..50}; do
        if ! kill -0 "$APP_PID" >/dev/null 2>&1; then
          break
        fi
        sleep 0.2
      done
      if kill -0 "$APP_PID" >/dev/null 2>&1; then
        kill -9 "$APP_PID" >/dev/null 2>&1 || true
      fi
    fi
  }

  trap cleanup_app EXIT

  if ! wait "$APP_PID"; then
    echo "Application exited with non-zero status in socket mode. Check $APP_LOG" >&2
    exit 1
  fi
  trap - EXIT
else
  java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+PrintInlining \
    "-agentpath:$AGENT_LIB=$AGENT_OPTIONS" \
    -cp "$RUNTIME_CLASSPATH" "$APP_MAIN" > "$APP_LOG" 2>&1
fi

if [[ -f "$APP_LOG" ]]; then
  echo "Application stdout log: $APP_LOG"
fi

if [[ ! -f "$DUMP_PATH" ]]; then
  echo "Expected dump file not found: $DUMP_PATH" >&2
  exit 1
fi

if [[ "$LLM_ONLY" -eq 1 ]]; then
  echo "[2/3] LLM-only mode: skipping test generation and execution"
  echo "Pipeline completed successfully."
  echo "Artifacts are in: $OUTPUT_DIR"
  exit 0
fi

echo "[2/3] Running test generator"
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

echo "[3/3] Compiling and running generated test: $TEST_CLASS"
USES_MOCKITO=0
if rg -q "org\\.mockito\\." "$TEST_JAVA"; then
  USES_MOCKITO=1
fi

MOCKITO_CP=""
MOCKITO_EXT_CP=""
if [[ "$USES_MOCKITO" -eq 1 ]]; then
  if [[ -z "$MOCKITO_JAR" || ! -f "$MOCKITO_JAR" ]]; then
    echo "Generated test uses Mockito, but mockito-core jar was not found in tests_generator/lib" >&2
    exit 1
  fi

  if [[ -z "$BYTE_BUDDY_JAR" || ! -f "$BYTE_BUDDY_JAR" || -z "$BYTE_BUDDY_AGENT_JAR" || ! -f "$BYTE_BUDDY_AGENT_JAR" || -z "$OBJENESIS_JAR" || ! -f "$OBJENESIS_JAR" ]]; then
    echo "Generated test uses Mockito, but required runtime jars are missing." >&2
    echo "Expected in tests_generator/lib:" >&2
    echo "  - byte-buddy-*.jar" >&2
    echo "  - byte-buddy-agent-*.jar" >&2
    echo "  - objenesis-*.jar" >&2
    exit 1
  fi

  MOCKITO_CP="$MOCKITO_JAR:$BYTE_BUDDY_JAR:$BYTE_BUDDY_AGENT_JAR:$OBJENESIS_JAR"

  mkdir -p "$MOCKITO_EXT_DIR/mockito-extensions"
  printf "mock-maker-subclass\n" > "$MOCKITO_EXT_DIR/mockito-extensions/org.mockito.plugins.MockMaker"
  MOCKITO_EXT_CP="$MOCKITO_EXT_DIR"
fi

TEST_COMPILE_CP="$JUNIT_JAR:$RUNTIME_CLASSPATH:$GEN_TESTS_DIR"
TEST_RUNTIME_CP="$TEST_CLASSES_DIR:$RUNTIME_CLASSPATH"
if [[ -n "$MOCKITO_CP" ]]; then
  TEST_COMPILE_CP="$TEST_COMPILE_CP:$MOCKITO_CP"
  TEST_RUNTIME_CP="$TEST_RUNTIME_CP:$MOCKITO_CP"
fi
if [[ -n "$MOCKITO_EXT_CP" ]]; then
  TEST_RUNTIME_CP="$TEST_RUNTIME_CP:$MOCKITO_EXT_CP"
fi

javac -cp "$TEST_COMPILE_CP" -d "$TEST_CLASSES_DIR" "$TEST_JAVA"
java -jar "$JUNIT_JAR" \
  --class-path "$TEST_RUNTIME_CP" \
  --select-class "$TEST_CLASS"

echo "Pipeline completed successfully."
echo "Artifacts are in: $OUTPUT_DIR"
