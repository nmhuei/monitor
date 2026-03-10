#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PORT="${PORT:-8784}"
VPORT="${VPORT:-8785}"
AGENT_NAME="${AGENT_NAME:-demo-agent}"
INTERVAL="${INTERVAL:-2}"

cleanup() {
  echo
  echo "[demo] stopping..."
  [[ -n "${AGENT_PID:-}" ]] && kill "$AGENT_PID" 2>/dev/null || true
  [[ -n "${SERVER_PID:-}" ]] && kill "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[demo] build..."
./build.sh >/dev/null

echo "[demo] start monitor_server on :$PORT (viewer:$VPORT)"
./monitor_server -port "$PORT" -vport "$VPORT" -config config/thresholds.conf -server-config config/server.conf &
SERVER_PID=$!
sleep 1

echo "[demo] start local agent: $AGENT_NAME"
./agent -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT_NAME" &
AGENT_PID=$!

echo "[demo] running. Press Ctrl+C to stop all."
echo "[demo] tips: open another terminal and run: nc 127.0.0.1 $VPORT"

wait "$SERVER_PID"
