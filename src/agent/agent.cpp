/*
 * agent.cpp — metrics collection agent
 * Separates collection and sending threads; supports a local backpressure queue.
 */
#include "../../include/json_wrapper.hpp"
#include "../../include/metrics_collector.hpp"
#include "../../include/net_framing.hpp"
#include "../../include/protocol.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <queue>
#include <deque>
#include <condition_variable>
#include <mutex>

using namespace monitor;

// ── Queue & Thread-safety ───────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::mutex g_queueMtx;
static std::condition_variable g_queueCv;
static std::deque<std::string> g_pendingQueue;
static constexpr size_t MAX_PENDING_ITEMS = 120; // Up to 10 mins at 5s interval

static void sigHandler(int) {
  g_running = false;
  g_queueCv.notify_all();
}

static bool daemonizeAgent() {
  pid_t pid = fork(); if (pid < 0) return false; if (pid > 0) _exit(0);
  if (setsid() < 0) return false;
  int dn = open("/dev/null", O_RDWR);
  if (dn >= 0) { dup2(dn,0); dup2(dn,1); dup2(dn,2); if(dn>2) close(dn); }
  return true;
}

static int connectToServer(const std::string &host, uint16_t port) {
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    return -1;
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); return -1; }
  struct timeval tv{}; tv.tv_sec = 10;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    close(fd); freeaddrinfo(res); return -1;
  }
  freeaddrinfo(res);
  return fd;
}

static std::string trim(const std::string &s) {
  auto b=s.find_first_not_of(" \t\r\n"), e=s.find_last_not_of(" \t\r\n");
  return (b==std::string::npos) ? "" : s.substr(b, e-b+1);
}

struct AgentConfig {
  int maxRetries = 0;
  int reconnectSec = RECONNECT_INTERVAL_SEC;
  std::string authToken;
};

static AgentConfig loadAgentConfig(const std::string &path) {
  AgentConfig cfg;
  std::ifstream in(path); if (!in) return cfg;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0]=='#') continue;
    auto eq = line.find('='); if (eq==std::string::npos) continue;
    std::string k=trim(line.substr(0,eq)), v=trim(line.substr(eq+1));
    try {
      if      (k=="MAX_CONNECT_RETRIES")   cfg.maxRetries   = std::max(0,std::stoi(v));
      else if (k=="RECONNECT_INTERVAL_SEC") cfg.reconnectSec = std::max(1,std::stoi(v));
      else if (k=="AUTH_TOKEN")             cfg.authToken    = v;
    } catch(...) {}
  }
  return cfg;
}

// ── Collector Loop ───────────────────────────────────────────────────────────
static void collectLoop(metrics::AsyncCpuSampler &cpuSampler, metrics::NetRaw &prevNet,
                        const std::string &agentName, const std::string &diskPath, int interval) {
  while (g_running) {
    auto sampleRes = metrics::collectWith(cpuSampler, prevNet, diskPath);
    std::string payload;
    
    if (sampleRes.valid) {
      auto& s = sampleRes.sample;
      time_t now = time(nullptr);
      payload = json::encode(agentName, s.cpu, s.ram, s.disk,
                             now, s.cores,
                             s.netRxKB, s.netTxKB,
                             s.loadAvg, s.procCount);
    } else {
      // Send heartbeat instead of fake zero metric if validation fails
      nlohmann::json heartbeatMsg;
      heartbeatMsg["type"] = "heartbeat";
      heartbeatMsg["host"] = agentName;
      heartbeatMsg["error"] = sampleRes.error;
      payload = heartbeatMsg.dump();
      std::cerr << "[WARN] Metric collection failed. Sending heartbeat error: " << sampleRes.error << "\n";
    }

    {
      std::lock_guard<std::mutex> lk(g_queueMtx);
      g_pendingQueue.push_back(payload);
      if (g_pendingQueue.size() > MAX_PENDING_ITEMS) {
        g_pendingQueue.pop_front(); // drop oldest
      }
    }
    g_queueCv.notify_one();

    for (int i = 0; i < interval && g_running; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

// ── Sender Loop ──────────────────────────────────────────────────────────────
static void sendLoop(const std::string &serverHost, uint16_t serverPort, const AgentConfig &cfg, const std::string &agentName) {
  int fd = -1;
  int failedAttempts = 0;

  while (g_running) {
    std::string payload;
    {
      std::unique_lock<std::mutex> lk(g_queueMtx);
      g_queueCv.wait(lk, []() { return !g_running || !g_pendingQueue.empty(); });
      if (!g_running && g_pendingQueue.empty()) {
        break;
      }
      payload = g_pendingQueue.front();
    }

    if (fd < 0) {
      std::cout << "Connecting to " << serverHost << ":" << serverPort << "...\n";
      fd = connectToServer(serverHost, serverPort);
      if (fd < 0) {
        failedAttempts++;
        std::cout << "Connection failed (attempt " << failedAttempts;
        if (cfg.maxRetries > 0) std::cout << "/" << cfg.maxRetries;
        std::cout << "). Retrying in " << cfg.reconnectSec << "s...\n";
        if (cfg.maxRetries > 0 && failedAttempts >= cfg.maxRetries) {
          std::cout << "Max retries reached. Stopping.\n";
          g_running = false;
          g_queueCv.notify_all();
          break;
        }
        for (int i = 0; i < cfg.reconnectSec && g_running; i++) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        continue;
      }
      failedAttempts = 0;
      std::cout << "Connected. Authenticating...\n";

      // Perform handshake
      nlohmann::json authMsg;
      authMsg["type"] = "auth";
      authMsg["token"] = cfg.authToken;
      authMsg["version"] = "1";
      if (!net::sendMsg(fd, authMsg.dump())) {
        std::cout << "Auth handshake send failed — disconnected.\n";
        close(fd); fd = -1;
        for (int i = 0; i < cfg.reconnectSec && g_running; i++) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        continue;
      }
      std::string resp = net::recvMsg(fd);
      if (resp.empty()) {
        std::cout << "No response for auth handshake — disconnected.\n";
        close(fd); fd = -1;
        for (int i = 0; i < cfg.reconnectSec && g_running; i++) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        continue;
      }
      try {
        auto jResp = nlohmann::json::parse(resp);
        if (jResp.contains("error")) {
          std::string errStr = jResp["error"].get<std::string>();
          std::cout << "Auth failed: " << errStr << std::endl;
          close(fd); fd = -1;
          if (errStr == "auth_failed") {
            std::cout << "Shutting down agent due to authentication failure." << std::endl;
            g_running = false;
            g_queueCv.notify_all();
            break;
          }
          for (int i = 0; i < cfg.reconnectSec && g_running; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
          continue;
        }
      } catch (...) {
        std::cout << "Invalid auth response from server.\n";
        close(fd); fd = -1;
        for (int i = 0; i < cfg.reconnectSec && g_running; i++) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        continue;
      }
      std::cout << "Authentication successful.\n";
    }

    // Check socket health
    int sockErr = 0; socklen_t errLen = sizeof(sockErr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen) < 0 || sockErr != 0) {
      close(fd); fd = -1;
      continue;
    }

    if (!net::sendMsg(fd, payload)) {
      std::cout << "Send failed — disconnected.\n";
      close(fd); fd = -1;
      continue;
    }

    // Success! Remove from front of queue
    {
      std::lock_guard<std::mutex> lk(g_queueMtx);
      if (!g_pendingQueue.empty()) {
        g_pendingQueue.pop_front();
      }
    }
  }

  if (fd >= 0) close(fd);
}

int main(int argc, char **argv) {
  std::string serverHost="127.0.0.1";
  uint16_t serverPort=DEFAULT_PORT;
  int interval=5;
  std::string agentName="agent", diskPath="/", cfgPath="config/agent.conf";
  std::string tokenOverride;
  bool foreground=false;

  for (int i=1;i<argc;i++) {
    std::string arg(argv[i]);
    if (arg=="-server"&&i+1<argc) {
      std::string sv(argv[++i]); auto c=sv.rfind(':');
      if (c!=std::string::npos) { serverHost=sv.substr(0,c); serverPort=(uint16_t)std::stoi(sv.substr(c+1)); }
      else serverHost=sv;
    } else if (arg=="-interval"&&i+1<argc) interval=std::stoi(argv[++i]);
    else if (arg=="-name"&&i+1<argc)       agentName=argv[++i];
    else if (arg=="-disk"&&i+1<argc)       diskPath=argv[++i];
    else if (arg=="-config"&&i+1<argc)     cfgPath=argv[++i];
    else if (arg=="-token"&&i+1<argc)      tokenOverride=argv[++i];
    else if (arg=="-fg"||arg=="--foreground") foreground=true;
  }

  auto cfg = loadAgentConfig(cfgPath);
  if (!tokenOverride.empty()) cfg.authToken = tokenOverride;

  signal(SIGINT, sigHandler); signal(SIGTERM, sigHandler); signal(SIGPIPE, SIG_IGN);

  if (!foreground) { if (!daemonizeAgent()) return 1; }

  std::cout << "Agent '" << agentName << "' started\n";
  std::cout << "Server: " << serverHost << ":" << serverPort << "\n";
  std::cout << "Interval: " << interval << "s | Auth: " << (cfg.authToken.empty()?"none":"***") << "\n";

  metrics::AsyncCpuSampler cpuSampler;
  metrics::NetRaw prevNet = metrics::readNetRaw();

  // Warm up CPU sampler (1 cycle)
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Spawn threads
  std::thread collectThread(collectLoop, std::ref(cpuSampler), std::ref(prevNet), agentName, diskPath, interval);
  std::thread sendThread(sendLoop, serverHost, serverPort, cfg, agentName);

  // Wait for signal
  if (collectThread.joinable()) collectThread.join();
  if (sendThread.joinable()) sendThread.join();

  std::cout << "\nAgent stopped.\n";
  return 0;
}
