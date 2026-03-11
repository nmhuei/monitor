# Distributed System Monitor

A **btop++-style** distributed monitoring tool written in **C++20** — real-time ncurses dashboard, multi-host, zero external deps beyond ncurses.

---

## Quick Start

```bash
# 1. Build
./build.sh

# 2. Start server (opens dashboard in your terminal)
./monitor_server

# 3. Connect agents (on each machine to monitor)
./agent -server 127.0.0.1:8784 -name web-1

# 4. (optional) Open interactive viewer from another terminal
./viewer_cli -server 127.0.0.1:8785
```

Or run the demo script to open everything at once:

```bash
./run_demo_terminals.sh              # 2 agents, opens 3 terminals
./run_demo_terminals.sh --agents 4 --stale 8  # custom
```

---

## Terminal Size Requirement

**Minimum: 60 × 15** (computed from layout constants). If the terminal is smaller, the dashboard shows a size warning:

```
 Terminal size too small 
  Width = 52  Height = 12

 Needed for current config:
  Width = 60  Height = 15
```

Recommended: **120 × 35+** for the best layout (all panels, table, and log visible at once).

---

## Project Structure

```
monitor/
├── monitor_server          # Server binary (runs the ncurses dashboard)
├── agent                   # Agent binary (deploy on each machine to watch)
├── viewer_cli              # Interactive remote viewer
├── build.sh                # Build script
├── run_demo_terminals.sh   # Demo: opens server + viewer + logs in 3 terminals
│
├── config/
│   ├── server.conf         # Server: IP limits, state file, stale/offline thresholds
│   ├── thresholds.conf     # Alert thresholds (global + per-host)
│   └── agent.conf          # Agent: retry policy, auth token
│
├── include/
│   ├── protocol.hpp        # Shared types & constants (HostStatus, ports, etc.)
│   ├── metrics_store.hpp   # Thread-safe metric store; hostsJson/historyJson/logJson
│   ├── metrics_collector.hpp  # /proc/stat, /proc/meminfo, statvfs, /proc/net/dev
│   ├── dashboard.hpp       # Full btop++-style ncurses dashboard (6 themes)
│   ├── net_framing.hpp     # TCP framing: [4-byte len][JSON payload]
│   ├── json_helper.hpp     # Lightweight JSON encoder/decoder
│   ├── thresholds.hpp      # Config loader (global + per-host overrides)
│   └── ansi_viewer.hpp     # ANSI renderer for nc/telnet viewers
│
└── src/
    ├── server/monitor_server.cpp   # Accept loop, stale checker, viewer handler
    ├── agent/agent.cpp             # Metric collection, auto-reconnect, daemon mode
    └── viewer/viewer_cli.cpp       # ncurses viewer; CMD query protocol
```

---

## Build

### Requirements

```bash
# Debian / Ubuntu
sudo apt install g++ libncurses-dev

# Fedora / RHEL
sudo dnf install gcc-c++ ncurses-devel

# Arch Linux
sudo pacman -S gcc ncurses
```

Requires **GCC 10+ / Clang 12+** (C++20). Agent runs Linux-only (reads `/proc`). Server can run anywhere with ncurses.

### Build options

```bash
# Option 1: build script
./build.sh

# Option 2: CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Server

```bash
./monitor_server [options]
```

| Flag | Description | Default |
|------|-------------|---------|
| `-port N` | Agent listen port | `8784` |
| `-vport N` | Viewer listen port | `8785` |
| `-config path` | Thresholds config | `config/thresholds.conf` |
| `-server-config path` | Server runtime config | `config/server.conf` |

### `config/server.conf`

```ini
MAX_AGENTS_PER_IP=2       # Max concurrent agents per source IP
BACKUP_INTERVAL_SEC=10    # State save interval
STATE_FILE=data/monitor_state.db

STALE_SEC=30              # Seconds without metric → STALE
OFFLINE_SEC=90            # Seconds without metric → OFFLINE

#AUTH_TOKEN=changeme      # Shared secret (empty = open)
```

> After a restart, hosts are restored as **STALE** (not ONLINE) until they send a new metric.

---

## Agent

```bash
./agent [options]
```

| Flag | Description | Default |
|------|-------------|---------|
| `-server host:port` | Server address | `127.0.0.1:8784` |
| `-name hostname` | Display name | hostname |
| `-interval N` | Send interval (seconds) | `2` |
| `-disk path` | Disk path to monitor | `/` |
| `-config path` | Agent config | `config/agent.conf` |
| `-fg` | Run in foreground (default: daemon) | off |

### `config/agent.conf`

```ini
MAX_CONNECT_RETRIES=12    # 0 = retry forever
RECONNECT_INTERVAL_SEC=5
#AUTH_TOKEN=changeme
```

### Metrics collected

| Metric | Source |
|--------|--------|
| CPU % (total + per-core) | `/proc/stat` (async sampler) |
| RAM % | `/proc/meminfo` |
| Disk % | `statvfs()` |
| Load avg (1m) | `/proc/loadavg` |
| Process count | `/proc/loadavg` |
| Net RX/TX KB/s | `/proc/net/dev` (loopback excluded) |

---

## Viewer CLI

```bash
./viewer_cli -server 127.0.0.1:8785
```

Press `/` to enter a command:

| Command | Description |
|---------|-------------|
| `/hosts` | List all hosts with status, CPU, RAM, Disk, Load |
| `/history <host> [n]` | Last n metric samples (default 30) |
| `/log [n]` | Last n event log entries (default 50) |
| `/help` | Show help |
| `/clear` | Clear output |
| `q` | Quit |

---

## Dashboard Controls

| Key | Action |
|-----|--------|
| `Q` | Quit |
| `Tab` | Enter host detail view / next host |
| `Shift+Tab` | Previous host |
| `Esc` | Back to overview |
| `↑` / `↓` | Scroll log or history |
| `PgUp` / `PgDn` | Scroll fast |
| `U` / `Ctrl+U` | Cycle theme (6 themes) |
| `/` | Open command bar |

### Dashboard commands (`/`)

| Command | Action |
|---------|--------|
| `/help` | Help overlay |
| `/viewer <host>` | Jump to host detail |
| `/history <host>` | Switch to history view for host |

> Partial host name matching supported: `/viewer web` matches `web-1`

---

## Host Status

| Symbol | Status | Meaning |
|--------|--------|---------|
| `● OK` | ONLINE | Receiving metrics, all below threshold |
| `◐ WARN` | WARNING | At least one metric near threshold (≥80% of limit) |
| `● ALERT` | ALERT | At least one metric exceeds threshold |
| `◌ STALE` | STALE | No metric received for `STALE_SEC` seconds |
| `○ OFFLINE` | OFFLINE | No metric received for `OFFLINE_SEC` seconds |

Hosts are sorted by severity: **ALERT → WARNING → STALE → ONLINE → OFFLINE**.

---

## Thresholds

Edit `config/thresholds.conf`:

```ini
# Global defaults (%)
CPU=80
RAM=90
DISK=85

# Per-host overrides
web-1.cpu=85
db-server.ram=95
```

---

## UI Themes

Press `U` to cycle. Requires 256-color terminal.

| # | Name | Style |
|---|------|-------|
| 1 | **BLOODLINE** | Crimson + electric cyan, dark hacker |
| 2 | **MOCHA** | Catppuccin Mocha, warm mauve pastels |
| 3 | **NORD** | Arctic blues, aurora accents |
| 4 | **DRACULA** | Deep purple, vivid pink/green/cyan |
| 5 | **MATRIX** | Monochrome lime green cascade |
| 6 | **CYBERPUNK** | Neon pink + acid yellow + electric cyan |

---

## Architecture

```
┌────────────────────────────────────────────────┐
│               monitor_server                    │
│                                                 │
│  acceptLoop()  ──▶  handleClient()  (1/agent)   │
│                      JSON decode                │
│                      MetricsStore::upsert()     │
│                                                 │
│  staleCheckerLoop()  (every 5s)                 │
│    MetricsStore::markStaleOffline()             │
│                                                 │
│  persistLoop()  ──▶  saveToFile() (every 10s)  │
│                                                 │
│  renderLoop()   ──▶  Dashboard::render()        │
│                      ncurses (main thread)      │
│                                                 │
│  viewerAcceptLoop() ──▶ viewerHandler()         │
│    CMD hosts / history / log  →  JSON           │
└────────────────────────────────────────────────┘
          ▲ TCP [4-byte len][JSON]
          │
 ┌────────┴──────────┐   ┌─────────────────────┐
 │  agent (web-1)    │   │  agent (db-server)   │
 │  AsyncCpuSampler  │   │  /proc/stat          │
 │  /proc/meminfo    │   │  /proc/meminfo       │
 │  statvfs / netdev │   │  statvfs / netdev    │
 └───────────────────┘   └─────────────────────┘
```

---

## Security

Set a shared secret in both `config/server.conf` and `config/agent.conf`:

```ini
AUTH_TOKEN=your_strong_secret_here
```

Agents without the correct token are immediately rejected. For internet-facing deployments, additionally put the port behind a VPN (WireGuard, Tailscale) or SSH tunnel — TLS is not included.

---

## Demo Scripts

```bash
./run_demo_terminals.sh                        # 2 agents, 3 terminals
./run_demo_terminals.sh --agents 4             # 4 agents
./run_demo_terminals.sh --stale 8 --offline 20 # fast stale testing
./run_demo_terminals.sh --help                 # all options
./stop_agents.sh                               # kill background agents
```
