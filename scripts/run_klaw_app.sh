#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KLAW_DIR="$ROOT_DIR/jar_example/klaw_compareutils/klaw"
JAR_PATH="$KLAW_DIR/core/target/klaw-2.10.4.jar"
PORT="${KLAW_PORT:-9097}"
DB_PATH="${KLAW_DB_PATH:-$ROOT_DIR/.artifacts/klaw_app/data/klawprodb}"
SUPERADMIN_PASSWORD="${KLAW_SUPERADMIN_PASSWORD:-superadmin}"
CORAL_ENABLED="${KLAW_CORAL_ENABLED:-false}"
BUILD_IF_MISSING=1

print_usage() {
  cat <<USAGE
Usage:
  $0 [--port <port>] [--db-path <path>] [--password <password>] [--coral] [--no-build]

Environment:
  KLAW_PORT                 Default: 9097
  KLAW_DB_PATH              Default: .artifacts/klaw_app/data/klawprodb
  KLAW_SUPERADMIN_PASSWORD  Default: superadmin
  KLAW_CORAL_ENABLED        Default: false
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    --db-path)
      DB_PATH="$2"
      shift 2
      ;;
    --password)
      SUPERADMIN_PASSWORD="$2"
      shift 2
      ;;
    --coral)
      CORAL_ENABLED=true
      shift
      ;;
    --no-build)
      BUILD_IF_MISSING=0
      shift
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

if [[ "$DB_PATH" != /* ]]; then
  DB_PATH="$ROOT_DIR/$DB_PATH"
fi

mkdir -p "$(dirname "$DB_PATH")"

if [[ ! -f "$JAR_PATH" ]]; then
  if [[ "$BUILD_IF_MISSING" -ne 1 ]]; then
    echo "Klaw jar not found: $JAR_PATH" >&2
    exit 1
  fi
  mvn -q -f "$KLAW_DIR/core/pom.xml" -DskipTests package
fi

echo "Starting Klaw Core"
echo "URL: http://localhost:$PORT/"
echo "Login: superadmin"
echo "Password: $SUPERADMIN_PASSWORD"
echo "Database: $DB_PATH"
echo

cd "$KLAW_DIR"
exec java \
  "-Dserver.port=$PORT" \
  "-Dspring.datasource.url=jdbc:h2:file:$DB_PATH;DB_CLOSE_ON_EXIT=FALSE;DB_CLOSE_DELAY=-1;MODE=MySQL;CASE_INSENSITIVE_IDENTIFIERS=TRUE;" \
  "-Dklaw.superadmin.default.password=$SUPERADMIN_PASSWORD" \
  "-Dklaw.coral.enabled=$CORAL_ENABLED" \
  -jar "$JAR_PATH"
