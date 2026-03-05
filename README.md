# Distributed System Monitor

A production-quality, **btop++-style** distributed monitoring tool written in **C++20**.

Monitors CPU, RAM, and Disk from multiple remote machines in a real-time ncurses terminal dashboard.

---

## Screenshot

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  15:32:01     ◈ DISTRIBUTED SYSTEM MONITOR  ◈          [Q] Quit [↑↓] Scroll ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ CPU % ──────────────────║─ RAM % ──────────────────║─ DISK % ─────────────║
║▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇  ║▂▃▄▄▅▅▆▇▇▇▇▇▇▇▇▇▇▇▇█     ║▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄║
║                   72.4%  ║                   88.1%  ║                45.0% ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ HOST TABLE                                                                   ║
╠──────────────────────────────────────────────────────────────────────────────║
║ HOST             CPU               %    RAM               %    DISK      STATUS║
╠──────────────────────────────────────────────────────────────────────────────║
║ web-1       ██████████████░░  87%  ████████░░░░░░ 62%  ████░░  45% ● ALERT ║
║ db-server   ████░░░░░░░░░░░░  23%  ██████████████ 91%  ██████  67% ◐ WARN  ║
║ worker-1    ██████░░░░░░░░░░  45%  ██████░░░░░░░░ 55%  ███░░░  38% ● OK   ║
║ redis-1     --- OFFLINE ---                                         OFFLINE  ║
╠══════════════════ CONNECTION LOG  [↑↓/PgUp/PgDn to scroll] ═════════════════╣
║ 15:32:01  web-1            192.168.1.10  CONNECT                            ║
║ 15:32:03  web-1            192.168.1.10  METRIC     CPU: 87% RAM: 62% DSK:45%║
║ 15:32:05  db-server        192.168.1.11  ALERT      RAM: 91% RAM=91%        ║
║ 15:32:10  redis-1          192.168.1.13  DISCONNECT                         ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

---

## Project Structure

```
monitor/
├── agent                          # Compiled agent binary
├── monitor_server                 # Compiled server binary
├── build.sh                       # Build script
├── CMakeLists.txt                 # CMake build definition
│
├── config/
│   └── thresholds.conf            # Alert thresholds config
│
├── include/
│   ├── protocol.hpp               # Shared constants & types
│   ├── json_helper.hpp            # Lightweight JSON encoder/decoder
│   ├── metrics_collector.hpp      # /proc/stat, /proc/meminfo, statvfs()
│   ├── metrics_store.hpp          # Thread-safe per-host metric store
│   ├── net_framing.hpp            # TCP message framing [len][payload]
│   ├── thresholds.hpp             # Config loader (global + per-host)
│   └── dashboard.hpp              # Full btop++-style ncurses dashboard
│
└── src/
    ├── server/
    │   └── monitor_server.cpp     # Server: accept, dispatch, render
    └── agent/
        └── agent.cpp              # Agent: collect, send, auto-reconnect
```

---

## Prerequisites

**Server (monitor_server):**
- Linux (any distro)
- `libncurses6` (runtime, usually pre-installed)
- `g++` with C++20 support (GCC 10+)

**Agent:**
- Linux only (reads `/proc/stat`, `/proc/meminfo`)
- `g++` with C++20 support

### Install build dependencies

```bash
# Debian / Ubuntu
sudo apt install g++ libncurses-dev

# Fedora / RHEL
sudo dnf install gcc-c++ ncurses-devel

# Arch Linux
sudo pacman -S gcc ncurses
```

---

## Build

### Option 1: build.sh (recommended)

```bash
chmod +x build.sh
./build.sh
```

### Option 2: CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Option 3: Manual

```bash
# Agent
g++ -std=c++20 -O2 -pthread -Iinclude \
    src/agent/agent.cpp -o agent

# Server
g++ -std=c++20 -O2 -pthread -Iinclude \
    src/server/monitor_server.cpp \
    -o monitor_server -lncurses
```

---

## Running

### 1. Start the server

```bash
./monitor_server
# or with options:
./monitor_server -port 8784 -config config/thresholds.conf
```

The dashboard will launch immediately in your terminal.
Press **Q** to quit.

### 2. Start agents (on each machine to monitor)

```bash
# Monitor localhost
./agent -server 127.0.0.1:8784 -interval 5 -name web-1

# Monitor a remote machine (copy agent binary there first)
./agent -server 192.168.1.5:8784 -interval 3 -name db-server

# Specify disk mount point to monitor
./agent -server 192.168.1.5:8784 -interval 5 -name nas-1 -disk /mnt/data
```

**Agent options:**

| Flag | Description | Default |
|------|-------------|---------|
| `-server host:port` | Server address | `127.0.0.1:8784` |
| `-interval N` | Send interval (seconds) | `5` |
| `-name hostname` | Display name in dashboard | `agent` |
| `-disk path` | Disk path to monitor | `/` |

---

## Configuration
t
Edit `config/thresholds.conf`:

```ini
# Global thresholds (percentage)
CPU=80
RAM=90
DISK=85

# Per-host overrides
web-1.cpu=85
web-1.ram=80
db-server.ram=95
db-server.disk=90
```

When a threshold is exceeded:
- The host row turns **red** in the table
- The log panel shows an **ALERT** entry with blinking warning
- The status badge shows `● ALERT`

---

## Network Protocol

TCP with message framing:

```
[4-byte big-endian length][JSON payload]
```

JSON payload:
```json
{
  "host": "web-1",
  "cpu": 87.3,
  "ram": 62.1,
  "disk": 45.0,
  "timestamp": 1710000000
}
```

---

## Dashboard Controls

| Key | Action |
|-----|--------|
| `Q` | Quit |
| `Tab` | Enter host detail view / next host |
| `Shift+Tab` | Previous host |
| `Esc` / `Backspace` | Return to overview |
| `↑` / `↓` | Scroll log (overview) or history (detail) |
| `PgUp` / `PgDn` | Scroll fast |

---

## Host Detail View

Press `Tab` to enter the host detail view. This shows individual resource metrics for the selected host:

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  08:15:32     ◈ HOST DETAIL: web-1 ◈           [Tab]Next [Esc]Back [Q]Quit ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ CPU ────────────────────────────────────────────────────────── 87.3% ─║
║▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁               ║
║▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁▂▃               ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ RAM ────────────────────────────────────────────────────────── 62.1% ─║
║▃▄▄▅▅▆▇▇▇▇▇▇▇▇▇▇▇▇▇█▃▄▄▅▅▆▇▇▇▇▇▇▇▇▇▇▇▇                              ║
║▆▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇█▆▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ DISK ───────────────────────────────────────────────────────── 45.0% ─║
║▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄                            ║
║▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄                            ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ HOST INFO                                                                  ║
║ Name:    web-1                    Status:   ● ALERT                        ║
║ IP:      192.168.1.10             Last Seen: 08:15:30                      ║
║ Thresholds: CPU≥85% RAM≥80% DISK≥85%                                      ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ RECENT HISTORY [1/3 hosts]                                                 ║
║ TIME        CPU%     RAM%     DISK%                                        ║
╠──────────────────────────────────────────────────────────────────────────────║
║ 08:15:30    87.3%    62.1%    45.0%                                        ║
║ 08:15:25    84.1%    61.5%    45.0%                                        ║
║ 08:15:20    79.0%    60.2%    45.0%                                        ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

Use `Tab` / `Shift+Tab` to cycle through hosts, `Esc` to return to overview.

---

## Features

- **Real-time graphs** — sparkline-style CPU/RAM/Disk history (last 60 samples) with block characters
- **Host detail view** — per-host resource graphs, host info, and scrollable history table (Tab to enter)
- **Host table** — progress bars, percentage, color-coded status
- **Connection log** — 500-entry scrollable log of CONNECT / METRIC / ALERT / DISCONNECT events
- **Alert system** — configurable global and per-host thresholds; red highlighting + blinking
- **Auto-reconnect** — agent retries every 5 seconds on disconnect
- **Thread-safe** — server handles unlimited concurrent agents
- **Zero external deps** — only ncurses + POSIX; custom JSON parser included
- **Color scheme** — green (OK) → yellow (warning) → red (alert) → gray (offline)

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   monitor_server                     │
│                                                     │
│  ┌──────────────┐    ┌───────────────────────────┐  │
│  │ Accept Thread │───▶│  handleClient (1 per fd)  │  │
│  └──────────────┘    │  recvMsg → JSON decode     │  │
│                       │  MetricsStore::upsert()   │  │
│  ┌──────────────┐    └───────────────────────────┘  │
│  │ Render Loop  │◀───  MetricsStore (mutex-guarded)  │
│  │ (main thread)│      per-host history + log        │
│  │  ncurses UI  │                                    │
│  └──────────────┘                                    │
└─────────────────────────────────────────────────────┘
           ▲ TCP [len][JSON]
           │
┌──────────┴────────┐   ┌──────────────────────┐
│     agent (web-1) │   │  agent (db-server)    │
│  /proc/stat       │   │  /proc/stat           │
│  /proc/meminfo    │   │  /proc/meminfo        │
│  statvfs("/")     │   │  statvfs("/")         │
└───────────────────┘   └──────────────────────┘
```
