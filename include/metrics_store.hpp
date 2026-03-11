#pragma once
/*
 * Thread-safe per-host metric store.
 * Fixed: pipe-escaped state file serialization, path traversal guard.
 */
#include "protocol.hpp"
#include "thresholds.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace monitor {

// ── Serialization helpers ─────────────────────────────────────────────────────
// Encode a string so it contains no '|' (pipe-escape with \P)
inline std::string pipeEncode(const std::string &s) {
  std::string out; out.reserve(s.size());
  for (char c : s) {
    if      (c == '\\') out += "\\\\";
    else if (c == '|')  out += "\\P";
    else                out += c;
  }
  return out;
}

inline std::string pipeDecode(const std::string &s) {
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i+1 < s.size()) {
      i++;
      if      (s[i] == '\\') out += '\\';
      else if (s[i] == 'P')  out += '|';
      else { out += '\\'; out += s[i]; }
    } else out += s[i];
  }
  return out;
}

// Validate that a resolved path stays inside a base directory
inline bool isPathSafe(const std::string &path, const std::string &base) {
  try {
    auto abs = std::filesystem::weakly_canonical(path);
    auto absBase = std::filesystem::weakly_canonical(base);
    // path must start with base
    auto rel = std::filesystem::relative(abs, absBase);
    std::string rs = rel.string();
    return rs.rfind("..", 0) != 0;
  } catch (...) { return false; }
}

// ── Data structures ───────────────────────────────────────────────────────────
struct HistorySample {
  time_t ts;
  float cpu, ram, disk;
  float netRxKB=0, netTxKB=0, loadAvg=0;
  int procCount=0;
};

struct HostState {
  std::string name, ip;
  float cpu=0, ram=0, disk=0;
  float netRxKB=0, netTxKB=0, loadAvg=0;
  int procCount=0;
  time_t lastSeen=0;
  HostStatus status=HostStatus::OFFLINE;
  std::deque<HistorySample> history;
  int fd=-1;
  std::vector<float> cores;
  int coreCount=0;

  void push(const MetricPayload &p) {
    cpu=p.cpu; ram=p.ram; disk=p.disk;
    netRxKB=p.netRxKB; netTxKB=p.netTxKB;
    loadAvg=p.loadAvg; procCount=p.procCount;
    lastSeen=p.timestamp;
    cores=p.cores; coreCount=(int)p.cores.size();
    history.push_back({p.timestamp, p.cpu, p.ram, p.disk,
                       p.netRxKB, p.netTxKB, p.loadAvg, p.procCount});
    if (history.size() > MAX_HISTORY) history.pop_front();
  }
};

enum class LogEventType { CONNECT, METRIC, ALERT, DISCONNECT };

struct LogEvent {
  time_t ts;
  std::string host, ip;
  LogEventType type;
  float cpu=0, ram=0, disk=0;
  std::string detail;
};

// ── MetricsStore ──────────────────────────────────────────────────────────────
class MetricsStore {
public:
  void upsert(const MetricPayload &p, const Thresholds &thresh) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto &h = hosts_[p.host];
    h.name=p.host; h.ip=p.ip;
    h.push(p);

    bool alert = (p.cpu  >= thresh.getCPU(p.host))  ||
                 (p.ram  >= thresh.getRAM(p.host))  ||
                 (p.disk >= thresh.getDisk(p.host));
    bool warn  = (p.cpu  >= thresh.getCPU(p.host)  * 0.85f) ||
                 (p.ram  >= thresh.getRAM(p.host)  * 0.85f) ||
                 (p.disk >= thresh.getDisk(p.host) * 0.85f);
    h.status = alert  ? HostStatus::ALERT
             : warn   ? HostStatus::WARNING
                      : HostStatus::ONLINE;

    LogEvent ev;
    ev.ts=p.timestamp; ev.host=p.host; ev.ip=p.ip;
    ev.cpu=p.cpu; ev.ram=p.ram; ev.disk=p.disk;
    if (alert) {
      ev.type=LogEventType::ALERT;
      if (p.cpu  >= thresh.getCPU(p.host))
        ev.detail += "CPU="+std::to_string((int)p.cpu)+"% ";
      if (p.ram  >= thresh.getRAM(p.host))
        ev.detail += "RAM="+std::to_string((int)p.ram)+"% ";
      if (p.disk >= thresh.getDisk(p.host))
        ev.detail += "DSK="+std::to_string((int)p.disk)+"% ";
    } else {
      ev.type=LogEventType::METRIC;
    }
    pushLog(ev);
  }

  void setOnline(const std::string &host, const std::string &ip, int fd) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto &h = hosts_[host];
    h.name=host; h.ip=ip; h.fd=fd;
    h.status=HostStatus::ONLINE; h.lastSeen=time(nullptr);
    LogEvent ev; ev.ts=time(nullptr); ev.host=host; ev.ip=ip;
    ev.type=LogEventType::CONNECT; pushLog(ev);
  }

  void setOffline(const std::string &host) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it=hosts_.find(host);
    if (it!=hosts_.end()) { it->second.status=HostStatus::OFFLINE; it->second.fd=-1; }
    LogEvent ev; ev.ts=time(nullptr); ev.host=host;
    ev.ip=(it!=hosts_.end()?it->second.ip:"");
    ev.type=LogEventType::DISCONNECT; pushLog(ev);
  }

  std::vector<HostState> snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<HostState> v; v.reserve(hosts_.size());
    for (auto &[k,h] : hosts_) v.push_back(h);
    return v;
  }

  std::vector<LogEvent> logSnapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return {log_.begin(), log_.end()};
  }

  bool saveToFile(const std::string &path) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto &[n,h] : hosts_) {
      out << "HOST|" << pipeEncode(h.name) << "|" << pipeEncode(h.ip)
          << "|" << h.cpu << "|" << h.ram << "|" << h.disk
          << "|" << (long long)h.lastSeen << "|" << (int)h.status << "\n";
    }
    for (const auto &ev : log_) {
      out << "LOG|" << (long long)ev.ts << "|" << pipeEncode(ev.host)
          << "|" << pipeEncode(ev.ip) << "|" << (int)ev.type
          << "|" << ev.cpu << "|" << ev.ram << "|" << ev.disk
          << "|" << pipeEncode(ev.detail) << "\n";
    }
    return true;
  }

  bool loadFromFile(const std::string &path) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ifstream in(path);
    if (!in) return false;
    hosts_.clear(); log_.clear();
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      std::vector<std::string> cols;
      std::stringstream ss(line); std::string tok;
      while (std::getline(ss, tok, '|')) cols.push_back(tok);

      if (cols.size() >= 8 && cols[0]=="HOST") {
        HostState h;
        h.name     = pipeDecode(cols[1]);
        h.ip       = pipeDecode(cols[2]);
        try {
          h.cpu      = std::stof(cols[3]);
          h.ram      = std::stof(cols[4]);
          h.disk     = std::stof(cols[5]);
          h.lastSeen = (time_t)std::stoll(cols[6]);
          h.status   = (HostStatus)std::stoi(cols[7]);
        } catch (...) {}
        h.fd=-1;
        hosts_[h.name]=h;
      } else if (cols.size() >= 9 && cols[0]=="LOG") {
        LogEvent ev;
        try {
          ev.ts     = (time_t)std::stoll(cols[1]);
          ev.host   = pipeDecode(cols[2]);
          ev.ip     = pipeDecode(cols[3]);
          ev.type   = (LogEventType)std::stoi(cols[4]);
          ev.cpu    = std::stof(cols[5]);
          ev.ram    = std::stof(cols[6]);
          ev.disk   = std::stof(cols[7]);
          ev.detail = pipeDecode(cols[8]);
        } catch (...) {}
        log_.push_back(ev);
      }
    }
    for (auto &[_,h] : hosts_)
      if (h.status != HostStatus::OFFLINE) h.status=HostStatus::WARNING;
    return true;
  }

private:
  void pushLog(const LogEvent &ev) {
    log_.push_back(ev);
    if (log_.size() > MAX_LOG_ENTRIES) log_.pop_front();
  }
  mutable std::mutex mtx_;
  std::unordered_map<std::string, HostState> hosts_;
  std::deque<LogEvent> log_;
};

} // namespace monitor
