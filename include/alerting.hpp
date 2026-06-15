#pragma once
/*
 * alerting.hpp — Alert dispatcher: HTTP/HTTPS webhook on status transitions.
 *
 * Features:
 *   - POST JSON payload to any webhook URL (Slack, Discord, Teams, custom)
 *   - Supports HTTPS/TLS via fork-exec curl subprocess.
 *   - Per-host cooldown to prevent spam (ALERT_COOLDOWN_SEC)
 *   - Recovery notification when host returns to ONLINE from ALERT/OFFLINE
 *   - Runs in a dedicated background worker thread with a bounded queue (max 50)
 *     to prevent thread leaks and render loop blocking.
 */
#include "logger.hpp"
#include "protocol.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <queue>
#include <condition_variable>
#include <atomic>

namespace monitor {

struct AlertConfig {
  std::string webhookUrl;   // full url, e.g. https://hooks.slack.com/...
  int  cooldownSec = 300;   // min seconds between alerts for same host
  bool enabled     = false;
};

// Parse a URL into host, path, port (http only — no libcurl)
struct ParsedUrl {
  std::string host, path;
  uint16_t    port = 80;
  bool        ok   = false;
};

inline ParsedUrl parseUrl(const std::string &url) {
  ParsedUrl p;
  std::string u = url;
  bool isHttps = false;
  if (u.rfind("https://", 0) == 0) { u = u.substr(8); isHttps = true; }
  else if (u.rfind("http://", 0) == 0) { u = u.substr(7); }
  else return p;

  p.port = isHttps ? 443 : 80;
  auto slash = u.find('/');
  std::string hostpart = (slash==std::string::npos) ? u : u.substr(0, slash);
  p.path = (slash==std::string::npos) ? "/" : u.substr(slash);

  // Check for port in host
  auto colon = hostpart.rfind(':');
  if (colon != std::string::npos) {
    try { p.port = (uint16_t)std::stoi(hostpart.substr(colon+1)); } catch(...) {}
    p.host = hostpart.substr(0, colon);
  } else {
    p.host = hostpart;
  }
  p.ok = !p.host.empty();
  return p;
}

// Fire-and-forget HTTP POST via raw POSIX socket
inline void httpPost(const std::string &url, const std::string &jsonBody) {
  auto p = parseUrl(url);
  if (!p.ok) { LOG_WARN("alerting: invalid webhook URL: " + url); return; }
  if (p.port == 443) {
    LOG_WARN("alerting: HTTPS webhooks not supported directly via raw sockets. Use HTTP proxy or curl.");
    return;
  }

  addrinfo hints{}, *res = nullptr;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(p.host.c_str(), std::to_string(p.port).c_str(), &hints, &res) != 0) {
    LOG_WARN("alerting: cannot resolve host: " + p.host);
    return;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); return; }

  // 5s connect timeout
  struct timeval tv{}; tv.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  bool connected = (connect(fd, res->ai_addr, res->ai_addrlen) == 0);
  freeaddrinfo(res);
  if (!connected) { close(fd); LOG_WARN("alerting: connect failed to " + p.host); return; }

  std::string req =
    "POST " + p.path + " HTTP/1.1\r\n"
    "Host: " + p.host + "\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n"
    "Connection: close\r\n\r\n" + jsonBody;

  if (send(fd, req.c_str(), req.size(), MSG_NOSIGNAL) <= 0)
    LOG_WARN("alerting: send failed");
  else
    LOG_INFO("alerting: webhook sent to " + p.host + p.path);

  close(fd);
}

// Status name for webhook payload
inline const char *statusName(HostStatus s) {
  switch(s) {
  case HostStatus::ALERT:   return "ALERT";
  case HostStatus::WARNING: return "WARN";
  case HostStatus::STALE:   return "STALE";
  case HostStatus::ONLINE:  return "ONLINE";
  case HostStatus::OFFLINE: return "OFFLINE";
  }
  return "UNKNOWN";
}

class Alerter {
public:
  explicit Alerter(AlertConfig cfg) : cfg_(std::move(cfg)), running_(true) {
    if (cfg_.enabled && !cfg_.webhookUrl.empty()) {
      worker_ = std::thread(&Alerter::workerLoop, this);
    }
  }

  ~Alerter() {
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  // Returns true if an alert was queued
  bool maybeAlert(const std::string &host, HostStatus prev, HostStatus cur,
                  float cpu, float ram, float disk) {
    if (!cfg_.enabled || cfg_.webhookUrl.empty()) return false;

    bool wasOk      = (prev == HostStatus::ONLINE || prev == HostStatus::WARNING);
    bool nowBad     = (cur  == HostStatus::ALERT  || cur  == HostStatus::OFFLINE ||
                       cur  == HostStatus::STALE);
    bool recovery   = (!wasOk && cur == HostStatus::ONLINE);

    if (!nowBad && !recovery) return false;
    if (onCooldown(host, recovery)) return false;

    // Build Slack-compatible payload
    std::string emoji  = (cur==HostStatus::ALERT)?"🔴":(cur==HostStatus::OFFLINE||cur==HostStatus::STALE)?"⚫":"🟢";
    std::string title  = recovery
      ? "✅ Recovery: " + host + " is back ONLINE"
      : emoji + " Alert: " + host + " → " + statusName(cur);

    char detail[128];
    snprintf(detail, sizeof(detail), "CPU %.1f%%  RAM %.1f%%  DISK %.1f%%", cpu, ram, disk);

    // Slack message format (works as generic webhook too)
    std::string body = "{\"text\":\"" + title + "\\n" + std::string(detail) + "\"}";

    // Push to queue
    {
      std::lock_guard<std::mutex> lk(queueMtx_);
      if (queue_.size() >= 50) {
        LOG_WARN("alerting: Queue full (size >= 50), dropping alert: " + title);
        return false;
      }
      queue_.push(body);
    }
    cv_.notify_one();
    updateCooldown(host);
    return true;
  }

private:
  bool onCooldown(const std::string &host, bool /*recovery*/) {
    std::lock_guard<std::mutex> lk(cooldownMtx_);
    auto it = lastAlert_.find(host);
    if (it == lastAlert_.end()) return false;
    double elapsed = std::difftime(time(nullptr), it->second);
    return elapsed < cfg_.cooldownSec;
  }

  void updateCooldown(const std::string &host) {
    std::lock_guard<std::mutex> lk(cooldownMtx_);
    lastAlert_[host] = time(nullptr);
  }

  void workerLoop() {
    while (running_) {
      std::string body;
      {
        std::unique_lock<std::mutex> lk(queueMtx_);
        cv_.wait(lk, [this]() { return !running_ || !queue_.empty(); });
        if (!running_ && queue_.empty()) {
          return;
        }
        body = std::move(queue_.front());
        queue_.pop();
      }
      dispatchAlert(body);
    }
  }

  void dispatchAlert(const std::string &jsonBody) {
    std::string url = cfg_.webhookUrl;
    if (url.rfind("https://", 0) == 0) {
      // Use curl subprocess for HTTPS/TLS to avoid shell injection
      pid_t pid = fork();
      if (pid == 0) {
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
          dup2(dn, 1);
          dup2(dn, 2);
          close(dn);
        }
        char* args[] = {
          (char*)"/usr/bin/curl",
          (char*)"-s",
          (char*)"-X", (char*)"POST",
          (char*)"-H", (char*)"Content-Type: application/json",
          (char*)"-d", (char*)jsonBody.c_str(),
          (char*)url.c_str(),
          nullptr
        };
        execv("/usr/bin/curl", args);
        _exit(127);
      } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
          LOG_INFO("alerting: HTTPS webhook sent successfully via curl to " + url);
        } else {
          LOG_WARN("alerting: curl webhook execution failed for " + url);
        }
      }
    } else {
      httpPost(url, jsonBody);
    }
  }

  AlertConfig cfg_;
  std::mutex  cooldownMtx_;
  std::unordered_map<std::string, time_t> lastAlert_;

  std::thread worker_;
  std::atomic<bool> running_;
  std::mutex queueMtx_;
  std::condition_variable cv_;
  std::queue<std::string> queue_;
};

} // namespace monitor
