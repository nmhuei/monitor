cmake_minimum_required(VERSION 3.16)
project(monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ──────────────────────────────────────────────────────────
add_compile_options(
    -O2
    -Wall
    -Wextra
    -pthread
)

# ── Find ncurses ─────────────────────────────────────────────────────────────
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIRS})
include_directories(include)

# ── monitor_server ───────────────────────────────────────────────────────────
add_executable(monitor_server
    src/server/monitor_server.cpp
)
target_link_libraries(monitor_server
    ${CURSES_LIBRARIES}
    pthread
)

# ── agent ────────────────────────────────────────────────────────────────────
add_executable(agent
    src/agent/agent.cpp
)
target_link_libraries(agent
    pthread
)

# ── Output binaries to project root ──────────────────────────────────────────
set_target_properties(monitor_server agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Copy config if not present
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/config/thresholds.conf)
    file(COPY config/thresholds.conf DESTINATION ${CMAKE_SOURCE_DIR}/config/)
endif()
