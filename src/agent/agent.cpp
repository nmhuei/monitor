/*
 * agent.cpp — metrics collection agent
 * Fixed: async CPU, auth token, new metrics (net, load, procs), SO_SNDTIMEO.
 * Usage: ./agent -server <host>:<port> -interval <sec> -name <hostname>
 *               [-disk <path>] [-config <path>] [-token <secret>] [-fg]
 */
#include "../../include/json_helper.hpp"
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

using namespace monitor;

static std::atomic<bool> g_running{true};
static void sigHandler(int) { g_running = false; }

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
  // Send timeout to avoid blocking forever on broken network
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

  // Start async CPU sampler — does not block collect()
  metrics::AsyncCpuSampler cpuSampler;
  metrics::NetRaw prevNet = metrics::readNetRaw();

  // Warm up CPU sampler (1 cycle)
  std::this_thread::sleep_for(std::chrono::seconds(1));

  int fd=-1, failedAttempts=0;

  while (g_running) {
    if (fd < 0) {
      std::cout << "Connecting to " << serverHost << ":" << serverPort << "...\n";
      fd = connectToServer(serverHost, serverPort);
      if (fd < 0) {
        failedAttempts++;
        std::cout << "Connection failed (attempt " << failedAttempts;
        if (cfg.maxRetries>0) std::cout << "/" << cfg.maxRetries;
        std::cout << "). Retrying in " << cfg.reconnectSec << "s...\n";
        if (cfg.maxRetries>0 && failedAttempts>=cfg.maxRetries) {
          std::cout << "Max retries reached. Stopping.\n"; break;
        }
        for (int i=0;i<cfg.reconnectSec&&g_running;i++)
          std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
      failedAttempts=0;
      std::cout << "Connected.\n";
    }

    // Collect (non-blocking CPU)
    auto sample = metrics::collectWith(cpuSampler, prevNet, diskPath);

    // Check socket health
    int sockErr=0; socklen_t errLen=sizeof(sockErr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen)<0 || sockErr!=0) {
      close(fd); fd=-1;
      for(int i=0;i<cfg.reconnectSec&&g_running;i++)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    time_t now=time(nullptr);
    std::string payload=json::encode(agentName, sample.cpu, sample.ram, sample.disk,
                                     now, sample.cores,
                                     sample.netRxKB, sample.netTxKB,
                                     sample.loadAvg, sample.procCount,
                                     cfg.authToken);

    if (!net::sendMsg(fd, payload)) {
      std::cout << "Send failed — disconnected.\n";
      close(fd); fd=-1;
      for(int i=0;i<cfg.reconnectSec&&g_running;i++)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    std::cout << "[" << agentName << "] cpu=" << (int)sample.cpu
              << "% ram=" << (int)sample.ram << "% load=" << sample.loadAvg
              << " net_rx=" << (int)sample.netRxKB << "KB tx=" << (int)sample.netTxKB << "KB\n";

    for (int i=0;i<interval&&g_running;i++)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (fd>=0) close(fd);
  std::cout << "\nAgent stopped.\n";
  return 0;
}
