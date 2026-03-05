/*
 * monitor_server.cpp
 *
 * Accepts agent connections, collects metrics, renders btop++-style dashboard.
 *
 * Usage: ./monitor_server [-port 8784] [-config config/thresholds.conf]
 */
#include "../include/protocol.hpp"
#include "../include/metrics_store.hpp"
#include "../include/net_framing.hpp"
#include "../include/json_helper.hpp"
#include "../include/thresholds.hpp"
#include "../include/dashboard.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <iostream>
#include <cstring>

using namespace monitor;

// ── Globals ──────────────────────────────────────────────────────────────────
static MetricsStore  g_store;
static Thresholds    g_thresh;
static std::atomic<bool> g_running{true};

// Map fd → host name (protected by mutex)
static std::mutex                          g_fdMtx;
static std::unordered_map<int,std::string> g_fdHost;
static std::unordered_map<int,std::string> g_fdIP;

// ── Client handler thread ────────────────────────────────────────────────────
static void handleClient(int fd, std::string ip) {
    // First message must contain host name
    std::string hostName;

    while (g_running) {
        std::string msg = net::recvMsg(fd);
        if (msg.empty()) break; // disconnected

        try {
            auto obj = json::decode(msg);

            std::string host = obj.count("host") ? obj["host"].str : "unknown";
            float cpu  = obj.count("cpu")  ? (float)obj["cpu"].num  : 0.f;
            float ram  = obj.count("ram")  ? (float)obj["ram"].num  : 0.f;
            float disk = obj.count("disk") ? (float)obj["disk"].num : 0.f;
            time_t ts  = obj.count("timestamp")
                         ? (time_t)obj["timestamp"].num : time(nullptr);

            if (hostName.empty()) {
                hostName = host;
                std::lock_guard<std::mutex> lk(g_fdMtx);
                g_fdHost[fd] = host;
                g_fdIP[fd]   = ip;
                g_store.setOnline(host, ip, fd);
            }

            MetricPayload p;
            p.host = host; p.cpu = cpu; p.ram = ram;
            p.disk = disk; p.timestamp = ts; p.ip = ip;
            g_store.upsert(p, g_thresh);

        } catch (...) {
            // Malformed JSON — skip
        }
    }

    // Clean up
    {
        std::lock_guard<std::mutex> lk(g_fdMtx);
        if (!hostName.empty()) g_store.setOffline(hostName);
        g_fdHost.erase(fd);
        g_fdIP.erase(fd);
    }
    close(fd);
}

// ── Render loop (runs in main thread with ncurses) ────────────────────────────
static void renderLoop(ui::Dashboard& dash) {
    // halfdelay(2) trong dashboard làm getch() chờ 0.2s → 5 frame/giây
    // Data snapshot mỗi giây để không lock quá thường xuyên
    auto lastData = std::chrono::steady_clock::now();
    std::vector<HostState> hosts;
    std::vector<LogEvent>  log;

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - lastData).count();
        if (ms >= 1000) {
            hosts    = g_store.snapshot();
            log      = g_store.logSnapshot();
            lastData = now;
        }
        dash.render(hosts, log, g_thresh);
        // Không sleep thêm — halfdelay đã throttle
    }
}

// ── Accept loop (runs in background thread) ───────────────────────────────────
static void acceptLoop(int serverFd) {
    while (g_running) {
        sockaddr_in addr{};
        socklen_t   addrLen = sizeof(addr);
        int fd = accept(serverFd, (sockaddr*)&addr, &addrLen);
        if (fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
        std::thread(handleClient, fd, std::string(ipBuf)).detach();
    }
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    uint16_t port       = DEFAULT_PORT;
    std::string cfgPath = "config/thresholds.conf";

    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "-port")   port    = (uint16_t)std::stoi(argv[i+1]);
        if (std::string(argv[i]) == "-config") cfgPath = argv[i+1];
    }

    g_thresh = loadThresholds(cfgPath);

    // Create server socket
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(serverFd); return 1;
    }
    if (listen(serverFd, 32) < 0) {
        perror("listen"); close(serverFd); return 1;
    }

    signal(SIGPIPE, SIG_IGN); // ignore broken pipe

    // Init dashboard (ncurses)
    ui::Dashboard dash;
    dash.init();

    // Start accept thread
    std::thread acceptThread(acceptLoop, serverFd);
    acceptThread.detach();

    // Render loop (blocks in main thread)
    renderLoop(dash);

    close(serverFd);
    return 0;
}
