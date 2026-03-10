#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PORT="${PORT:-8784}"
VPORT="${VPORT:-8785}"
AGENT1_NAME="${AGENT1_NAME:-web-1}"
AGENT2_NAME="${AGENT2_NAME:-db-1}"
CFG_FILE="$ROOT_DIR/config/server.conf"

read_cfg() {
  local key="$1" def="$2"
  if [[ -f "$CFG_FILE" ]]; then
    local v
    v=$(grep -E "^${key}=" "$CFG_FILE" | tail -n1 | cut -d'=' -f2- || true)
    v="${v//[[:space:]]/}"
    [[ -n "$v" ]] && { echo "$v"; return; }
  fi
  echo "$def"
}

INTERVAL="${INTERVAL:-$(read_cfg AGENT_INTERVAL_SEC 2)}"

cleanup() {
  echo
  echo "[demo] stopping processes..."
  [[ -n "${AGENT1_PID:-}" ]] && kill "$AGENT1_PID" 2>/dev/null || true
  [[ -n "${AGENT2_PID:-}" ]] && kill "$AGENT2_PID" 2>/dev/null || true
  [[ -n "${SERVER_PID:-}" ]] && kill "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[demo] build..."
./build.sh >/dev/null

echo "[demo] start monitor_server on :$PORT (viewer:$VPORT)"
./monitor_server -port "$PORT" -vport "$VPORT" -config config/thresholds.conf -server-config config/server.conf &
SERVER_PID=$!
sleep 1

echo "[demo] start agent #1: $AGENT1_NAME"
./agent -fg -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT1_NAME" -config config/agent.conf &
AGENT1_PID=$!

echo "[demo] start agent #2: $AGENT2_NAME"
./agent -fg -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT2_NAME" -config config/agent.conf &
AGENT2_PID=$!

echo "[demo] launch viewer_cli (interactive test)"
echo "[demo] commands: /history <host> <n> | /warning <host> <n> | /clear"
echo "[demo] press q in viewer to stop demo"
./viewer_cli -server 127.0.0.1:"$VPORT"
