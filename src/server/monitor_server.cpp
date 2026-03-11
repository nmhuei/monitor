/*
 * monitor_server.cpp
 * Fixed: auth token validation, SO_RCVTIMEO on client sockets,
 *        explicit viewer port default, state file path validation.
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
#include <algorithm>
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

using namespace monitor;

static MetricsStore g_store;
static Thresholds   g_thresh;
static std::atomic<bool> g_running{true};

static std::mutex g_fdMtx;
static std::unordered_map<int, std::string> g_fdHost, g_fdIP;

struct ServerConfig {
  int maxAgentsPerIP   = 2;
  int backupIntervalSec= 10;
  std::string stateFile= "data/monitor_state.db";
  std::string authToken;          // empty = no auth required
};
static ServerConfig g_cfg;

static std::string trim(const std::string &s) {
  auto b=s.find_first_not_of(" \t\r\n"), e=s.find_last_not_of(" \t\r\n");
  return (b==std::string::npos)?"":s.substr(b,e-b+1);
}

static ServerConfig loadServerConfig(const std::string &path) {
  ServerConfig cfg;
  std::ifstream in(path); if(!in) return cfg;
  std::string line;
  while(std::getline(in,line)){
    line=trim(line); if(line.empty()||line[0]=='#') continue;
    auto eq=line.find('='); if(eq==std::string::npos) continue;
    std::string k=trim(line.substr(0,eq)), v=trim(line.substr(eq+1));
    try {
      if      (k=="MAX_AGENTS_PER_IP")    cfg.maxAgentsPerIP    =std::max(1,std::stoi(v));
      else if (k=="BACKUP_INTERVAL_SEC")  cfg.backupIntervalSec =std::max(1,std::stoi(v));
      else if (k=="STATE_FILE")           cfg.stateFile         =v;
      else if (k=="AUTH_TOKEN")           cfg.authToken         =v;
    } catch(...) {}
  }
  return cfg;
}

// Validate state file stays inside the server's working directory
static bool validateStatePath(const std::string &path) {
  try {
    auto cwd = std::filesystem::current_path();
    auto abs = std::filesystem::weakly_canonical(path);
    auto rel = std::filesystem::relative(abs, cwd);
    std::string rs = rel.string();
    return rs.rfind("..",0) != 0;
  } catch(...) { return false; }
}

// ── Client handler ────────────────────────────────────────────────────────────
static void handleClient(int fd, std::string ip) {
  // Set receive timeout to prevent blocking on half-open connections
  net::setRecvTimeout(fd, RECV_TIMEOUT_SEC);

  std::string hostName;
  bool authenticated = g_cfg.authToken.empty(); // no token = open access

  while (g_running) {
    std::string msg = net::recvMsg(fd);
    if (msg.empty()) break;   // disconnected or timed out

    try {
      auto obj = json::decode(msg);

      // ── Auth check on first message ──────────────────────────────────────
      if (!authenticated) {
        if (!obj.count("auth") || obj["auth"].str != g_cfg.authToken) {
          // Send rejection and close
          net::sendMsg(fd, "{\"error\":\"auth_failed\"}");
          close(fd);
          return;
        }
        authenticated = true;
      }

      std::string host = obj.count("host") ? obj["host"].str : "unknown";
      float cpu  = obj.count("cpu")  ? (float)obj["cpu"].num  : 0.f;
      float ram  = obj.count("ram")  ? (float)obj["ram"].num  : 0.f;
      float disk = obj.count("disk") ? (float)obj["disk"].num : 0.f;
      time_t ts  = obj.count("timestamp") ? (time_t)obj["timestamp"].num : time(nullptr);
      float netRx  = obj.count("net_rx")    ? (float)obj["net_rx"].num    : 0.f;
      float netTx  = obj.count("net_tx")    ? (float)obj["net_tx"].num    : 0.f;
      float loadAvg= obj.count("load_avg")  ? (float)obj["load_avg"].num  : 0.f;
      int procCount= obj.count("proc_count")? (int)obj["proc_count"].num  : 0;

      if (hostName.empty()) {
        hostName = host;
        std::lock_guard<std::mutex> lk(g_fdMtx);
        g_fdHost[fd]=host; g_fdIP[fd]=ip;
        g_store.setOnline(host, ip, fd);
      }

      MetricPayload p;
      p.host=host; p.cpu=cpu; p.ram=ram; p.disk=disk; p.timestamp=ts; p.ip=ip;
      p.netRxKB=netRx; p.netTxKB=netTx; p.loadAvg=loadAvg; p.procCount=procCount;
      if (obj.count("cores") && obj["cores"].is_arr)
        for (double v : obj["cores"].arr) p.cores.push_back((float)v);

      g_store.upsert(p, g_thresh);
    } catch(...) {
      // Malformed JSON — skip silently
    }
  }

  {
    std::lock_guard<std::mutex> lk(g_fdMtx);
    if (!hostName.empty()) g_store.setOffline(hostName);
    g_fdHost.erase(fd); g_fdIP.erase(fd);
  }
  close(fd);
}

// ── Loops ─────────────────────────────────────────────────────────────────────
static void persistLoop() {
  while (g_running) {
    g_store.saveToFile(g_cfg.stateFile);
    std::this_thread::sleep_for(std::chrono::seconds(g_cfg.backupIntervalSec));
  }
  g_store.saveToFile(g_cfg.stateFile);
}

static void renderLoop(ui::Dashboard &dash) {
  auto lastData = std::chrono::steady_clock::now();
  std::vector<HostState> hosts; std::vector<LogEvent> log;
  while (g_running) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now-lastData).count()>=1000) {
      hosts=g_store.snapshot(); log=g_store.logSnapshot(); lastData=now;
    }
    dash.render(hosts, log, g_thresh);
  }
}

static void acceptLoop(int serverFd) {
  while (g_running) {
    sockaddr_in addr{}; socklen_t addrLen=sizeof(addr);
    int fd=accept(serverFd,(sockaddr*)&addr,&addrLen);
    if (fd<0) { if(errno==EINTR||errno==EAGAIN) continue; break; }
    char ipBuf[INET_ADDRSTRLEN]; inet_ntop(AF_INET,&addr.sin_addr,ipBuf,sizeof(ipBuf));
    std::string ip(ipBuf);
    bool reject=false;
    {
      std::lock_guard<std::mutex> lk(g_fdMtx);
      int count=0;
      for(const auto &[_,v]:g_fdIP) if(v==ip) count++;
      if(count>=g_cfg.maxAgentsPerIP) reject=true;
      else g_fdIP[fd]=ip;
    }
    if(reject) {
      net::sendMsg(fd,"{\"error\":\"ip_limit\"}");
      close(fd); continue;
    }
    std::thread(handleClient, fd, ip).detach();
  }
}

static void viewerHandler(int fd) {
  while (g_running) {
    auto hosts=g_store.snapshot();
    std::string frame=viewer::renderFrame(hosts, g_thresh);
    if (write(fd, frame.c_str(), frame.size())<=0) break;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  close(fd);
}

static void viewerAcceptLoop(int vfd) {
  while (g_running) {
    sockaddr_in addr{}; socklen_t addrLen=sizeof(addr);
    int fd=accept(vfd,(sockaddr*)&addr,&addrLen);
    if (fd<0) { if(errno==EINTR||errno==EAGAIN) continue; break; }
    std::thread(viewerHandler, fd).detach();
  }
}

static int createListenSocket(uint16_t port) {
  int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1;
  int opt=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  sockaddr_in addr{}; addr.sin_family=AF_INET;
  addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(port);
  if(bind(fd,(sockaddr*)&addr,sizeof(addr))<0||listen(fd,32)<0) { close(fd); return -1; }
  return fd;
}

int main(int argc, char **argv) {
  uint16_t port=DEFAULT_PORT, vport=DEFAULT_VPORT;
  std::string cfgPath="config/thresholds.conf", serverCfgPath="config/server.conf";

  for(int i=1;i<argc-1;i++) {
    std::string arg(argv[i]);
    if      (arg=="-port")          port   =(uint16_t)std::stoi(argv[i+1]);
    else if (arg=="-vport")         vport  =(uint16_t)std::stoi(argv[i+1]);
    else if (arg=="-config")        cfgPath=argv[i+1];
    else if (arg=="-server-config") serverCfgPath=argv[i+1];
  }

  g_thresh = loadThresholds(cfgPath);
  g_cfg    = loadServerConfig(serverCfgPath);

  // Validate state file path — reject traversals
  if (!validateStatePath(g_cfg.stateFile)) {
    std::cerr << "[ERROR] STATE_FILE path is outside working directory: "
              << g_cfg.stateFile << "\nUsing default: data/monitor_state.db\n";
    g_cfg.stateFile = "data/monitor_state.db";
  }

  if (!g_cfg.authToken.empty())
    std::cout << "[auth] Token authentication enabled.\n";
  else
    std::cout << "[auth] WARNING: No AUTH_TOKEN set. Any agent can connect.\n";

  try {
    std::filesystem::path p(g_cfg.stateFile);
    if(p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  } catch(...) {}

  g_store.loadFromFile(g_cfg.stateFile);
  signal(SIGPIPE, SIG_IGN);

  int serverFd=createListenSocket(port);
  if(serverFd<0) { perror("agent socket"); return 1; }

  int viewerFd=createListenSocket(vport);
  if(viewerFd<0) { perror("viewer socket"); return 1; }

  std::cout << "Monitor server listening — agents:" << port
            << "  viewer:" << vport << "\n";

  ui::Dashboard dash; dash.init();

  std::thread(acceptLoop, serverFd).detach();
  std::thread(viewerAcceptLoop, viewerFd).detach();
  std::thread(persistLoop).detach();

  renderLoop(dash);

  close(serverFd); close(viewerFd);
  return 0;
}
