/*
 * agent.cpp — metrics collection agent
 *
 * Usage: ./agent -server <host>:<port> -interval <sec> -name <hostname>
 *               [-disk <path>]
 *
 * Collects CPU, RAM, Disk and sends JSON to the monitor server.
 * Auto-reconnects on disconnect.
 */
#include "../../include/json_helper.hpp"
#include "../../include/metrics_collector.hpp"
#include "../../include/net_framing.hpp"
#include "../../include/protocol.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

using namespace monitor;

static std::atomic<bool> g_running{true};

static void sigHandler(int) { g_running = false; }

static int connectToServer(const std::string &host, uint16_t port) {
  struct addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) !=
      0)
    return -1;

  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    return -1;
  }

  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    close(fd);
    freeaddrinfo(res);
    return -1;
  }
  freeaddrinfo(res);
  return fd;
}

int main(int argc, char **argv) {
  std::string serverHost = "127.0.0.1";
  uint16_t serverPort = DEFAULT_PORT;
  int interval = 5;
  std::string agentName = "agent";
  std::string diskPath = "/";

  // Parse arguments
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "-server" && i + 1 < argc) {
      std::string sv(argv[++i]);
      auto colon = sv.rfind(':');
      if (colon != std::string::npos) {
        serverHost = sv.substr(0, colon);
        serverPort = (uint16_t)std::stoi(sv.substr(colon + 1));
      } else {
        serverHost = sv;
      }
    } else if (arg == "-interval" && i + 1 < argc) {
      interval = std::stoi(argv[++i]);
    } else if (arg == "-name" && i + 1 < argc) {
      agentName = argv[++i];
    } else if (arg == "-disk" && i + 1 < argc) {
      diskPath = argv[++i];
    }
  }

  signal(SIGINT, sigHandler);
  signal(SIGTERM, sigHandler);
  signal(SIGPIPE, SIG_IGN);

  std::cout << "Agent '" << agentName << "' started\n";
  std::cout << "Server: " << serverHost << ":" << serverPort << "\n";
  std::cout << "Sending metrics every " << interval << " seconds...\n";

  int fd = -1;

  while (g_running) {
    // Connect / reconnect
    if (fd < 0) {
      std::cout << "Connecting to " << serverHost << ":" << serverPort
                << "...\n";
      fd = connectToServer(serverHost, serverPort);
      if (fd < 0) {
        std::cout << "Connection failed. Retrying in " << RECONNECT_INTERVAL_SEC
                  << "s...\n";
        for (int i = 0; i < RECONNECT_INTERVAL_SEC && g_running; i++)
          std::this_thread::sleep_for(std::chrono::seconds(1));
        continue;
      }
      std::cout << "Connected.\n";
    }

    // Collect
    auto sample = metrics::collect(diskPath);

    // Check socket health before sending (catches server-side close)
    int sockErr = 0;
    socklen_t errLen = sizeof(sockErr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &errLen) < 0 ||
        sockErr != 0) {
      std::cout << "Connection lost (socket error " << sockErr
                << "). Reconnecting in " << RECONNECT_INTERVAL_SEC << "s...\n";
      close(fd);
      fd = -1;
      for (int i = 0; i < RECONNECT_INTERVAL_SEC && g_running; i++)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    // Encode and send (with per-core CPU data)
    time_t now = time(nullptr);
    std::string payload = json::encode(agentName, sample.cpu, sample.ram,
                                       sample.disk, now, sample.cores);

    if (!net::sendMsg(fd, payload)) {
      std::cout << "Send failed — server disconnected. Reconnecting in "
                << RECONNECT_INTERVAL_SEC << "s...\n";
      close(fd);
      fd = -1;
      for (int i = 0; i < RECONNECT_INTERVAL_SEC && g_running; i++)
        std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }

    std::cout << "[" << agentName << "] cpu=" << (int)sample.cpu
              << "% ram=" << (int)sample.ram << "% disk=" << (int)sample.disk
              << "% cores=" << sample.cores.size() << "\n";

    // Sleep interval (interruptible)
    for (int i = 0; i < interval && g_running; i++)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (fd >= 0)
    close(fd);
  std::cout << "\nAgent stopped.\n";
  return 0;
}
