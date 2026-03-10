#pragma once
/*
 * Thread-safe per-host metric store used by the server.
 */
#include "protocol.hpp"
#include "thresholds.hpp"
#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace monitor {

struct HistorySample {
  time_t ts;
  float cpu, ram, disk;
};

struct HostState {
  std::string name;
  std::string ip;
  float cpu = 0;
  float ram = 0;
  float disk = 0;
  time_t lastSeen = 0;
  HostStatus status = HostStatus::OFFLINE;
  std::deque<HistorySample> history; // newest at back
  int fd = -1;                       // socket fd
  std::vector<float> cores;          // latest per-core CPU %
  int coreCount = 0;
  float netRxKBps = 0;
  float netTxKBps = 0;
  float load1 = 0;
  int procCount = 0;

  void push(float c, float r, float d, time_t ts,
            const std::vector<float> &coreVals = {}) {
    cpu = c;
    ram = r;
    disk = d;
    lastSeen = ts;
    cores = coreVals;
    coreCount = (int)coreVals.size();
    history.push_back({ts, c, r, d});
    if (history.size() > MAX_HISTORY)
      history.pop_front();
  }

  std::vector<HistorySample> recent(size_t n) const {
    if (history.size() <= n)
      return {history.begin(), history.end()};
    return {history.end() - n, history.end()};
  }
};

// Log event types
enum class LogEventType { CONNECT, METRIC, ALERT, DISCONNECT };

struct LogEvent {
  time_t ts;
  std::string host;
  std::string ip;
  LogEventType type;
  float cpu = 0, ram = 0, disk = 0;
  std::string detail; // e.g. "CPU=87%"
};

class MetricsStore {
public:
  static std::string esc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
      if (c == '\\' || c == '|')
        o += '\\';
      if (c == '\n') {
        o += "\\n";
        continue;
      }
      o += c;
    }
    return o;
  }

  static std::string unesc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    bool e = false;
    for (char c : s) {
      if (!e) {
        if (c == '\\') {
          e = true;
          continue;
        }
        o += c;
        continue;
      }
      if (c == 'n')
        o += '\n';
      else
        o += c;
      e = false;
    }
    return o;
  }
  void upsert(const MetricPayload &p, const Thresholds &thresh) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto &h = hosts_[p.host];
    h.name = p.host;
    h.ip = p.ip;
    h.push(p.cpu, p.ram, p.disk, p.timestamp, p.cores);
    h.netRxKBps = p.netRxKBps;
    h.netTxKBps = p.netTxKBps;
    h.load1 = p.load1;
    h.procCount = p.procCount;

    // Determine status
    bool alert = (p.cpu >= thresh.getCPU(p.host)) ||
                 (p.ram >= thresh.getRAM(p.host)) ||
                 (p.disk >= thresh.getDisk(p.host));
    bool warn = (p.cpu >= thresh.getCPU(p.host) * 0.85f) ||
                (p.ram >= thresh.getRAM(p.host) * 0.85f) ||
                (p.disk >= thresh.getDisk(p.host) * 0.85f);
    h.status = alert  ? HostStatus::ALERT
               : warn ? HostStatus::WARNING
                      : HostStatus::ONLINE;

    // Build log event
    LogEvent ev;
    ev.ts = p.timestamp;
    ev.host = p.host;
    ev.ip = p.ip;
    ev.cpu = p.cpu;
    ev.ram = p.ram;
    ev.disk = p.disk;
    if (alert) {
      ev.type = LogEventType::ALERT;
      if (p.cpu >= thresh.getCPU(p.host))
        ev.detail += "CPU=" + std::to_string((int)p.cpu) + "% ";
      if (p.ram >= thresh.getRAM(p.host))
        ev.detail += "RAM=" + std::to_string((int)p.ram) + "% ";
      if (p.disk >= thresh.getDisk(p.host))
        ev.detail += "DISK=" + std::to_string((int)p.disk) + "% ";
    } else {
      ev.type = LogEventType::METRIC;
    }
    pushLog(ev);
  }

  void setOnline(const std::string &host, const std::string &ip, int fd) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto &h = hosts_[host];
    h.name = host;
    h.ip = ip;
    h.fd = fd;
    h.status = HostStatus::ONLINE;
    h.lastSeen = time(nullptr);
    LogEvent ev;
    ev.ts = time(nullptr);
    ev.host = host;
    ev.ip = ip;
    ev.type = LogEventType::CONNECT;
    pushLog(ev);
  }

  void setOffline(const std::string &host) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = hosts_.find(host);
    if (it != hosts_.end()) {
      it->second.status = HostStatus::OFFLINE;
      it->second.fd = -1;
    }
    LogEvent ev;
    ev.ts = time(nullptr);
    ev.host = host;
    ev.ip = (it != hosts_.end()) ? it->second.ip : "";
    ev.type = LogEventType::DISCONNECT;
    pushLog(ev);
  }

  std::vector<HostState> snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<HostState> v;
    v.reserve(hosts_.size());
    for (auto &[k, h] : hosts_)
      v.push_back(h);
    return v;
  }

  std::vector<LogEvent> logSnapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return {log_.begin(), log_.end()};
  }

  // Persist lightweight state for restart recovery.
  // Format (line based):
  // HOST|<name>|<ip>|<cpu>|<ram>|<disk>|<lastSeen>|<status>
  // LOG |<ts>|<host>|<ip>|<type>|<cpu>|<ram>|<disk>|<detail>
  bool saveToFile(const std::string &path) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
      return false;

    for (const auto &[name, h] : hosts_) {
      out << "HOST|" << esc(h.name) << "|" << esc(h.ip) << "|" << h.cpu << "|" << h.ram
          << "|" << h.disk << "|" << (long long)h.lastSeen << "|"
          << (int)h.status << "|" << h.netRxKBps << "|" << h.netTxKBps
          << "|" << h.load1 << "|" << h.procCount << "\n";
    }

    for (const auto &ev : log_) {
      out << "LOG|" << (long long)ev.ts << "|" << esc(ev.host) << "|" << esc(ev.ip) << "|"
          << (int)ev.type << "|" << ev.cpu << "|" << ev.ram << "|" << ev.disk
          << "|" << esc(ev.detail) << "\n";
    }
    return true;
  }

  bool loadFromFile(const std::string &path) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ifstream in(path);
    if (!in)
      return false;

    hosts_.clear();
    log_.clear();

    std::string line;
    while (std::getline(in, line)) {
      if (line.empty())
        continue;

      std::vector<std::string> cols;
      std::string tok;
      bool e = false;
      for (char c : line) {
        if (!e && c == '\\') {
          e = true;
          tok += c;
          continue;
        }
        if (!e && c == '|') {
          cols.push_back(tok);
          tok.clear();
          continue;
        }
        e = false;
        tok += c;
      }
      cols.push_back(tok);

      if (cols.size() >= 8 && cols[0] == "HOST") {
        HostState h;
        h.name = unesc(cols[1]);
        h.ip = unesc(cols[2]);
        h.cpu = std::stof(cols[3]);
        h.ram = std::stof(cols[4]);
        h.disk = std::stof(cols[5]);
        h.lastSeen = (time_t)std::stoll(cols[6]);
        h.status = (HostStatus)std::stoi(cols[7]);
        if (cols.size() >= 12) {
          h.netRxKBps = std::stof(cols[8]);
          h.netTxKBps = std::stof(cols[9]);
          h.load1 = std::stof(cols[10]);
          h.procCount = std::stoi(cols[11]);
        }
        h.fd = -1;
        hosts_[h.name] = h;
      } else if (cols.size() >= 9 && cols[0] == "LOG") {
        LogEvent ev;
        ev.ts = (time_t)std::stoll(cols[1]);
        ev.host = unesc(cols[2]);
        ev.ip = unesc(cols[3]);
        ev.type = (LogEventType)std::stoi(cols[4]);
        ev.cpu = std::stof(cols[5]);
        ev.ram = std::stof(cols[6]);
        ev.disk = std::stof(cols[7]);
        ev.detail = unesc(cols[8]);
        log_.push_back(ev);
      }
    }

    // On restart, recovered hosts are stale until reconnect.
    for (auto &[_, h] : hosts_) {
      if (h.status != HostStatus::OFFLINE)
        h.status = HostStatus::WARNING;
    }

    return true;
  }

private:
  void pushLog(const LogEvent &ev) {
    log_.push_back(ev);
    if (log_.size() > MAX_LOG_ENTRIES)
      log_.pop_front();
  }

  mutable std::mutex mtx_;
  std::unordered_map<std::string, HostState> hosts_;
  std::deque<LogEvent> log_;
};

} // namespace monitor
