#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${PORT:-8784}"
VPORT="${VPORT:-8785}"
AGENT1_NAME="${AGENT1_NAME:-web-1}"
AGENT2_NAME="${AGENT2_NAME:-db-1}"
PID_DIR="$ROOT_DIR/.pids"
LOG_DIR="$ROOT_DIR/.logs"
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

mkdir -p "$PID_DIR" "$LOG_DIR"
cd "$ROOT_DIR"
./build.sh >/dev/null

# Start agents hidden in background
nohup ./agent -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT1_NAME" \
  >"$LOG_DIR/agent1.log" 2>&1 &
echo $! > "$PID_DIR/agent1.pid"

nohup ./agent -server 127.0.0.1:"$PORT" -interval "$INTERVAL" -name "$AGENT2_NAME" \
  >"$LOG_DIR/agent2.log" 2>&1 &
echo $! > "$PID_DIR/agent2.pid"

open_term() {
  local title="$1"
  local cmd="$2"

  if command -v gnome-terminal >/dev/null 2>&1; then
    gnome-terminal --title="$title" -- bash -lc "$cmd; exec bash"
  elif command -v xfce4-terminal >/dev/null 2>&1; then
    xfce4-terminal --title="$title" --hold -e "bash -lc '$cmd'"
  elif command -v konsole >/dev/null 2>&1; then
    konsole --new-tab -p tabtitle="$title" -e bash -lc "$cmd; exec bash"
  elif command -v x-terminal-emulator >/dev/null 2>&1; then
    x-terminal-emulator -e bash -lc "$cmd; exec bash"
  else
    echo "No supported terminal emulator found (gnome-terminal/xfce4-terminal/konsole/x-terminal-emulator)."
    echo "Monitor command: ./monitor_server -port $PORT -vport $VPORT -config config/thresholds.conf -server-config config/server.conf"
    exit 1
  fi
}

# Open only monitor terminal
open_term "MONITOR" "cd '$ROOT_DIR' && ./monitor_server -port $PORT -vport $VPORT -config config/thresholds.conf -server-config config/server.conf"

echo "Opened MONITOR terminal only. Agents are running hidden in background."
echo "Agent logs: $LOG_DIR/agent1.log, $LOG_DIR/agent2.log"
echo "To stop hidden agents: kill \$(cat $PID_DIR/agent1.pid) \$(cat $PID_DIR/agent2.pid)"
