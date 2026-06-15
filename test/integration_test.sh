#!/usr/bin/env bash
# test/integration_test.sh — End-to-end integration test for Distributed System Monitor
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "=== E2E Integration Test ==="

PID_DIR="$ROOT_DIR/.test_pids"
LOG_DIR="$ROOT_DIR/.test_logs"
mkdir -p "$PID_DIR" "$LOG_DIR"
rm -f "$PID_DIR"/* "$LOG_DIR"/*

cleanup() {
  echo "Cleaning up test processes..."
  for f in "$PID_DIR"/*.pid; do
    if [[ -f "$f" ]]; then
      pid=$(cat "$f")
      kill "$pid" 2>/dev/null || true
    fi
  done
  rm -rf "$PID_DIR" "$LOG_DIR"
  rm -f config/server_test.conf config/agent_test.conf
}
trap cleanup EXIT

# 1. Write server config
TEST_SERVER_CONF="$ROOT_DIR/config/server_test.conf"
cat > "$TEST_SERVER_CONF" << EOF
MAX_AGENTS_PER_IP=3
BACKUP_INTERVAL_SEC=2
STATE_FILE=data/test_state.db
STALE_SEC=3
OFFLINE_SEC=6
AUTH_TOKEN=testsecret
HTTP_API_PORT=8788
EOF

# 2. Write agent config
TEST_AGENT_CONF="$ROOT_DIR/config/agent_test.conf"
cat > "$TEST_AGENT_CONF" << EOF
MAX_CONNECT_RETRIES=0
RECONNECT_INTERVAL_SEC=2
AUTH_TOKEN=testsecret
EOF

rm -f data/test_state.db

# 3. Start server
echo "Starting monitor_server on port 9991, viewer port 9992, HTTP API port 8788..."
TERM=screen ./monitor_server -port 9991 -vport 9992 -server-config "$TEST_SERVER_CONF" > "$LOG_DIR/server.log" 2>&1 &
echo $! > "$PID_DIR/server.pid"
sleep 2

# Check if server is running
if ! kill -0 $(cat "$PID_DIR/server.pid") 2>/dev/null; then
  echo "Error: Server failed to start!"
  cat "$LOG_DIR/server.log"
  exit 1
fi

# 4. Test wrong auth token
echo "Testing agent with invalid auth token..."
TEST_AGENT_BAD_CONF="$ROOT_DIR/config/agent_bad.conf"
cat > "$TEST_AGENT_BAD_CONF" << EOF
MAX_CONNECT_RETRIES=1
RECONNECT_INTERVAL_SEC=1
AUTH_TOKEN=wrongsecret
EOF

./agent -fg -server 127.0.0.1:9991 -name test-bad -config "$TEST_AGENT_BAD_CONF" -interval 1 > "$LOG_DIR/agent_bad.log" 2>&1 &
BAD_AGENT_PID=$!
sleep 2
kill "$BAD_AGENT_PID" 2>/dev/null || true
rm -f "$TEST_AGENT_BAD_CONF"

if ! grep -q "Auth failed" "$LOG_DIR/agent_bad.log"; then
  echo "Error: Server did not reject agent with bad token!"
  echo "=== Agent Bad Log ==="
  cat "$LOG_DIR/agent_bad.log"
  echo "=== Server Log ==="
  cat "$LOG_DIR/server.log"
  exit 1
fi
echo "✔ Invalid auth token rejected correctly"

# 5. Start valid agent 1
echo "Starting valid agent 'web-test-1'..."
./agent -fg -server 127.0.0.1:9991 -name web-test-1 -config "$TEST_AGENT_CONF" -interval 1 > "$LOG_DIR/agent_1.log" 2>&1 &
echo $! > "$PID_DIR/agent_1.pid"
sleep 2

# Check if agent 1 connected
echo "Querying HTTP API to verify 'web-test-1' is ONLINE..."
API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "web-test-1" ]] || [[ ! "$API_RESP" =~ "OK" ]]; then
  echo "Error: web-test-1 not ONLINE!"
  echo "API Response: $API_RESP"
  exit 1
fi
echo "✔ web-test-1 connected and status is OK"

# 6. Start valid agent 2
echo "Starting valid agent 'db-test-1'..."
./agent -fg -server 127.0.0.1:9991 -name db-test-1 -config "$TEST_AGENT_CONF" -interval 1 > "$LOG_DIR/agent_2.log" 2>&1 &
echo $! > "$PID_DIR/agent_2.pid"
sleep 2

# Check if agent 2 connected
API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "db-test-1" ]]; then
  echo "Error: db-test-1 not found in host list!"
  echo "API Response: $API_RESP"
  exit 1
fi
echo "✔ db-test-1 connected successfully"

# 7. Test STALE state
echo "Suspending agent 'web-test-1' with SIGSTOP to test STALE transition (STALE_SEC=3)..."
kill -STOP $(cat "$PID_DIR/agent_1.pid")

echo "Waiting 7s..."
sleep 7

API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "\"host\":\"web-test-1\"" ]] || [[ ! "$API_RESP" =~ "\"status\":\"STALE\"" ]]; then
  echo "Error: web-test-1 status did not transition to STALE!"
  echo "API Response: $API_RESP"
  exit 1
fi
echo "✔ web-test-1 transitioned to STALE correctly"

# 8. Test OFFLINE state
echo "Waiting another 7s to test OFFLINE transition (OFFLINE_SEC=6)..."
sleep 7

API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "\"host\":\"web-test-1\"" ]] || [[ ! "$API_RESP" =~ "\"status\":\"OFFLINE\"" ]]; then
  echo "Error: web-test-1 status did not transition to OFFLINE!"
  echo "API Response: $API_RESP"
  exit 1
fi
echo "✔ web-test-1 transitioned to OFFLINE correctly"

# 9. Test Reconnect & Recovery
echo "Resuming agent 'web-test-1' with SIGCONT to test recovery..."
kill -CONT $(cat "$PID_DIR/agent_1.pid")
sleep 2

API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "\"host\":\"web-test-1\"" ]] || [[ ! "$API_RESP" =~ "\"status\":\"OK\"" ]]; then
  echo "Error: web-test-1 status did not transition back to OK!"
  echo "API Response: $API_RESP"
  exit 1
fi
echo "✔ web-test-1 recovered to OK correctly"

# 10. Test IP Limit (MAX_AGENTS_PER_IP=3)
echo "Starting 3rd agent 'cache-test-1'..."
./agent -fg -server 127.0.0.1:9991 -name cache-test-1 -config "$TEST_AGENT_CONF" -interval 1 > "$LOG_DIR/agent_3.log" 2>&1 &
echo $! > "$PID_DIR/agent_3.pid"
sleep 2

API_RESP=$(curl -s http://127.0.0.1:8788/api/hosts)
if [[ ! "$API_RESP" =~ "cache-test-1" ]]; then
  echo "Error: cache-test-1 did not connect!"
  echo "API Response: $API_RESP"
  exit 1
fi

echo "Starting 4th agent 'worker-test-1' (expect rejection due to IP limit)..."
./agent -fg -server 127.0.0.1:9991 -name worker-test-1 -config "$TEST_AGENT_CONF" -interval 1 > "$LOG_DIR/agent_4.log" 2>&1 &
AGENT_4_PID=$!
sleep 2
kill "$AGENT_4_PID" 2>/dev/null || true

if ! grep -q "ip_limit" "$LOG_DIR/agent_4.log"; then
  echo "Error: Server did not reject 4th agent due to IP limit!"
  cat "$LOG_DIR/agent_4.log"
  exit 1
fi
echo "✔ 4th agent rejected correctly due to IP limit"

echo ""
echo "=== ALL INTEGRATION TESTS PASSED! ==="
echo ""
