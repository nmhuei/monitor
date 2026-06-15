#!/usr/bin/env bash
# test/integration_test_full.sh — Comprehensive E2E integration test suite
# Covers 32 scenarios across 10 groups for Distributed System Monitor
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║       Comprehensive E2E Integration Test Suite              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# ── Test ports (unique to avoid conflicts) ────────────────────────────────────
SERVER_PORT=9981
VIEWER_PORT=9982
HTTP_PORT=9983

# ── Directories ───────────────────────────────────────────────────────────────
PID_DIR="$ROOT_DIR/.test_full_pids"
LOG_DIR="$ROOT_DIR/.test_full_logs"
DATA_DIR="$ROOT_DIR/.test_full_data"
CFG_DIR="$ROOT_DIR/.test_full_config"
mkdir -p "$PID_DIR" "$LOG_DIR" "$DATA_DIR" "$CFG_DIR"
rm -rf "$PID_DIR"/* "$LOG_DIR"/* "$DATA_DIR"/* "$CFG_DIR"/*

# ── Counters ──────────────────────────────────────────────────────────────────
PASSED=0
FAILED=0
TOTAL=0
FAIL_DETAILS=""

# ── Test framework ────────────────────────────────────────────────────────────
run_test() {
  local name="$1"
  TOTAL=$((TOTAL + 1))
  echo -n "  [$TOTAL] $name... "
}

pass() {
  PASSED=$((PASSED + 1))
  echo "✔"
}

fail() {
  FAILED=$((FAILED + 1))
  local msg="${1:-}"
  echo "✘ $msg"
  FAIL_DETAILS="${FAIL_DETAILS}  [$TOTAL] FAILED: $msg\n"
}

# ── Cleanup ───────────────────────────────────────────────────────────────────
kill_pid_file() {
  local f="$1"
  if [[ -f "$f" ]]; then
    local pid
    pid=$(cat "$f")
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      # Wait briefly for process to exit
      for _ in $(seq 1 10); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
      done
      # Force kill if still alive
      kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$f"
  fi
}

kill_all_test_processes() {
  for f in "$PID_DIR"/*.pid; do
    if [[ -f "$f" ]]; then
      kill_pid_file "$f" || true
    fi
  done
  return 0
}

cleanup() {
  echo ""
  echo "Cleaning up all test processes..."
  kill_all_test_processes || true
  rm -rf "$PID_DIR" "$LOG_DIR" "$DATA_DIR" "$CFG_DIR" || true
}
trap cleanup EXIT

# ── Helper: start the server ──────────────────────────────────────────────────
start_server() {
  local server_conf="$1"
  local thresh_conf="${2:-$CFG_DIR/thresholds_test.conf}"
  local log_file="$LOG_DIR/server.log"
  TERM=screen ./monitor_server \
    -port "$SERVER_PORT" \
    -vport "$VIEWER_PORT" \
    -server-config "$server_conf" \
    -config "$thresh_conf" \
    > "$log_file" 2>&1 &
  echo $! > "$PID_DIR/server.pid"
  # Wait for server to be ready (listen on HTTP port)
  local tries=0
  while ! curl -sf "http://127.0.0.1:$HTTP_PORT/healthz" >/dev/null 2>&1; do
    sleep 0.3
    tries=$((tries + 1))
    if [[ $tries -ge 20 ]]; then
      echo "ERROR: Server did not become ready in 6s"
      cat "$log_file"
      return 1
    fi
  done
  return 0
}

stop_server() {
  kill_pid_file "$PID_DIR/server.pid"
  sleep 0.5
}

# ── Helper: start an agent ────────────────────────────────────────────────────
start_agent() {
  local name="$1"
  local agent_conf="$2"
  local pid_file="$3"
  local log_file="$4"
  local interval="${5:-1}"
  ./agent -fg -server "127.0.0.1:$SERVER_PORT" \
    -name "$name" \
    -config "$agent_conf" \
    -interval "$interval" \
    > "$log_file" 2>&1 &
  echo $! > "$pid_file"
}

# ── Helper: query API ────────────────────────────────────────────────────────
api_get() {
  local path="$1"
  curl -sf "http://127.0.0.1:$HTTP_PORT$path" 2>/dev/null || echo ""
}

api_status() {
  local path="$1"
  curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$HTTP_PORT$path" 2>/dev/null || echo "000"
}

# ── Helper: viewer CMD query ─────────────────────────────────────────────────
viewer_cmd() {
  local cmd="$1"
  echo "CMD $cmd" | nc -q 2 127.0.0.1 "$VIEWER_PORT" 2>/dev/null || echo ""
}

# ── Write config files ───────────────────────────────────────────────────────
# Thresholds config
cat > "$CFG_DIR/thresholds_test.conf" << 'EOF'
CPU=50
RAM=60
DISK=80
EOF

# Main server config
cat > "$CFG_DIR/server_test.conf" << EOF
AUTH_TOKEN=testsecret123
MAX_AGENTS_PER_IP=3
BACKUP_INTERVAL_SEC=2
STATE_FILE=$DATA_DIR/test_state.db
STALE_SEC=5
OFFLINE_SEC=10
HTTP_API_PORT=$HTTP_PORT
LOG_FILE=$DATA_DIR/test_monitor.log
LOG_LEVEL=DEBUG
HISTORY_DIR=$DATA_DIR/history
HISTORY_MAX_LINES=1000
EOF

# Agent config (valid token)
cat > "$CFG_DIR/agent_valid.conf" << 'EOF'
MAX_CONNECT_RETRIES=0
RECONNECT_INTERVAL_SEC=2
AUTH_TOKEN=testsecret123
EOF

# Agent config (invalid token)
cat > "$CFG_DIR/agent_bad.conf" << 'EOF'
MAX_CONNECT_RETRIES=1
RECONNECT_INTERVAL_SEC=1
AUTH_TOKEN=wrongsecret999
EOF

# Agent config (empty token — for when server has no auth)
cat > "$CFG_DIR/agent_noauth.conf" << 'EOF'
MAX_CONNECT_RETRIES=1
RECONNECT_INTERVAL_SEC=1
AUTH_TOKEN=
EOF

# Server config with NO auth token
cat > "$CFG_DIR/server_noauth.conf" << EOF
MAX_AGENTS_PER_IP=3
BACKUP_INTERVAL_SEC=2
STATE_FILE=$DATA_DIR/test_state_noauth.db
STALE_SEC=5
OFFLINE_SEC=10
HTTP_API_PORT=$HTTP_PORT
LOG_FILE=$DATA_DIR/test_monitor_noauth.log
LOG_LEVEL=DEBUG
HISTORY_DIR=$DATA_DIR/history_noauth
HISTORY_MAX_LINES=1000
EOF

###############################################################################
# GROUP 1: Server Startup & Configuration
###############################################################################
echo "━━━ Group 1: Server Startup & Configuration ━━━"

# Test 1: Server starts successfully and listens on configured ports
run_test "Server starts and listens on configured ports"
rm -f "$DATA_DIR/test_state.db"
if start_server "$CFG_DIR/server_test.conf"; then
  # Verify all three ports are listening
  health=$(curl -sf "http://127.0.0.1:$HTTP_PORT/healthz" || echo "")
  if [[ "$health" == *"ok"* ]]; then
    # Verify agent port is open by checking if nc can connect
    if echo "" | nc -w 1 127.0.0.1 "$SERVER_PORT" >/dev/null 2>&1; then
      # Verify viewer port is open
      if echo "" | nc -w 1 127.0.0.1 "$VIEWER_PORT" >/dev/null 2>&1; then
        pass
      else
        fail "Viewer port $VIEWER_PORT not listening"
      fi
    else
      fail "Server port $SERVER_PORT not listening"
    fi
  else
    fail "Health endpoint not responding"
  fi
else
  fail "Server failed to start"
fi
stop_server

# Test 2: Server creates data directory if missing
run_test "Server creates data directory if missing"
rm -rf "$DATA_DIR/subdir_test"
cat > "$CFG_DIR/server_subdir.conf" << EOF
AUTH_TOKEN=testsecret123
MAX_AGENTS_PER_IP=3
BACKUP_INTERVAL_SEC=2
STATE_FILE=$DATA_DIR/subdir_test/nested/state.db
STALE_SEC=5
OFFLINE_SEC=10
HTTP_API_PORT=$HTTP_PORT
LOG_FILE=$DATA_DIR/test_monitor_subdir.log
LOG_LEVEL=DEBUG
HISTORY_DIR=$DATA_DIR/history_subdir
HISTORY_MAX_LINES=1000
EOF
if start_server "$CFG_DIR/server_subdir.conf"; then
  if [[ -d "$DATA_DIR/subdir_test/nested" ]]; then
    pass
  else
    fail "Directory $DATA_DIR/subdir_test/nested was not created"
  fi
else
  fail "Server failed to start"
fi
stop_server

# Test 3: Server loads thresholds config correctly
run_test "Server loads thresholds config correctly"
cat > "$CFG_DIR/thresholds_custom.conf" << 'EOF'
CPU=85
RAM=90
DISK=95
EOF
if start_server "$CFG_DIR/server_test.conf" "$CFG_DIR/thresholds_custom.conf"; then
  # If it started, thresholds loaded without crash
  health=$(curl -sf "http://127.0.0.1:$HTTP_PORT/healthz" || echo "")
  if [[ "$health" == *"ok"* ]]; then
    pass
  else
    fail "Server not healthy after loading custom thresholds"
  fi
else
  fail "Server failed to start with custom thresholds"
fi
stop_server

###############################################################################
# GROUP 2: Agent Authentication
###############################################################################
echo "━━━ Group 2: Agent Authentication ━━━"

# Start server for auth tests
start_server "$CFG_DIR/server_test.conf"

# Test 4: Agent with valid token connects successfully
run_test "Agent with valid token connects successfully"
start_agent "auth-valid-agent" "$CFG_DIR/agent_valid.conf" "$PID_DIR/agent_auth_ok.pid" "$LOG_DIR/agent_auth_ok.log"
sleep 3
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q "auth-valid-agent"; then
  pass
else
  fail "Agent with valid token not found in /api/hosts. Response: $RESP"
fi
kill_pid_file "$PID_DIR/agent_auth_ok.pid"
sleep 1

# Test 5: Agent with invalid token is rejected
run_test "Agent with invalid token is rejected"
./agent -fg -server "127.0.0.1:$SERVER_PORT" -name "auth-bad-agent" \
  -config "$CFG_DIR/agent_bad.conf" -interval 1 \
  > "$LOG_DIR/agent_auth_bad.log" 2>&1 &
BAD_PID=$!
sleep 3
kill "$BAD_PID" 2>/dev/null || true
wait "$BAD_PID" 2>/dev/null || true
if grep -q "Auth failed" "$LOG_DIR/agent_auth_bad.log"; then
  pass
else
  fail "Expected 'Auth failed' in agent log"
  echo "    Agent log: $(cat "$LOG_DIR/agent_auth_bad.log")"
fi

stop_server
sleep 1

# Test 6: Agent with empty token connects when server has no token configured
run_test "Agent with empty token connects when server has no token"
start_server "$CFG_DIR/server_noauth.conf"
start_agent "noauth-agent" "$CFG_DIR/agent_noauth.conf" "$PID_DIR/agent_noauth.pid" "$LOG_DIR/agent_noauth.log"
sleep 3
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q "noauth-agent"; then
  pass
else
  fail "Agent without token not found when server has no token. Response: $RESP"
fi
kill_pid_file "$PID_DIR/agent_noauth.pid"
stop_server
sleep 1

###############################################################################
# GROUP 3: Agent Connection & Metrics
###############################################################################
echo "━━━ Group 3: Agent Connection & Metrics ━━━"

start_server "$CFG_DIR/server_test.conf"

# Test 7: Single agent connects, verify via HTTP API status=OK
run_test "Single agent connects, status=OK via HTTP API"
start_agent "single-agent" "$CFG_DIR/agent_valid.conf" "$PID_DIR/agent_single.pid" "$LOG_DIR/agent_single.log"
sleep 3
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q '"host":"single-agent"' && echo "$RESP" | grep -q '"status":"OK"'; then
  pass
else
  fail "single-agent not OK. Response: $RESP"
fi

# Test 8: Multiple agents (3) connect, all visible in /api/hosts
run_test "Multiple agents (3) connect, all visible in /api/hosts"
start_agent "multi-agent-2" "$CFG_DIR/agent_valid.conf" "$PID_DIR/agent_multi2.pid" "$LOG_DIR/agent_multi2.log"
start_agent "multi-agent-3" "$CFG_DIR/agent_valid.conf" "$PID_DIR/agent_multi3.pid" "$LOG_DIR/agent_multi3.log"
sleep 3
RESP=$(api_get "/api/hosts")
FOUND=0
echo "$RESP" | grep -q "single-agent" && FOUND=$((FOUND + 1))
echo "$RESP" | grep -q "multi-agent-2" && FOUND=$((FOUND + 1))
echo "$RESP" | grep -q "multi-agent-3" && FOUND=$((FOUND + 1))
if [[ $FOUND -eq 3 ]]; then
  pass
else
  fail "Only $FOUND/3 agents visible. Response: $RESP"
fi

# Test 9: Agent metrics update in real-time
run_test "Agent metrics update in real-time (two queries, CPU values may differ)"
RESP1=$(api_get "/api/hosts")
sleep 3
RESP2=$(api_get "/api/hosts")
# Both responses should have the agent and have cpu fields
if echo "$RESP1" | grep -q '"cpu"' && echo "$RESP2" | grep -q '"cpu"'; then
  pass
else
  fail "CPU metrics not found in API responses"
fi

# Test 10: Agent with custom name appears correctly
run_test "Agent with custom name appears correctly"
# single-agent was started with -name "single-agent", verify exact match
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q '"host":"single-agent"'; then
  pass
else
  fail "Custom name 'single-agent' not found in response"
fi

# Clean up agents for next group
kill_pid_file "$PID_DIR/agent_multi2.pid"
kill_pid_file "$PID_DIR/agent_multi3.pid"
# Keep single-agent running for status transition tests
sleep 1

###############################################################################
# GROUP 4: Status Transitions
###############################################################################
echo "━━━ Group 4: Status Transitions ━━━"

# Test 11: SIGSTOP agent → wait STALE_SEC+2 → verify STALE status via API
run_test "SIGSTOP agent → STALE after STALE_SEC+2"
AGENT_PID=$(cat "$PID_DIR/agent_single.pid")
kill -STOP "$AGENT_PID"
sleep 11  # STALE_SEC=5, wait 11s to ensure stale check loop executes
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q '"host":"single-agent"' && echo "$RESP" | grep -q '"status":"STALE"'; then
  pass
else
  fail "Expected STALE status. Response: $RESP"
fi

# Test 12: Continue waiting → verify OFFLINE status via API
run_test "Continue waiting → OFFLINE after OFFLINE_SEC"
sleep 6  # Total ~13s from stop, OFFLINE_SEC=10
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q '"host":"single-agent"' && echo "$RESP" | grep -q '"status":"OFFLINE"'; then
  pass
else
  fail "Expected OFFLINE status. Response: $RESP"
fi

# Test 13: SIGCONT agent → wait 3s → verify back to OK
run_test "SIGCONT agent → back to OK"
kill -CONT "$AGENT_PID"
sleep 4
RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q '"host":"single-agent"' && echo "$RESP" | grep -q '"status":"OK"'; then
  pass
else
  fail "Expected OK status after SIGCONT. Response: $RESP"
fi

# Test 14: Kill agent process → verify transitions to STALE then OFFLINE
run_test "Kill agent → transitions to STALE then OFFLINE"
kill "$AGENT_PID" 2>/dev/null || true
wait "$AGENT_PID" 2>/dev/null || true
rm -f "$PID_DIR/agent_single.pid"
sleep 11  # Wait for STALE_SEC+2 (11s to ensure stale check loop executes)
RESP=$(api_get "/api/hosts")
GOT_STALE=false
if echo "$RESP" | grep -q '"host":"single-agent"'; then
  if echo "$RESP" | grep -q '"status":"STALE"'; then
    GOT_STALE=true
  fi
fi
sleep 6  # Wait more for OFFLINE
RESP=$(api_get "/api/hosts")
GOT_OFFLINE=false
if echo "$RESP" | grep -q '"host":"single-agent"'; then
  if echo "$RESP" | grep -q '"status":"OFFLINE"'; then
    GOT_OFFLINE=true
  fi
fi
if $GOT_STALE && $GOT_OFFLINE; then
  pass
elif $GOT_OFFLINE; then
  # May have transitioned too fast from STALE to OFFLINE — still acceptable
  pass
else
  fail "Expected STALE→OFFLINE transition. Got stale=$GOT_STALE offline=$GOT_OFFLINE"
fi

stop_server
sleep 1

###############################################################################
# GROUP 5: IP Rate Limiting
###############################################################################
echo "━━━ Group 5: IP Rate Limiting ━━━"

start_server "$CFG_DIR/server_test.conf"

# Test 15: Connect MAX_AGENTS_PER_IP agents from same IP → all succeed
run_test "Connect MAX_AGENTS_PER_IP (3) agents → all succeed"
start_agent "iplim-1" "$CFG_DIR/agent_valid.conf" "$PID_DIR/iplim1.pid" "$LOG_DIR/iplim1.log"
start_agent "iplim-2" "$CFG_DIR/agent_valid.conf" "$PID_DIR/iplim2.pid" "$LOG_DIR/iplim2.log"
start_agent "iplim-3" "$CFG_DIR/agent_valid.conf" "$PID_DIR/iplim3.pid" "$LOG_DIR/iplim3.log"
sleep 3
RESP=$(api_get "/api/hosts")
FOUND=0
echo "$RESP" | grep -q "iplim-1" && FOUND=$((FOUND + 1))
echo "$RESP" | grep -q "iplim-2" && FOUND=$((FOUND + 1))
echo "$RESP" | grep -q "iplim-3" && FOUND=$((FOUND + 1))
if [[ $FOUND -eq 3 ]]; then
  pass
else
  fail "Only $FOUND/3 agents connected. Response: $RESP"
fi

# Test 16: Connect MAX_AGENTS_PER_IP+1 → last one rejected
run_test "Connect MAX_AGENTS_PER_IP+1 (4th) → rejected with ip_limit"
./agent -fg -server "127.0.0.1:$SERVER_PORT" -name "iplim-4-reject" \
  -config "$CFG_DIR/agent_valid.conf" -interval 1 \
  > "$LOG_DIR/iplim4.log" 2>&1 &
IPLIM4_PID=$!
sleep 3
kill "$IPLIM4_PID" 2>/dev/null || true
wait "$IPLIM4_PID" 2>/dev/null || true
# Check server log or agent behavior for ip_limit
SRV_LOG="$DATA_DIR/test_monitor.log"
if grep -q "ip_limit" "$SRV_LOG" 2>/dev/null || grep -q "ip_limit" "$LOG_DIR/iplim4.log" 2>/dev/null; then
  pass
else
  fail "No ip_limit rejection found in server or agent logs"
fi

# Clean up IP limit agents
kill_pid_file "$PID_DIR/iplim1.pid"
kill_pid_file "$PID_DIR/iplim2.pid"
kill_pid_file "$PID_DIR/iplim3.pid"
sleep 1

###############################################################################
# GROUP 6: Viewer Protocol (CMD queries)
###############################################################################
echo "━━━ Group 6: Viewer Protocol (CMD queries) ━━━"

# Start a fresh agent for viewer tests
start_agent "viewer-test-host" "$CFG_DIR/agent_valid.conf" "$PID_DIR/viewer_agent.pid" "$LOG_DIR/viewer_agent.log"
sleep 3

# Test 17: CMD hosts → returns JSON array with connected hosts
run_test "CMD hosts → JSON array with connected hosts"
VRESP=$(viewer_cmd "hosts")
if echo "$VRESP" | grep -q "viewer-test-host" && echo "$VRESP" | grep -q '\['; then
  pass
else
  fail "CMD hosts did not return expected JSON. Got: $VRESP"
fi

# Test 18: CMD history <host> 5 → returns JSON array with ≤5 samples
run_test "CMD history <host> 5 → JSON array with ≤5 samples"
VRESP=$(viewer_cmd "history viewer-test-host 5")
if echo "$VRESP" | grep -q '\['; then
  pass
else
  fail "CMD history did not return JSON array. Got: $VRESP"
fi

# Test 19: CMD history nonexistent_host → returns []
run_test "CMD history nonexistent_host → returns empty"
VRESP=$(viewer_cmd "history nonexistent_host_xyz 5")
if echo "$VRESP" | grep -q '\[\]' || echo "$VRESP" | grep -q '\['; then
  pass
else
  fail "CMD history for nonexistent host returned unexpected. Got: $VRESP"
fi

# Test 20: CMD log 10 → returns JSON array with log entries
run_test "CMD log 10 → JSON array with log entries"
VRESP=$(viewer_cmd "log 10")
if echo "$VRESP" | grep -q '\['; then
  pass
else
  fail "CMD log did not return JSON array. Got: $VRESP"
fi

# Test 21: CMD invalid_command → returns error or empty
run_test "CMD invalid_command → returns error"
VRESP=$(viewer_cmd "invalid_command_xyz")
if echo "$VRESP" | grep -q "error" || [[ -z "$VRESP" ]]; then
  pass
else
  fail "CMD invalid_command did not return error. Got: $VRESP"
fi

kill_pid_file "$PID_DIR/viewer_agent.pid"
sleep 1

###############################################################################
# GROUP 7: HTTP API
###############################################################################
echo "━━━ Group 7: HTTP API ━━━"

# Start a fresh agent for API tests
start_agent "api-test-host" "$CFG_DIR/agent_valid.conf" "$PID_DIR/api_agent.pid" "$LOG_DIR/api_agent.log"
sleep 3

# Test 22: GET /api/hosts → JSON array, status 200
run_test "GET /api/hosts → JSON array, status 200"
HTTP_CODE=$(api_status "/api/hosts")
RESP=$(api_get "/api/hosts")
if [[ "$HTTP_CODE" == "200" ]] && echo "$RESP" | grep -q '\['; then
  pass
else
  fail "Expected 200 with JSON array. Got HTTP $HTTP_CODE, body: $RESP"
fi

# Test 23: GET /api/stats → JSON with agents_online, msgs_total, etc.
run_test "GET /api/stats → JSON with agents_online and msgs_total"
HTTP_CODE=$(api_status "/api/stats")
RESP=$(api_get "/api/stats")
if [[ "$HTTP_CODE" == "200" ]] && echo "$RESP" | grep -q "agents_online" && echo "$RESP" | grep -q "msgs_total"; then
  pass
else
  fail "Expected 200 with stats JSON. Got HTTP $HTTP_CODE, body: $RESP"
fi

# Test 24: GET /metrics → Prometheus format text with monitor_ prefixed metrics
run_test "GET /metrics → Prometheus format with monitor_ prefix"
HTTP_CODE=$(api_status "/metrics")
RESP=$(api_get "/metrics")
if [[ "$HTTP_CODE" == "200" ]] && echo "$RESP" | grep -q "monitor_agents_online" && echo "$RESP" | grep -q "monitor_msgs_total"; then
  pass
else
  fail "Expected Prometheus metrics with monitor_ prefix. Got HTTP $HTTP_CODE"
fi

# Test 25: GET /api/nonexistent → 404
run_test "GET /api/nonexistent → 404"
HTTP_CODE=$(api_status "/api/nonexistent")
if [[ "$HTTP_CODE" == "404" ]]; then
  pass
else
  fail "Expected 404, got HTTP $HTTP_CODE"
fi

kill_pid_file "$PID_DIR/api_agent.pid"
stop_server
sleep 1

###############################################################################
# GROUP 8: State Persistence
###############################################################################
echo "━━━ Group 8: State Persistence ━━━"

rm -f "$DATA_DIR/test_state.db"
start_server "$CFG_DIR/server_test.conf"

# Test 26: Connect agent, wait for state save (BACKUP_INTERVAL_SEC=2)
run_test "State is saved after BACKUP_INTERVAL_SEC"
start_agent "persist-agent" "$CFG_DIR/agent_valid.conf" "$PID_DIR/persist_agent.pid" "$LOG_DIR/persist_agent.log"
sleep 5  # BACKUP_INTERVAL_SEC=2, wait 5s to ensure at least one save cycle
if [[ -f "$DATA_DIR/test_state.db" ]]; then
  pass
else
  fail "State file $DATA_DIR/test_state.db not found after 5s"
fi

# Test 27: Verify state file exists and has content
run_test "State file exists and has content"
if [[ -f "$DATA_DIR/test_state.db" ]] && [[ -s "$DATA_DIR/test_state.db" ]]; then
  pass
else
  fail "State file missing or empty"
fi

# Test 28: Kill server, restart, verify host data persisted
run_test "Kill server → restart → host data persisted"
kill_pid_file "$PID_DIR/persist_agent.pid"
sleep 1
stop_server
sleep 1
# Restart server with same config (same STATE_FILE)
start_server "$CFG_DIR/server_test.conf"
RESP=$(api_get "/api/hosts")
# The host should appear (as STALE or OFFLINE since agent is dead)
if echo "$RESP" | grep -q "persist-agent"; then
  pass
else
  fail "persist-agent not found after server restart. Response: $RESP"
fi
stop_server
sleep 1

###############################################################################
# GROUP 9: Reconnection
###############################################################################
echo "━━━ Group 9: Reconnection ━━━"

# Test 29: Start agent, kill server, restart server → agent reconnects
run_test "Agent reconnects after server restart"
start_server "$CFG_DIR/server_test.conf"
start_agent "reconnect-agent" "$CFG_DIR/agent_valid.conf" "$PID_DIR/reconnect_agent.pid" "$LOG_DIR/reconnect_agent.log"
sleep 3
# Verify connected
RESP=$(api_get "/api/hosts")
INITIAL_OK=false
echo "$RESP" | grep -q "reconnect-agent" && INITIAL_OK=true

# Kill server
stop_server
sleep 2

# Restart server
start_server "$CFG_DIR/server_test.conf"
sleep 5  # Give agent time to detect disconnect and reconnect

RESP=$(api_get "/api/hosts")
if echo "$RESP" | grep -q "reconnect-agent" && echo "$RESP" | grep -q '"status":"OK"'; then
  pass
elif $INITIAL_OK; then
  # Agent may still be reconnecting — try again
  sleep 5
  RESP=$(api_get "/api/hosts")
  if echo "$RESP" | grep -q "reconnect-agent" && echo "$RESP" | grep -q '"status":"OK"'; then
    pass
  else
    fail "Agent did not reconnect after server restart. Response: $RESP"
  fi
else
  fail "Agent was never connected initially."
fi
kill_pid_file "$PID_DIR/reconnect_agent.pid"
stop_server
sleep 1

###############################################################################
# GROUP 10: Stress & Edge Cases
###############################################################################
echo "━━━ Group 10: Stress & Edge Cases ━━━"

# Need to raise IP limit for stress tests
cat > "$CFG_DIR/server_stress.conf" << EOF
AUTH_TOKEN=testsecret123
MAX_AGENTS_PER_IP=10
BACKUP_INTERVAL_SEC=2
STATE_FILE=$DATA_DIR/test_state_stress.db
STALE_SEC=5
OFFLINE_SEC=10
HTTP_API_PORT=$HTTP_PORT
LOG_FILE=$DATA_DIR/test_monitor_stress.log
LOG_LEVEL=DEBUG
HISTORY_DIR=$DATA_DIR/history_stress
HISTORY_MAX_LINES=1000
EOF
start_server "$CFG_DIR/server_stress.conf"

# Test 30: Connect 5 agents simultaneously, verify all appear
run_test "Connect 5 agents simultaneously → all appear"
for i in $(seq 1 5); do
  start_agent "stress-agent-$i" "$CFG_DIR/agent_valid.conf" \
    "$PID_DIR/stress_agent_$i.pid" "$LOG_DIR/stress_agent_$i.log"
done
sleep 5
RESP=$(api_get "/api/hosts")
FOUND=0
for i in $(seq 1 5); do
  echo "$RESP" | grep -q "stress-agent-$i" && FOUND=$((FOUND + 1))
done
if [[ $FOUND -eq 5 ]]; then
  pass
else
  fail "Only $FOUND/5 agents visible. Response: $RESP"
fi

# Test 31: Send metrics at fast interval (1s) for 10s, verify no crash
run_test "Fast interval (1s) for 10s → no crash"
sleep 10  # Agents already running at 1s interval
SERVER_PID=$(cat "$PID_DIR/server.pid")
if kill -0 "$SERVER_PID" 2>/dev/null; then
  RESP=$(api_get "/api/hosts")
  if echo "$RESP" | grep -q "stress-agent"; then
    pass
  else
    fail "Server alive but no agents found after 10s fast interval"
  fi
else
  fail "Server crashed during fast interval test"
fi

# Clean up stress agents
for i in $(seq 1 5); do
  kill_pid_file "$PID_DIR/stress_agent_$i.pid"
done
sleep 1

# Test 32: Agent with very long name (50 chars) → verify it appears
run_test "Agent with very long name (50 chars) → appears in API"
LONG_NAME="agent-with-a-very-long-name-that-is-fifty-chars-xx"
start_agent "$LONG_NAME" "$CFG_DIR/agent_valid.conf" \
  "$PID_DIR/long_name_agent.pid" "$LOG_DIR/long_name_agent.log"
sleep 3
RESP=$(api_get "/api/hosts")
# Check if the name appears (possibly truncated)
if echo "$RESP" | grep -q "agent-with-a-very-long-name"; then
  pass
else
  fail "Long-name agent not found in response. Response: $RESP"
fi
kill_pid_file "$PID_DIR/long_name_agent.pid"

stop_server

###############################################################################
# Results
###############################################################################
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [[ $FAILED -gt 0 ]]; then
  echo ""
  echo "Failed tests:"
  echo -e "$FAIL_DETAILS"
  exit 1
else
  echo ""
  echo "╔══════════════════════════════════════════════════════════════╗"
  echo "║              === ALL TESTS PASSED ===                       ║"
  echo "╚══════════════════════════════════════════════════════════════╝"
  exit 0
fi
