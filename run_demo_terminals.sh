#!/usr/bin/env bash
# run_demo_terminals.sh — mở đa terminal để test monitor nhanh
# Usage: ./run_demo_terminals.sh [--agents N] [--interval S] [--stale S] [--offline S]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# ── Tham số ───────────────────────────────────────────────────────────────────
PORT="${PORT:-8784}"
VPORT="${VPORT:-8785}"
INTERVAL="${INTERVAL:-2}"       # chu kỳ metric (giây)
NUM_AGENTS="${NUM_AGENTS:-2}"   # số agent giả lập
STALE_SEC="${STALE_SEC:-15}"    # ngưỡng stale (giảm xuống để test nhanh hơn)
OFFLINE_SEC="${OFFLINE_SEC:-40}" # ngưỡng offline

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --agents)   NUM_AGENTS="$2"; shift 2 ;;
    --interval) INTERVAL="$2";   shift 2 ;;
    --stale)    STALE_SEC="$2";  shift 2 ;;
    --offline)  OFFLINE_SEC="$2";shift 2 ;;
    -h|--help)
      echo "Usage: $0 [--agents N] [--interval S] [--stale S] [--offline S]"
      echo "  --agents N    số agent (mặc định 2)"
      echo "  --interval S  chu kỳ metric giây (mặc định 2)"
      echo "  --stale S     giây không có metric → STALE (mặc định 15)"
      echo "  --offline S   giây không có metric → OFFLINE (mặc định 40)"
      exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

PID_DIR="$ROOT_DIR/.pids"
LOG_DIR="$ROOT_DIR/.logs"
mkdir -p "$PID_DIR" "$LOG_DIR"

# ── Build ─────────────────────────────────────────────────────────────────────
echo "▶ Building..."
./build.sh > /dev/null 2>&1 || { echo "Build thất bại!"; exit 1; }
echo "✔ Build OK"

# ── Ghi server.conf tạm với stale thấp để test nhanh ────────────────────────
DEMO_SERVER_CONF="$ROOT_DIR/config/server_demo.conf"
cat > "$DEMO_SERVER_CONF" << EOF
# Demo config — stale/offline thấp để test nhanh
MAX_AGENTS_PER_IP=10
BACKUP_INTERVAL_SEC=5
STATE_FILE=data/monitor_state.db
STALE_SEC=$STALE_SEC
OFFLINE_SEC=$OFFLINE_SEC
EOF
echo "✔ Demo server config: STALE=${STALE_SEC}s  OFFLINE=${OFFLINE_SEC}s"

# ── Dọn process cũ ───────────────────────────────────────────────────────────
for f in "$PID_DIR"/*.pid; do
  [[ -f "$f" ]] || continue
  pid=$(<"$f")
  kill "$pid" 2>/dev/null && echo "  Stopped old PID $pid ($(basename "$f"))" || true
  rm -f "$f"
done

# ── Hàm mở terminal ──────────────────────────────────────────────────────────
open_term() {
  local title="$1" cmd="$2"
  if command -v gnome-terminal &>/dev/null; then
    gnome-terminal --title="$title" -- bash -lc "$cmd; exec bash" &
  elif command -v xfce4-terminal &>/dev/null; then
    xfce4-terminal --title="$title" --hold -e "bash -lc '$cmd'" &
  elif command -v konsole &>/dev/null; then
    konsole --new-tab -p tabtitle="$title" -e bash -lc "$cmd; exec bash" &
  elif command -v xterm &>/dev/null; then
    xterm -title "$title" -e bash -lc "$cmd; exec bash" &
  else
    echo "[WARN] Không tìm thấy terminal emulator. Chạy thủ công:"
    echo "  $cmd"
    return 1
  fi
}

# ── Mở terminal 1: Monitor Server ────────────────────────────────────────────
SERVER_CMD="cd '$ROOT_DIR' && ./monitor_server -port $PORT -vport $VPORT -config config/thresholds.conf -server-config '$DEMO_SERVER_CONF'"
open_term "MONITOR [:$PORT]" "$SERVER_CMD"
echo "▶ Mở terminal MONITOR (port $PORT, viewer $VPORT)..."
sleep 1   # chờ server sẵn sàng

# ── Khởi động agents ẩn (background) ─────────────────────────────────────────
AGENT_NAMES=("web-1" "db-1" "cache-1" "worker-1" "api-1" "lb-1" "ml-1" "queue-1")
started_agents=()
for ((i=0; i<NUM_AGENTS && i<${#AGENT_NAMES[@]}; i++)); do
  aname="${AGENT_NAMES[$i]}"
  nohup ./agent -fg \
    -server "127.0.0.1:$PORT" \
    -interval "$INTERVAL" \
    -name "$aname" \
    -config config/agent.conf \
    > "$LOG_DIR/agent_${aname}.log" 2>&1 &
  echo $! > "$PID_DIR/agent_${aname}.pid"
  started_agents+=("$aname")
  echo "  ✔ Agent '$aname' started (PID $!)"
done

# ── Mở terminal 2: Viewer CLI ─────────────────────────────────────────────────
VIEWER_CMD="cd '$ROOT_DIR' && echo 'Commands: /hosts  /history <host> [n]  /log [n]' && ./viewer_cli -server 127.0.0.1:$VPORT"
open_term "VIEWER [:$VPORT]" "$VIEWER_CMD"
echo "▶ Mở terminal VIEWER (port $VPORT)..."

# ── Mở terminal 3: Agent log tail (optional monitor) ─────────────────────────
LOG_CMD="echo '=== Agent Logs ===' && tail -f $(printf "'%s' " "$LOG_DIR"/agent_*.log)"
open_term "AGENT LOGS" "$LOG_CMD"
echo "▶ Mở terminal AGENT LOGS..."

# ── Tóm tắt ──────────────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Demo đang chạy — ${#started_agents[@]} agents: ${started_agents[*]}"
echo "  STALE sau ${STALE_SEC}s | OFFLINE sau ${OFFLINE_SEC}s"
echo ""
echo "  Test STALE/OFFLINE:"
echo "    Ctrl+C một agent để dừng → quan sát dashboard"
echo "    Sau ${STALE_SEC}s: host chuyển sang STALE (◌)"
echo "    Sau ${OFFLINE_SEC}s: host chuyển sang OFFLINE"
echo ""
echo "  Dừng tất cả agents:"
echo "    ./stop_agents.sh"
echo ""
echo "  Viewer commands (trong cửa sổ VIEWER):"
echo "    /hosts               — xem tất cả hosts"
echo "    /history web-1       — xem history của web-1"
echo "    /history web-1 10    — 10 samples gần nhất"
echo "    /log                 — xem event log"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
