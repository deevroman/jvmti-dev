#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DUMP_PATH=""
LLM_DUMP_PATH=""
OUTPUT_DIR=""
RUNTIME_CLASSPATH=""
TEST_CLASSES_DIR=""
GENERATORS="${TEST_GENERATORS:-algorithmic}"
RUN_ALGORITHMIC_TEST=0
GEMINI_MODEL="${GEMINI_MODEL:-}"
GEMINI_API_KEY_ARG="${GEMINI_API_KEY:-}"

JSON_JAR="$ROOT_DIR/tests_generator/json-20230227.jar"
JUNIT_JAR="$ROOT_DIR/tests_generator/lib/junit-platform-console-standalone-1.9.3.jar"
MOCKITO_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'mockito-core-*.jar' | head -n 1)"
BYTE_BUDDY_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'byte-buddy-*.jar' ! -name 'byte-buddy-agent-*.jar' | head -n 1)"
BYTE_BUDDY_AGENT_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'byte-buddy-agent-*.jar' | head -n 1)"
OBJENESIS_JAR="$(find "$ROOT_DIR/tests_generator/lib" -maxdepth 1 -name 'objenesis-*.jar' | head -n 1)"

print_usage() {
  cat <<USAGE
Usage:
  $0 --dump <dump.json> --output-dir <dir> [--llm-dump <dump.llm.txt>] [--runtime-classpath <cp>] [--test-classes-dir <dir>] [--run-algorithmic-test] [--generators <algorithmic|llm|both|none>]

Environment:
  TEST_GENERATORS  Default: algorithmic. Supported: algorithmic, llm, both, none.
  GEMINI_API_KEY   Required when TEST_GENERATORS=llm or both.
  GEMINI_MODEL     Optional Gemini model for LLM generator.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dump)
      DUMP_PATH="$2"
      shift 2
      ;;
    --llm-dump)
      LLM_DUMP_PATH="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --runtime-classpath)
      RUNTIME_CLASSPATH="$2"
      shift 2
      ;;
    --test-classes-dir)
      TEST_CLASSES_DIR="$2"
      shift 2
      ;;
    --run-algorithmic-test)
      RUN_ALGORITHMIC_TEST=1
      shift
      ;;
    --generators)
      GENERATORS="$2"
      shift 2
      ;;
    --gemini-api-key)
      GEMINI_API_KEY_ARG="$2"
      shift 2
      ;;
    --model)
      GEMINI_MODEL="$2"
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

if [[ -z "$DUMP_PATH" || -z "$OUTPUT_DIR" ]]; then
  print_usage
  exit 2
fi

if [[ "$DUMP_PATH" != /* ]]; then
  DUMP_PATH="$ROOT_DIR/$DUMP_PATH"
fi
if [[ -n "$LLM_DUMP_PATH" && "$LLM_DUMP_PATH" != /* ]]; then
  LLM_DUMP_PATH="$ROOT_DIR/$LLM_DUMP_PATH"
fi
if [[ "$OUTPUT_DIR" != /* ]]; then
  OUTPUT_DIR="$ROOT_DIR/$OUTPUT_DIR"
fi

if [[ ! -f "$DUMP_PATH" ]]; then
  echo "Dump file not found: $DUMP_PATH" >&2
  exit 1
fi
if [[ ! -f "$JSON_JAR" ]]; then
  echo "JSON jar not found: $JSON_JAR" >&2
  exit 1
fi

case "$GENERATORS" in
  algorithmic|llm|both|none) ;;
  *)
    echo "Unsupported --generators value: $GENERATORS" >&2
    print_usage
    exit 2
    ;;
esac

if [[ "$GENERATORS" == "none" ]]; then
  echo "Test generators disabled."
  exit 0
fi

GENERATOR_CLASSES_DIR="$OUTPUT_DIR/generator_classes"
ALG_TESTS_DIR="$OUTPUT_DIR/generated_tests"
LLM_TESTS_DIR="$OUTPUT_DIR/llm_generated_tests"
mkdir -p "$GENERATOR_CLASSES_DIR" "$ALG_TESTS_DIR" "$LLM_TESTS_DIR"

echo "Compiling test generators"
pushd "$ROOT_DIR" >/dev/null
javac -cp "$JSON_JAR" -d "$GENERATOR_CLASSES_DIR" \
  tests_generator/JsonToJUnitGenerator.java \
  tests_generator/Main.java \
  tests_generator/LlmJUnitGenerator.java \
  tests_generator/LlmMain.java
popd >/dev/null

extract_test_class() {
  python3 - "$DUMP_PATH" <<'PY'
import json
import sys
from pathlib import Path

states = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
class_name = states[0].get("class_simple_name") or states[0]["class"].split("/")[-1]
print(f"{class_name}Test")
PY
}

run_algorithmic_generator=0
run_llm_generator=0
if [[ "$GENERATORS" == "algorithmic" || "$GENERATORS" == "both" ]]; then
  run_algorithmic_generator=1
fi
if [[ "$GENERATORS" == "llm" || "$GENERATORS" == "both" ]]; then
  run_llm_generator=1
fi

if [[ "$run_algorithmic_generator" -eq 1 ]]; then
  echo "Running algorithmic test generator"
  java -cp "$GENERATOR_CLASSES_DIR:$JSON_JAR" Main "$DUMP_PATH" "$ALG_TESTS_DIR"
  echo "Algorithmic generated tests: $ALG_TESTS_DIR"
fi

if [[ "$run_llm_generator" -eq 1 ]]; then
  if [[ -z "$GEMINI_API_KEY_ARG" ]]; then
    echo "GEMINI_API_KEY is required for LLM test generation" >&2
    exit 1
  fi

  LLM_ARGS=(--input "$DUMP_PATH" --output-dir "$LLM_TESTS_DIR" --gemini-api-key "$GEMINI_API_KEY_ARG")
  if [[ -n "$LLM_DUMP_PATH" && -f "$LLM_DUMP_PATH" ]]; then
    LLM_ARGS+=(--llm-dump "$LLM_DUMP_PATH")
  fi
  if [[ -n "$GEMINI_MODEL" ]]; then
    LLM_ARGS+=(--model "$GEMINI_MODEL")
  fi

  echo "Running LLM test generator"
  java -cp "$GENERATOR_CLASSES_DIR:$JSON_JAR" LlmMain "${LLM_ARGS[@]}"
  echo "LLM generated tests: $LLM_TESTS_DIR"
fi

if [[ "$RUN_ALGORITHMIC_TEST" -eq 1 ]]; then
  if [[ "$run_algorithmic_generator" -ne 1 ]]; then
    echo "--run-algorithmic-test requires algorithmic generator" >&2
    exit 2
  fi
  if [[ -z "$RUNTIME_CLASSPATH" ]]; then
    echo "--runtime-classpath is required when --run-algorithmic-test is used" >&2
    exit 2
  fi
  if [[ -z "$TEST_CLASSES_DIR" ]]; then
    TEST_CLASSES_DIR="$OUTPUT_DIR/test_classes"
  elif [[ "$TEST_CLASSES_DIR" != /* ]]; then
    TEST_CLASSES_DIR="$ROOT_DIR/$TEST_CLASSES_DIR"
  fi
  if [[ ! -f "$JUNIT_JAR" ]]; then
    echo "JUnit console jar not found: $JUNIT_JAR" >&2
    exit 1
  fi

  TEST_CLASS="$(extract_test_class)"
  TEST_JAVA="$ALG_TESTS_DIR/${TEST_CLASS}.java"
  if [[ ! -f "$TEST_JAVA" ]]; then
    echo "Generated test file not found: $TEST_JAVA" >&2
    exit 1
  fi

  USES_MOCKITO=0
  if rg -q "org\\.mockito\\." "$TEST_JAVA"; then
    USES_MOCKITO=1
  fi

  MOCKITO_CP=""
  MOCKITO_EXT_CP=""
  if [[ "$USES_MOCKITO" -eq 1 ]]; then
    if [[ -z "$MOCKITO_JAR" || ! -f "$MOCKITO_JAR" || -z "$BYTE_BUDDY_JAR" || ! -f "$BYTE_BUDDY_JAR" || -z "$BYTE_BUDDY_AGENT_JAR" || ! -f "$BYTE_BUDDY_AGENT_JAR" || -z "$OBJENESIS_JAR" || ! -f "$OBJENESIS_JAR" ]]; then
      echo "Generated test uses Mockito, but required runtime jars are missing in tests_generator/lib" >&2
      exit 1
    fi

    MOCKITO_CP="$MOCKITO_JAR:$BYTE_BUDDY_JAR:$BYTE_BUDDY_AGENT_JAR:$OBJENESIS_JAR"
    MOCKITO_EXT_CP="$OUTPUT_DIR/mockito_ext"
    mkdir -p "$MOCKITO_EXT_CP/mockito-extensions"
    printf "mock-maker-subclass\n" > "$MOCKITO_EXT_CP/mockito-extensions/org.mockito.plugins.MockMaker"
  fi

  mkdir -p "$TEST_CLASSES_DIR"
  TEST_COMPILE_CP="$JUNIT_JAR:$RUNTIME_CLASSPATH:$ALG_TESTS_DIR"
  TEST_RUNTIME_CP="$TEST_CLASSES_DIR:$RUNTIME_CLASSPATH"
  if [[ -n "$MOCKITO_CP" ]]; then
    TEST_COMPILE_CP="$TEST_COMPILE_CP:$MOCKITO_CP"
    TEST_RUNTIME_CP="$TEST_RUNTIME_CP:$MOCKITO_CP"
  fi
  if [[ -n "$MOCKITO_EXT_CP" ]]; then
    TEST_RUNTIME_CP="$TEST_RUNTIME_CP:$MOCKITO_EXT_CP"
  fi

  echo "Compiling and running algorithmic generated test: $TEST_CLASS"
  javac -cp "$TEST_COMPILE_CP" -d "$TEST_CLASSES_DIR" "$TEST_JAVA"
  java -jar "$JUNIT_JAR" \
    --class-path "$TEST_RUNTIME_CP" \
    --select-class "$TEST_CLASS"
fi
