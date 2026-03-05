#!/usr/bin/env bash
# build.sh — Build monitor_server and agent
set -e

echo "=== Distributed System Monitor — Build Script ==="
echo ""

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -O2 -Wall -pthread"
LDFLAGS_SERVER="-lncursesw -Wl,-rpath,/usr/lib/x86_64-linux-gnu"
LDFLAGS_AGENT="-pthread"

# Detect ncurses include location
NCURSES_INC=""
for d in /usr/include/ncursesw /usr/include /usr/include/ncurses /usr/local/include; do
    if [ -f "$d/curses.h" ] || [ -f "$d/ncurses.h" ]; then
        NCURSES_INC="-I$d"
        break
    fi
done

# Detect libtinfo (may be needed on some distros)
TINFO_LIB=""
if $CXX -ltinfo -x c++ /dev/null -o /dev/null 2>/dev/null; then
    TINFO_LIB="-ltinfo"
elif [ -f "/usr/lib/x86_64-linux-gnu/libtinfo.so.6" ]; then
    TINFO_LIB="/usr/lib/x86_64-linux-gnu/libtinfo.so.6"
fi

echo "[1/2] Compiling agent..."
$CXX $CXXFLAGS -Iinclude \
    src/agent/agent.cpp \
    -o agent \
    $LDFLAGS_AGENT
echo "      -> agent  ✓"

echo "[2/2] Compiling monitor_server..."
$CXX $CXXFLAGS -Iinclude $NCURSES_INC \
    src/server/monitor_server.cpp \
    -o monitor_server \
    $LDFLAGS_SERVER $TINFO_LIB
echo "      -> monitor_server  ✓"

echo ""
echo "Build complete!"
echo ""
echo "Run the server:"
echo "  ./monitor_server [-port 8784] [-config config/thresholds.conf]"
echo ""
echo "Run an agent (on any Linux machine):"
echo "  ./agent -server <server-ip>:8784 -interval 5 -name web-1"
echo ""
echo "Controls in the dashboard:"
echo "  Q          — quit"
echo "  ↑ ↓        — scroll log"
echo "  PgUp PgDn  — scroll log fast"
