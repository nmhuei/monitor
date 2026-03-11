/*
 * monitor_server.cpp  v4
 *
 * Fixes vs v3:
 *  - Proper thread lifecycle: joinable threads stored in vector, graceful shutdown
 *  - Race condition fix: g_fdHost/g_fdIP always accessed under g_fdMtx
 *  - Internal server metrics (server_stats.hpp)
 *  - HTTP API + Prometheus endpoint (http_api.hpp) on configurable port
 *  - Protocol version field in agent handshake
 *  - Heartbeat/ping messages handled gracefully
 *  - Alert counter wired to ServerStats
 */
#include "../../include/alerting.hpp"
#include "../../include/ansi_viewer.hpp"
#include "../../include/dashboard.hpp"
#include "../../include/http_api.hpp"
#include "../../include/json_helper.hpp"
#include "../../include/logger.hpp"
#include "../../include/metrics_store.hpp"
#include "../../include/net_framing.hpp"
#include "../../include/protocol.hpp"
#include "../../include/server_stats.hpp"
#include "../../include/thresholds.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace monitor;

// ── Globals ──────────────────────────────────────────────────────────────────
static MetricsStore        g_store;
static Thresholds          g_thresh;
static ServerStats         g_stats;
static std::atomic<bool>   g_running{true};

// fd → host/IP map; always access under g_fdMtx
static std::mutex                          g_fdMtx;
static std::unordered_map<int,std::string> g_fdHost;
static std::unordered_map<int,std::string> g_fdIP;

// Thread registry for graceful shutdown
static std::mutex              g_threadMtx;
static std::vector<std::thread> g_threads;

static Alerter *g_alerter = nullptr;

// ── Config ───────────────────────────────────────────────────────────────────
struct ServerConfig {
  int  maxAgentsPerIP     = 2;
  int  backupIntervalSec  = 10;
  int  staleSec           = DEFAULT_STALE_SEC;
  int  offlineSec         = DEFAULT_OFFLINE_SEC;
  std::string stateFile   = "data/monitor_state.db";
  std::string authToken;
  std::string alertWebhookUrl;
  int  alertCooldownSec   = 300;
  std::string logFile     = "data/monitor.log";
  std::string logLevel    = "INFO";
  std::string historyDir  = "data/history";
  int  historyMaxLines    = 10000;
  uint16_t httpPort       = 8786;   // 0 = disable HTTP API
};
static ServerConfig g_cfg;

// ── Helpers ──────────────────────────────────────────────────────────────────
static std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
  return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

static ServerConfig loadServerConfig(const std::string &path) {
  ServerConfig cfg;
  std::ifstream in(path);
  if (!in) return cfg;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
    try {
      if      (k == "MAX_AGENTS_PER_IP")    cfg.maxAgentsPerIP    = std::max(1, std::stoi(v));
      else if (k == "BACKUP_INTERVAL_SEC")  cfg.backupIntervalSec = std::max(1, std::stoi(v));
      else if (k == "STALE_SEC")            cfg.staleSec          = std::max(5, std::stoi(v));
      else if (k == "OFFLINE_SEC")          cfg.offlineSec        = std::max(10, std::stoi(v));
      else if (k == "STATE_FILE")           cfg.stateFile         = v;
      else if (k == "AUTH_TOKEN")           cfg.authToken         = v;
      else if (k == "ALERT_WEBHOOK_URL")    cfg.alertWebhookUrl   = v;
      else if (k == "ALERT_COOLDOWN_SEC")   cfg.alertCooldownSec  = std::max(0, std::stoi(v));
      else if (k == "LOG_FILE")             cfg.logFile           = v;
      else if (k == "LOG_LEVEL")            cfg.logLevel          = v;
      else if (k == "HISTORY_DIR")          cfg.historyDir        = v;
      else if (k == "HISTORY_MAX_LINES")    cfg.historyMaxLines   = std::max(100, std::stoi(v));
      else if (k == "HTTP_API_PORT")        cfg.httpPort          = (uint16_t)std::max(0, std::stoi(v));
    } catch (const std::exception &ex) {
      LOG_WARN("server.conf: bad value for '" + k + "' = '" + v + "': " + ex.what());
    }
  }
  return cfg;
}

static bool validateStatePath(const std::string &path) {
  try {
    auto cwd = std::filesystem::current_path();
    auto abs = std::filesystem::weakly_canonical(path);
    auto rel = std::filesystem::relative(abs, cwd);
    return rel.string().rfind("..", 0) != 0;
  } catch (...) { return false; }
}

// Spawn a joinable worker thread and register it
static void spawnThread(std::function<void()> fn) {
  std::lock_guard<std::mutex> lk(g_threadMtx);
  g_threads.emplace_back(std::move(fn));
}

// Join all registered threads (call after g_running = false)
static void joinAllThreads() {
  std::lock_guard<std::mutex> lk(g_threadMtx);
  for (auto &t : g_threads)
    if (t.joinable()) t.join();
  g_threads.clear();
}

// ── Client handler ───────────────────────────────────────────────────────────
static void handleClient(int fd, std::string ip) {
  net::setRecvTimeout(fd, RECV_TIMEOUT_SEC);
  std::string hostName;
  bool authenticated = g_cfg.authToken.empty();

  while (g_running) {
    std::string msg = net::recvMsg(fd);
    if (msg.empty()) break;

    try {
      auto obj = json::decode(msg);

      // ── Auth check ──────────────────────────────────────────────────────
      if (!authenticated) {
        bool ok = obj.count("auth") && obj["auth"].str == g_cfg.authToken;
        if (!ok) {
          net::sendMsg(fd, "{\"error\":\"auth_failed\"}");
          LOG_WARN("auth_failed from " + ip);
          g_stats.msgsDropped++;
          close(fd);
          return;
        }
        authenticated = true;
        continue; // auth message only, no metric yet
      }

      // ── Heartbeat / ping ────────────────────────────────────────────────
      if (obj.count("type") && obj["type"].str == "heartbeat") {
        // Just update lastSeen without metric push
        if (!hostName.empty())
          g_store.touchLastSeen(hostName);
        continue;
      }

      // ── Metric payload ──────────────────────────────────────────────────
      std::string host = obj.count("host") ? obj["host"].str : "unknown";
      if (host.empty() || host == "unknown") {
        g_stats.msgsDropped++;
        continue;
      }

      float  cpu      = obj.count("cpu")        ? (float)obj["cpu"].num        : 0.f;
      float  ram      = obj.count("ram")        ? (float)obj["ram"].num        : 0.f;
      float  disk     = obj.count("disk")       ? (float)obj["disk"].num       : 0.f;
      time_t ts       = obj.count("timestamp")  ? (time_t)obj["timestamp"].num : time(nullptr);
      float  netRx    = obj.count("net_rx")     ? (float)obj["net_rx"].num     : 0.f;
      float  netTx    = obj.count("net_tx")     ? (float)obj["net_tx"].num     : 0.f;
      float  loadAvg  = obj.count("load_avg")   ? (float)obj["load_avg"].num   : 0.f;
      int    procCount= obj.count("proc_count") ? (int)obj["proc_count"].num   : 0;

      if (hostName.empty()) {
        hostName = host;
        std::lock_guard<std::mutex> lk(g_fdMtx);
        g_fdHost[fd] = host;
        g_fdIP[fd]   = ip;
        g_store.setOnline(host, ip, fd);
        g_stats.agentsOnline++;
        LOG_INFO("agent connected: " + host + " (" + ip + ")");
      }

      MetricPayload p;
      p.host = host; p.cpu = cpu; p.ram = ram; p.disk = disk;
      p.timestamp = ts; p.ip = ip;
      p.netRxKB = netRx; p.netTxKB = netTx;
      p.loadAvg = loadAvg; p.procCount = procCount;
      if (obj.count("cores") && obj["cores"].is_arr)
        for (double v : obj["cores"].arr)
          p.cores.push_back((float)v);

      g_stats.msgsTotal++;
      auto [prev, cur] = g_store.upsert(p, g_thresh);

      if (g_alerter && g_alerter->maybeAlert(host, prev, cur, cpu, ram, disk))
        g_stats.alertsSent++;

    } catch (...) {
      LOG_WARN("handleClient: malformed JSON from " + ip);
      g_stats.msgsDropped++;
    }
  }

  // Cleanup: remove from fd maps under lock
  {
    std::lock_guard<std::mutex> lk(g_fdMtx);
    if (!hostName.empty()) {
      g_store.setOffline(hostName);
      g_stats.agentsOnline--;
      LOG_INFO("agent disconnected: " + hostName + " (" + ip + ")");
    }
    g_fdHost.erase(fd);
    g_fdIP.erase(fd);
  }
  close(fd);
}

// ── Background loops ─────────────────────────────────────────────────────────
static void persistLoop() {
  while (g_running) {
    g_store.saveToFile(g_cfg.stateFile);
    for (int i = 0; i < g_cfg.backupIntervalSec * 10 && g_running; i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  g_store.saveToFile(g_cfg.stateFile); // final flush
  LOG_INFO("persistLoop: final save complete");
}

static void staleCheckerLoop() {
  while (g_running) {
    auto [online, stale] = g_store.markStaleOffline(g_cfg.staleSec, g_cfg.offlineSec);
    g_stats.agentsOnline = online;
    g_stats.agentsStale  = stale;
    for (int i = 0; i < 50 && g_running; i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

static void renderLoop(ui::Dashboard &dash) {
  auto lastData = std::chrono::steady_clock::now();
  std::vector<HostState> hosts;
  std::vector<LogEvent>  log;
  while (g_running) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count() >= 1000) {
      hosts   = g_store.snapshot();
      log     = g_store.logSnapshot();
      lastData = now;
    }
    dash.render(hosts, log, g_thresh);
    if (!g_running) break;
  }
}

// ── Viewer handler ───────────────────────────────────────────────────────────
static void viewerHandler(int fd) {
  g_stats.viewerConnects++;
  struct timeval tv{}; tv.tv_sec = 2;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  char buf[512]; int n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n > 0) {
    buf[n] = '\0';
    std::string line = buf;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();

    if (line.rfind("CMD", 0) == 0) {
      std::istringstream ss(line.substr(4));
      std::string verb; ss >> verb;
      std::string response;

      if (verb == "hosts") {
        response = g_store.hostsJson() + "\n";
      } else if (verb == "history") {
        std::string host; int cnt = 30;
        ss >> host >> cnt;
        response = host.empty()
          ? "{\"error\":\"missing host\"}\n"
          : g_store.historyJson(host, std::max(1, std::min(cnt, 1000))) + "\n";
      } else if (verb == "log") {
        int cnt = 50; ss >> cnt;
        response = g_store.logJson(std::max(1, std::min(cnt, 1000))) + "\n";
      } else if (verb == "stats") {
        response = g_stats.toJson() + "\n";
      } else {
        response = "{\"error\":\"unknown command\"}\n";
      }
      (void)write(fd, response.c_str(), response.size());
      close(fd);
      return;
    }
  }

  // Legacy push mode: render ASCII frame every 2s
  tv.tv_sec = 0; tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  while (g_running) {
    auto hosts = g_store.snapshot();
    std::string frame = viewer::renderFrame(hosts, g_thresh);
    if (write(fd, frame.c_str(), frame.size()) <= 0) break;
    for (int i = 0; i < 20 && g_running; i++)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  close(fd);
}

// ── Accept loops ─────────────────────────────────────────────────────────────
static int createListenSocket(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);
  if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 32) < 0) {
    close(fd); return -1;
  }
  return fd;
}

static void acceptLoop(int serverFd) {
  while (g_running) {
    sockaddr_in addr{}; socklen_t addrLen = sizeof(addr);
    int fd = accept(serverFd, (sockaddr *)&addr, &addrLen);
    if (fd < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      break;
    }
    char ipBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
    std::string ip(ipBuf);

    bool reject = false;
    {
      std::lock_guard<std::mutex> lk(g_fdMtx);
      int count = 0;
      for (const auto &[_, v] : g_fdIP) if (v == ip) count++;
      if (count >= g_cfg.maxAgentsPerIP) reject = true;
      else g_fdIP[fd] = ip; // pre-register so IP limit counts correctly
    }
    if (reject) {
      net::sendMsg(fd, "{\"error\":\"ip_limit\"}");
      LOG_WARN("ip_limit: rejected extra agent from " + ip);
      close(fd);
      continue;
    }
    // Detach individual client handler threads — OK because each
    // cleans up its own fd. Server lifecycle managed separately.
    std::thread(handleClient, fd, ip).detach();
  }
}

static void viewerAcceptLoop(int vfd) {
  while (g_running) {
    sockaddr_in addr{}; socklen_t addrLen = sizeof(addr);
    int fd = accept(vfd, (sockaddr *)&addr, &addrLen);
    if (fd < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      break;
    }
    std::thread(viewerHandler, fd).detach();
  }
}

// ── Signal handler ───────────────────────────────────────────────────────────
static void sigHandler(int) { g_running = false; }

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char **argv) {
  uint16_t port = DEFAULT_PORT, vport = DEFAULT_VPORT;
  std::string cfgPath = "config/thresholds.conf",
              serverCfgPath = "config/server.conf";

  for (int i = 1; i < argc - 1; i++) {
    std::string arg(argv[i]);
    try {
      if      (arg == "-port")          port   = (uint16_t)std::stoi(argv[i+1]);
      else if (arg == "-vport")         vport  = (uint16_t)std::stoi(argv[i+1]);
      else if (arg == "-config")        cfgPath       = argv[i+1];
      else if (arg == "-server-config") serverCfgPath = argv[i+1];
    } catch (const std::exception &ex) {
      std::cerr << "[ERROR] Bad arg '" << arg << "': " << ex.what() << "\n";
      return 1;
    }
  }

  g_thresh = loadThresholds(cfgPath);
  g_cfg    = loadServerConfig(serverCfgPath);
  g_stats.reset();

  // Validate state file path
  if (!validateStatePath(g_cfg.stateFile)) {
    std::cerr << "[ERROR] STATE_FILE path outside working dir: " << g_cfg.stateFile << "\n";
    g_cfg.stateFile = "data/monitor_state.db";
  }
  try {
    std::filesystem::path p(g_cfg.stateFile);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  } catch (...) {}

  // Logger
  {
    LogLevel lv = LogLevel::INFO;
    if      (g_cfg.logLevel == "DEBUG") lv = LogLevel::DEBUG;
    else if (g_cfg.logLevel == "WARN")  lv = LogLevel::WARN;
    else if (g_cfg.logLevel == "ERROR") lv = LogLevel::ERROR;
    Logger::instance().init(g_cfg.logFile, lv);
  }
  LOG_INFO("monitor_server v4 starting — port=" + std::to_string(port)
           + " vport=" + std::to_string(vport)
           + " http=" + std::to_string(g_cfg.httpPort));

  // Alerter
  static Alerter alerter({g_cfg.alertWebhookUrl, g_cfg.alertCooldownSec,
                           !g_cfg.alertWebhookUrl.empty()});
  g_alerter = &alerter;
  if (!g_cfg.alertWebhookUrl.empty())
    LOG_INFO("alerting: webhook → " + g_cfg.alertWebhookUrl);

  // Restore state + history
  g_store.loadFromFile(g_cfg.stateFile);
  g_store.setHistoryDir(g_cfg.historyDir, g_cfg.historyMaxLines);
  g_store.loadHistoryFiles(500);

  // Signals
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT,  sigHandler);
  signal(SIGTERM, sigHandler);

  // Sockets
  int serverFd = createListenSocket(port);
  if (serverFd < 0) { LOG_ERROR("bind failed on port " + std::to_string(port)); return 1; }
  int viewerFd = createListenSocket(vport);
  if (viewerFd < 0) { LOG_ERROR("bind failed on vport " + std::to_string(vport)); return 1; }

  // HTTP API
  static HttpApiServer httpApi(g_store, g_stats);
  if (g_cfg.httpPort > 0) {
    if (httpApi.start(g_cfg.httpPort))
      LOG_INFO("HTTP API listening on :" + std::to_string(g_cfg.httpPort));
    else
      LOG_WARN("HTTP API failed to bind on port " + std::to_string(g_cfg.httpPort));
  }

  // Background threads (joinable, tracked)
  spawnThread([serverFd]() { acceptLoop(serverFd); });
  spawnThread([viewerFd]() { viewerAcceptLoop(viewerFd); });
  spawnThread([]() { persistLoop(); });
  spawnThread([]() { staleCheckerLoop(); });

  LOG_INFO("all threads started, entering render loop");

  // Dashboard (blocks until q/Q or signal)
  ui::Dashboard dash; dash.init();
  renderLoop(dash);

  // Graceful shutdown
  LOG_INFO("shutting down...");
  g_running = false;
  shutdown(serverFd, SHUT_RDWR); close(serverFd);
  shutdown(viewerFd, SHUT_RDWR); close(viewerFd);
  httpApi.stop();
  joinAllThreads();
  g_store.saveToFile(g_cfg.stateFile);
  LOG_INFO("shutdown complete");
  return 0;
}
