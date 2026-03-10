#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PORT="${PORT:-8784}"
VPORT="${VPORT:-8785}"
INTERVAL="${INTERVAL:-2}"
AGENT1_NAME="${AGENT1_NAME:-web-1}"
AGENT2_NAME="${AGENT2_NAME:-db-1}"

cleanup() {
  echo
  echo "[demo] stopping agents..."
  [[ -n "${AGENT1_PID:-}" ]] && kill "$AGENT1_PID" 2>/dev/null || true
  [[ -n "${AGENT2_PID:-}" ]] && kill "$AGENT2_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[demo] build..."
./build.sh >/dev/null

echo "[demo] start agent #1: $AGENT1_NAME"
./agent -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT1_NAME" &
AGENT1_PID=$!

echo "[demo] start agent #2: $AGENT2_NAME"
./agent -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT2_NAME" &
AGENT2_PID=$!

echo "[demo] start monitor_server (foreground) on :$PORT"
echo "[demo] press Ctrl+C to stop all"
./monitor_server -port "$PORT" -vport "$VPORT" -config config/thresholds.conf -server-config config/server.conf
