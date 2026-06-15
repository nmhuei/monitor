#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$ROOT_DIR/.pids"

if [[ -d "$PID_DIR" ]]; then
  for f in "$PID_DIR"/*.pid; do
    if [[ -f "$f" ]]; then
      pid=$(cat "$f" || true)
      if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" || true
        echo "Stopped agent pid=$pid ($(basename "$f"))"
      fi
      rm -f "$f"
    fi
  done
fi
