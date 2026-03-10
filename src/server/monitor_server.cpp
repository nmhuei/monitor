/*
 * monitor_server.cpp
 *
 * Accepts agent connections, collects metrics, renders btop++-style dashboard.
 *
 * Usage: ./monitor_server [-port 8784] [-config config/thresholds.conf]
 *                         [-server-config config/server.conf]
 */
#include "../include/ansi_viewer.hpp"
#include "../include/dashboard.hpp"
#include "../include/json_helper.hpp"
#include "../include/metrics_store.hpp"
#include "../include/net_framing.hpp"
#include "../include/protocol.hpp"
#include "../include/thresholds.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

using namespace monitor;

// ── Globals ──────────────────────────────────────────────────────────────────
static MetricsStore g_store;
static Thresholds g_thresh;
static std::atomic<bool> g_running{true};

// Map fd → host name (protected by mutex)
static std::mutex g_fdMtx;
static std::unordered_map<int, std::string> g_fdHost;
static std::unordered_map<int, std::string> g_fdIP;

struct ServerConfig {
  int maxAgentsPerIP = 2;
  int backupIntervalSec = 10;
  std::string stateFile = "data/monitor_state.db";
};

static ServerConfig g_cfg;

static std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

static ServerConfig loadServerConfig(const std::string &path) {
  ServerConfig cfg;
  std::ifstream in(path);
  if (!in)
    return cfg;

  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));

    try {
      if (k == "MAX_AGENTS_PER_IP")
        cfg.maxAgentsPerIP = std::max(1, std::stoi(v));
      else if (k == "BACKUP_INTERVAL_SEC")
        cfg.backupIntervalSec = std::max(1, std::stoi(v));
      else if (k == "STATE_FILE")
        cfg.stateFile = v;
    } catch (...) {
      // ignore malformed value
    }
  }
  return cfg;
}

// ── Client handler thread ────────────────────────────────────────────────────
static void handleClient(int fd, std::string ip) {
  // First message must contain host name
  std::string hostName;

  while (g_running) {
    std::string msg = net::recvMsg(fd);
    if (msg.empty())
      break; // disconnected

    try {
      auto obj = json::decode(msg);

      std::string host = obj.count("host") ? obj["host"].str : "unknown";
      float cpu = obj.count("cpu") ? (float)obj["cpu"].num : 0.f;
      float ram = obj.count("ram") ? (float)obj["ram"].num : 0.f;
      float disk = obj.count("disk") ? (float)obj["disk"].num : 0.f;
      time_t ts =
          obj.count("timestamp") ? (time_t)obj["timestamp"].num : time(nullptr);

      if (hostName.empty()) {
        hostName = host;
        std::lock_guard<std::mutex> lk(g_fdMtx);
        g_fdHost[fd] = host;
        g_fdIP[fd] = ip;
        g_store.setOnline(host, ip, fd);
      }

      MetricPayload p;
      p.host = host;
      p.cpu = cpu;
      p.ram = ram;
      p.disk = disk;
      p.timestamp = ts;
      p.ip = ip;

      // Parse per-core CPU data
      if (obj.count("cores") && obj["cores"].is_arr) {
        for (double v : obj["cores"].arr)
          p.cores.push_back((float)v);
      }

      g_store.upsert(p, g_thresh);

    } catch (...) {
      // Malformed JSON — skip
    }
  }

  // Clean up
  {
    std::lock_guard<std::mutex> lk(g_fdMtx);
    if (!hostName.empty())
      g_store.setOffline(hostName);
    g_fdHost.erase(fd);
    g_fdIP.erase(fd);
  }
  close(fd);
}

// ── Render loop (runs in main thread with ncurses)
// ────────────────────────────
static void persistLoop() {
  while (g_running) {
    g_store.saveToFile(g_cfg.stateFile);
    std::this_thread::sleep_for(std::chrono::seconds(g_cfg.backupIntervalSec));
  }
  // final flush
  g_store.saveToFile(g_cfg.stateFile);
}

static void renderLoop(ui::Dashboard &dash) {
  // halfdelay(2) trong dashboard làm getch() chờ 0.2s → 5 frame/giây
  // Data snapshot mỗi giây để không lock quá thường xuyên
  auto lastData = std::chrono::steady_clock::now();
  std::vector<HostState> hosts;
  std::vector<LogEvent> log;

  while (g_running) {
    auto now = std::chrono::steady_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData)
            .count();
    if (ms >= 1000) {
      hosts = g_store.snapshot();
      log = g_store.logSnapshot();
      lastData = now;
    }
    dash.render(hosts, log, g_thresh);
    // Không sleep thêm — halfdelay đã throttle
  }
}

// ── Accept loop (runs in background thread)
// ───────────────────────────────────
static void acceptLoop(int serverFd) {
  while (g_running) {
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    int fd = accept(serverFd, (sockaddr *)&addr, &addrLen);
    if (fd < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      break;
    }
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
    std::string ip(ipBuf);

    bool reject = false;
    {
      std::lock_guard<std::mutex> lk(g_fdMtx);
      int count = 0;
      for (const auto &[_, v] : g_fdIP)
        if (v == ip)
          count++;
      if (count >= g_cfg.maxAgentsPerIP)
        reject = true;
      else
        g_fdIP[fd] = ip; // reserve slot immediately
    }

    if (reject) {
      std::string msg = "{\"error\":\"ip_limit\",\"detail\":\"max agents per ip reached\"}";
      net::sendMsg(fd, msg);
      close(fd);
      continue;
    }

    std::thread(handleClient, fd, ip).detach();
  }
}

// ── Viewer handler (ANSI stream to nc client) ─────────────────────────────
static void viewerHandler(int fd) {
  while (g_running) {
    auto hosts = g_store.snapshot();
    std::string frame = viewer::renderFrame(hosts, g_thresh);
    ssize_t sent = write(fd, frame.c_str(), frame.size());
    if (sent <= 0)
      break; // viewer disconnected
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  close(fd);
}

static void viewerAcceptLoop(int vfd) {
  while (g_running) {
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);
    int fd = accept(vfd, (sockaddr *)&addr, &addrLen);
    if (fd < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      break;
    }
    std::thread(viewerHandler, fd).detach();
  }
}

static int createListenSocket(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 32) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

// ── main
// ──────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
  uint16_t port = DEFAULT_PORT;
  uint16_t vport = 0; // viewer port (0 = auto = port+1)
  std::string cfgPath = "config/thresholds.conf";
  std::string serverCfgPath = "config/server.conf";

  for (int i = 1; i < argc - 1; i++) {
    std::string arg(argv[i]);
    if (arg == "-port")
      port = (uint16_t)std::stoi(argv[i + 1]);
    if (arg == "-vport")
      vport = (uint16_t)std::stoi(argv[i + 1]);
    if (arg == "-config")
      cfgPath = argv[i + 1];
    if (arg == "-server-config")
      serverCfgPath = argv[i + 1];
  }
  if (vport == 0)
    vport = port + 1;

  g_thresh = loadThresholds(cfgPath);
  g_cfg = loadServerConfig(serverCfgPath);
  try {
    std::filesystem::path p(g_cfg.stateFile);
    if (p.has_parent_path())
      std::filesystem::create_directories(p.parent_path());
  } catch (...) {
  }
  g_store.loadFromFile(g_cfg.stateFile); // best-effort restore

  signal(SIGPIPE, SIG_IGN);

  // Create agent socket
  int serverFd = createListenSocket(port);
  if (serverFd < 0) {
    perror("agent socket");
    return 1;
  }

  // Create viewer socket
  int viewerFd = createListenSocket(vport);
  if (viewerFd < 0) {
    perror("viewer socket");
    return 1;
  }

  // Init dashboard (ncurses)
  ui::Dashboard dash;
  dash.init();

  // Start agent accept thread
  std::thread(acceptLoop, serverFd).detach();

  // Start viewer accept thread
  std::thread(viewerAcceptLoop, viewerFd).detach();

  // Start state persistence thread
  std::thread(persistLoop).detach();

  // Render loop (blocks in main thread)
  renderLoop(dash);

  close(serverFd);
  close(viewerFd);
  return 0;
}
