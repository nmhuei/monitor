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

# ── Find ncurses (try wide-char ncursesw first, fall back to ncurses) ─────────
find_package(Curses QUIET)
if(NOT CURSES_FOUND)
    # Some distros split ncursesw into a separate package
    find_library(NCURSESW_LIB ncursesw)
    find_path(NCURSESW_INCLUDE ncursesw/curses.h)
    if(NCURSESW_LIB AND NCURSESW_INCLUDE)
        set(CURSES_LIBRARIES ${NCURSESW_LIB})
        set(CURSES_INCLUDE_DIRS ${NCURSESW_INCLUDE})
        set(CURSES_FOUND TRUE)
    else()
        message(FATAL_ERROR "Could not find ncurses or ncursesw. Install libncurses-dev or libncursesw5-dev.")
    endif()
endif()

# Check if ncursesw/curses.h header is available (for wide-char support)
find_path(NCURSESW_HEADER_DIR ncursesw/curses.h)
if(NCURSESW_HEADER_DIR)
    add_compile_definitions(HAVE_NCURSESW)
message(STATUS "Found ncursesw/curses.h — wide char support enabled")
else()
    message(STATUS "Using fallback curses.h")
endif()

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

# ── viewer_cli ────────────────────────────────────────────────────────────────
add_executable(viewer_cli
    src/viewer/viewer_cli.cpp
)
target_link_libraries(viewer_cli
    ${CURSES_LIBRARIES}
    pthread
)

# ── Output binaries to project root ──────────────────────────────────────────
set_target_properties(monitor_server agent viewer_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Copy config if not present
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/config/thresholds.conf)
    file(COPY config/thresholds.conf DESTINATION ${CMAKE_SOURCE_DIR}/config/)
endif()
