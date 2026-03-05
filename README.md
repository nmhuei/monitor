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
./monitor_server -port 8784 -vport 8785 -config config/thresholds.conf
```

The dashboard will launch immediately in your terminal.

**Server options:**

| Flag | Description | Default |
|------|-------------|---------|
| `-port N` | Agent connection port | `8784` |
| `-vport N` | Remote viewer port | `port + 1` |
| `-config path` | Thresholds config file | `config/thresholds.conf` |

### 2. Remote Viewer (via nc)

Connect from any machine to view a read-only dashboard:

```bash
nc server-ip 8785
```

- Auto-refreshes every 2 seconds
- Shows host table with colored bars, per-core CPU summary
- Multiple viewers can connect simultaneously
- Press `Ctrl+C` to disconnect
Press **Q** to quit.

### 3. Start agents (on each machine to monitor)

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
| `Esc` / `Backspace` | Return to overview / close |
| `↑` / `↓` | Scroll log (overview) or cores/history (detail) |
| `PgUp` / `PgDn` | Scroll fast |
| `/` | Open command bar |

### Commands

Press `/` to open the command bar, then type a command and press `Enter`:

| Command | Action |
|---------|--------|
| `/help` | Show help overlay with all keybindings |
| `/viewer <host>` | Jump directly to host detail view |
| `/history <host>` | Show scrollable history table for host |

> Tip: Commands support partial host name matching (e.g., `/viewer web` matches `web-1`)

---

## Host Detail View

Press `Tab` to enter the host detail view. Shows per-core CPU usage, RAM/Disk sparklines, and host info:

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  08:15:32     ◈ HOST DETAIL: web-1 ◈           [Tab]Next [Esc]Back [Q]Quit ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ CPU OVERVIEW ──────────────────────────────────────────────── 87.3% ─║
║▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁▂▃▄▅▆▇█▇▆▅▄▃▂▁▂▃▅▇██▇▆▅▃▂▁               ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ PROCESSORS (6 cores)                                                       ║
║ core  0  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  53.2%      ║
║ core  1  ██████████████████████████████████░░░░░░░░░░░░░░░░░░░  87.1%      ║
║ core  2  ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  26.4%      ║
║ core  3  ██████████████████████████████████████████████████████  98.5%      ║
║ core  4  ████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  45.0%      ║
║ core  5  ██████████████████████████████░░░░░░░░░░░░░░░░░░░░░░░  65.3%      ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ RAM ─────────────────────────────────────────────────────── 62.1% ───║
║▃▄▄▅▅▆▇▇▇▇▇▇▇▇▇▇▇▇▇█▃▄▄▅▅▆▇▇▇▇▇▇▇▇▇▇▇▇▇                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║─ DISK ────────────────────────────────────────────────────── 45.0% ───║
║▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄                              ║
╠══════════════════════════════════════════════════════════════════════════════╣
║ HOST INFO                                                                  ║
║ Name: web-1              Status: ● ALERT                                   ║
║ IP:   192.168.1.10       Cores: 6       Last: 08:15:30                     ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

Use `Tab` / `Shift+Tab` to cycle through hosts, `↑↓` to scroll cores, `Esc` to return.

---

## Features

- **Real-time graphs** — sparkline-style CPU/RAM/Disk history (last 60 samples) with block characters
- **Host detail view** — per-core CPU bars, RAM/DISK sparklines, host info (Tab to enter)
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
