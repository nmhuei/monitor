# 🖥️ Distributed System Monitor

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square&logo=cplusplus)](https://en.cppreference.com/w/cpp/compiler_support/20)
[![Platform](https://img.shields.io/badge/Platform-Linux-orange.svg?style=flat-square&logo=linux)](https://www.linux.org)
[![UI](https://img.shields.io/badge/UI-ncursesw-green.svg?style=flat-square)](https://invisible-island.net/ncurses/)
[![Build](https://img.shields.io/badge/Build-CMake%20%7C%20Bash-brightgreen.svg?style=flat-square)](#build)
[![Tests](https://img.shields.io/badge/Tests-Passed%20(34%20%7C%2032)-brightgreen.svg?style=flat-square)](#testing-suite)

A high-performance **btop++-inspired Ops Console TUI** and sharded monitoring system written in **C++20**. It collects and aggregates real-time system metrics (CPU, RAM, Disk, Load, Network speeds, and per-core utilization) from remote agents and renders them in a responsive, modular terminal dashboard.

---

## ✨ Key Features

*   **🎮 Ops Console TUI**: Re-architected terminal dashboard focusing on fast navigation, multi-node status filtering, and diagnostic drill-downs rather than cramming everything in one view.
*   **📐 Responsive Layout Engine**: Fluid interface adjusting across three breakpoint states:
    *   **Compact (< 100 cols)**: Fullscreen host table view optimized for tiny terminals.
    *   **Standard (100–139 cols)**: Split-screen displaying the Host List (35%) and live Metrics Card Detail (65%), plus a preview log.
    *   **Wide (>= 140 cols)**: Multi-pane display containing a **Groups Sidebar** (categorizing hosts by status, role, env, and cluster), the central Host Table (52%), and the Metric Card Detail (30%).
*   **🛠️ Game-Engine Style UI Pipeline**: Uses a decoupled, object-oriented rendering loop (**Stateless Widgets ➔ Screen Router ➔ Focus Panes ➔ Input Router**).
*   **🔎 Command Palette & Autocomplete**: Press `/` to enter commands. Supports active suggestion boxes for hostnames, themes, sort parameters, and labels.
*   **🏷️ Dynamic Metadata Grouping**: Parses Prometheus/Grafana-style labels (`role`, `env`, `cluster`) heuristically from hostnames (e.g. `web-prod-hanoi-01`) for advanced TUI filtering.
*   **🚨 Alert Webhooks**: Immediate webhook triggers (Slack/Discord) on `ALERT`, `STALE`, and `OFFLINE` status changes.
*   **📊 Prometheus Metrics Scraper**: Native HTTP/1.1 endpoint `/metrics` to scrape state directly into Prometheus & Grafana.

---

## 🚀 Quick Start

```bash
# 1. Build all binaries
./build.sh

# 2. Start the server (launches the ncurses Ops Console TUI)
./monitor_server

# 3. Connect agents from target machines to monitor
./agent -server 127.0.0.1:8784 -name web-prod-01 -interval 2

# 4. (Optional) Run the E2E interactive demo simulator
# Spawns mock agents, feeds metrics, and spins up terminal view
./run_demo_terminals.sh --agents 3
```

---

## 🎮 Keyboard Controls

### Global Keys
*   `q` / `Q` — Exit application.
*   `/` — Open Command Prompt.
*   `f` / `F` — Open Command Prompt pre-filled with `/filter `.
*   `u` / `U` — Cycle through 6 color themes (`BLOODLINE`, `MOCHA`, `NORD`, `DRACULA`, `MATRIX`, `CYBERPUNK`).
*   `Esc` / `Backspace` — Return to the main Overview screen.

### Overview Screen
*   `↑ / ↓` or `k / j` — Scroll through the host table.
*   `Tab` — Shift focus pane (`Host Table` ➔ `Events Preview` ➔ `Groups Sidebar` ➔ `Host Table`).
*   `Enter` — Drill down into the selected host details.
*   `s / S` — Cycle table sort keys (Status, Host, CPU, RAM, Disk, Load, Age).

### Host Detail Screen (Drill-down)
*   `← / →` — Cycle through metrics tabs (`Metrics graphs` ➔ `CPU Cores grid` ➔ `History table` ➔ `Incident logs`).
*   `Tab` / `Shift-Tab` — Zoom and switch focus to the next/previous host while retaining the active metrics tab.
*   `Esc` — Return to the host table.

---

## 📂 Project Directory Structure

```text
monitor/
├── monitor_server          # Server binary (ingestor & dashboard TUI)
├── agent                   # Metric collection agent (Linux-only daemon)
├── viewer_cli              # Remote query shell client
├── build.sh                # Optimized native compilation script
├── CMakeLists.txt          # Cross-platform CMake description
│
├── include/
│   ├── dashboard.hpp       # TUI Screen Router & Input Router engine
│   ├── protocol.hpp        # Framing headers, status codes, and constants
│   ├── metrics_store.hpp   # Sharded thread-safe metrics database
│   ├── metrics_collector.hpp # Linux /proc parser
│   ├── thresholds.hpp      # Threshold parser & overrides evaluator
│   ├── alerting.hpp        # cURL Slack/Discord webhook dispatcher
│   │
│   └── ui/                 # Modular terminal user interface files
│       ├── ui_types.hpp    # Rect, Size, and DashboardState definitions
│       ├── theme.hpp       # Theme colors and status glyph definitions
│       ├── format.hpp      # Padding, data rate, and Braille sparkline utilities
│       ├── layout.hpp      # Compact / Standard / Wide layout calculator
│       ├── command_registry.hpp # Command palette syntax & tab autocompletes
│       │
│       ├── widgets/        # Isolated rendering components
│       │   ├── panel.hpp   # Outer container frames
│       │   ├── sparkline.hpp # Sparkline chart renderer
│       │   ├── top_bar.hpp # Summary stats & clocks
│       │   ├── status_bar.hpp # Context shortcut keys & command prompts
│       │   ├── host_table.hpp # Sortable host details table
│       │   ├── event_list.hpp # Chronological event listings
│       │   ├── metric_card.hpp # Tab content drawer (graphs, cores, history)
│       │   └── help_view.hpp  # Shortcut reference sheets
│       │
│       └── screens/        # Screen assemblies
│           ├── iscreen.hpp # Polymorphic screen interface
│           ├── overview_screen.hpp # Main dashboard (with Groups sidebar)
│           ├── host_detail_screen.hpp # Full-screen host diagnostic tabs
│           ├── events_screen.hpp # Full-screen event stream log
│           └── help_screen.hpp  # Full-screen manual reference screen
│
└── src/
    ├── server/             # monitor_server sources
    ├── agent/              # agent sources
    └── viewer/             # viewer_cli sources
```

---

## ⚙️ Configuration Guides

### 1. Server Configuration (`config/server.conf`)

```ini
MAX_AGENTS_PER_IP=3       # Prevent client metric flooding
BACKUP_INTERVAL_SEC=10    # Writes in-memory database to disk backup
STATE_FILE=data/monitor_state.db # Backup recovery location

STALE_SEC=30              # Stale status threshold
OFFLINE_SEC=90            # Offline status threshold

# ALERT_WEBHOOK_URL=https://hooks.slack.com/services/XYZ # Webhook destination
ALERT_COOLDOWN_SEC=300    # Prevent alert spamming (5m cooldown)
HTTP_API_PORT=8786        # HTTP API listener port
```

### 2. Thresholds Configuration (`config/thresholds.conf`)

Set global thresholds or define per-host overrides. If a metrics value exceeds the threshold, the host enters `ALERT` (or `WARNING` if it reaches $\ge 85\%$ of the limit).

```ini
# Global limits (%)
CPU=80
RAM=90
DISK=85

# Per-host overrides (case-insensitive)
web-server-01.cpu=75
db-master-hanoi.ram=95
```

---

## 📊 Integrations & Remote Access

### 1. HTTP API Endpoints

The server hosts a high-performance HTTP server at `HTTP_API_PORT` (default `8786`):

*   `GET /api/hosts` — List all hosts, IPs, resource usages, and statuses in JSON.
*   `GET /api/stats` — Server statistics (uptime, alerts sent, message rates).
*   `GET /api/log` — Recent monitoring event logs.
*   `GET /api/history/<host>` — JSON history samples for a given host name.
*   `GET /metrics` — Prometheus gauge exports.
*   `GET /healthz` — Server health state.

---

## 🧪 Testing Suite

### 1. C++ Unit Tests (`test/unit_test.cpp`)

To compile and run the 34 native unit tests testing framing, parser escapes, configs, UI buffers, and concurrent store accesses:
```bash
g++ -std=c++20 -O2 -Wall -Iinclude test/unit_test.cpp -o test/unit_test -lncursesw
./test/unit_test
```

### 2. E2E Integration Tests (`test/integration_test_full.sh`)

Executes 32 E2E scenarios, checking agent handshakes, auth rejections, transition states, database save/load cycles, and fast-interval stress scenarios:
```bash
./test/integration_test_full.sh
```
