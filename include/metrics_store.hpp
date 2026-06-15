#pragma once
/*
 * Thread-safe per-host metric store.
 * v3: Sharded lock, JSONL atomic persistence, nlohmann/json integration.
 */
#include "protocol.hpp"
#include "thresholds.hpp"
#include "third_party/json.hpp"
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

// ── Serialization helpers (Legacy compatibility) ───────────────────────────
inline std::string pipeEncode(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '\\')
      out += "\\\\";
    else if (c == '|')
      out += "\\P";
    else
      out += c;
  }
  return out;
}

inline std::string pipeDecode(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      i++;
      if (s[i] == '\\')
        out += '\\';
      else if (s[i] == 'P')
        out += '|';
      else {
        out += '\\';
        out += s[i];
      }
    } else
      out += s[i];
  }
  return out;
}

// Minimal JSON string escape helper for custom JSON output
inline std::string jsonStr(const std::string &s) {
  std::string out;
  out += '"';
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else
      out += c;
  }
  out += '"';
  return out;
}

// ── Data structures ───────────────────────────────────────────────────────────
struct HistorySample {
  time_t ts;
  float cpu, ram, disk;
  float netRxKB = 0, netTxKB = 0, loadAvg = 0;
  int procCount = 0;
};

struct HostState {
  std::string name, ip;
  float cpu = 0, ram = 0, disk = 0;
  float netRxKB = 0, netTxKB = 0, loadAvg = 0;
  int procCount = 0;
  time_t lastSeen = 0;
  HostStatus status = HostStatus::OFFLINE;
  std::deque<HistorySample> history;
  int fd = -1;
  std::vector<float> cores;
  int coreCount = 0;

  void push(const MetricPayload &p) {
    cpu = p.cpu;
    ram = p.ram;
    disk = p.disk;
    netRxKB = p.netRxKB;
    netTxKB = p.netTxKB;
    loadAvg = p.loadAvg;
    procCount = p.procCount;
    lastSeen = p.timestamp;
    cores = p.cores;
    coreCount = (int)p.cores.size();
    history.push_back({p.timestamp, p.cpu, p.ram, p.disk, p.netRxKB, p.netTxKB,
                       p.loadAvg, p.procCount});
    if (history.size() > MAX_HISTORY)
      history.pop_front();
  }
};

enum class LogEventType { CONNECT, METRIC, ALERT, DISCONNECT, STALE };

struct LogEvent {
  time_t ts;
  std::string host, ip;
  LogEventType type;
  float cpu = 0, ram = 0, disk = 0;
  std::string detail;
};

// ── MetricsStore ──────────────────────────────────────────────────────────────
class MetricsStore {
public:
  std::pair<HostStatus, HostStatus> upsert(const MetricPayload &p, const Thresholds &thresh) {
    HostStatus prev;
    HostStatus cur;
    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      auto &h = hosts_[p.host];
      prev = h.status;
      h.name = p.host;
      h.ip = p.ip;
      h.push(p);

      bool alert = (p.cpu >= thresh.getCPU(p.host)) ||
                   (p.ram >= thresh.getRAM(p.host)) ||
                   (p.disk >= thresh.getDisk(p.host));
      bool warn = (p.cpu >= thresh.getCPU(p.host) * 0.85f) ||
                  (p.ram >= thresh.getRAM(p.host) * 0.85f) ||
                  (p.disk >= thresh.getDisk(p.host) * 0.85f);
      h.status = alert  ? HostStatus::ALERT
                 : warn ? HostStatus::WARNING
                        : HostStatus::ONLINE;
      cur = h.status;
    }

    LogEvent ev;
    ev.ts = p.timestamp;
    ev.host = p.host;
    ev.ip = p.ip;
    ev.cpu = p.cpu;
    ev.ram = p.ram;
    ev.disk = p.disk;
    bool alert = (p.cpu >= thresh.getCPU(p.host)) ||
                 (p.ram >= thresh.getRAM(p.host)) ||
                 (p.disk >= thresh.getDisk(p.host));
    if (alert) {
      ev.type = LogEventType::ALERT;
      if (p.cpu >= thresh.getCPU(p.host))
        ev.detail += "CPU=" + std::to_string((int)p.cpu) + "% ";
      if (p.ram >= thresh.getRAM(p.host))
        ev.detail += "RAM=" + std::to_string((int)p.ram) + "% ";
      if (p.disk >= thresh.getDisk(p.host))
        ev.detail += "DSK=" + std::to_string((int)p.disk) + "% ";
    } else {
      ev.type = LogEventType::METRIC;
    }

    {
      std::lock_guard<std::mutex> lk(logMtx_);
      pushLog(ev);
    }
    return {prev, cur};
  }

  void setOnline(const std::string &host, const std::string &ip, int fd) {
    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      auto &h = hosts_[host];
      h.name = host;
      h.ip = ip;
      h.fd = fd;
      h.status = HostStatus::ONLINE;
      h.lastSeen = time(nullptr);
    }
    LogEvent ev;
    ev.ts = time(nullptr);
    ev.host = host;
    ev.ip = ip;
    ev.type = LogEventType::CONNECT;
    {
      std::lock_guard<std::mutex> lk(logMtx_);
      pushLog(ev);
    }
  }

  void setOffline(const std::string &host) {
    std::string ip;
    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      auto it = hosts_.find(host);
      if (it != hosts_.end()) {
        it->second.status = HostStatus::OFFLINE;
        it->second.fd = -1;
        ip = it->second.ip;
      }
    }
    LogEvent ev;
    ev.ts = time(nullptr);
    ev.host = host;
    ev.ip = ip;
    ev.type = LogEventType::DISCONNECT;
    {
      std::lock_guard<std::mutex> lk(logMtx_);
      pushLog(ev);
    }
  }

  // Called by stale-checker thread every few seconds
  // Returns {online_count, stale_count}
  std::pair<int,int> markStaleOffline(int staleSec, int offlineSec) {
    time_t now = time(nullptr);
    int online = 0, stale = 0;
    std::vector<LogEvent> eventsToPush;
    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      for (auto &[name, h] : hosts_) {
        if (h.status == HostStatus::OFFLINE) continue;
        if (h.lastSeen == 0) continue;
        time_t age = now - h.lastSeen;
        if (age >= offlineSec) {
          if (h.status != HostStatus::OFFLINE) {
            h.status = HostStatus::OFFLINE;
            h.fd = -1;
            LogEvent ev;
            ev.ts = now; ev.host = name; ev.ip = h.ip;
            ev.type = LogEventType::DISCONNECT;
            ev.detail = "timeout (" + std::to_string(age) + "s)";
            eventsToPush.push_back(ev);
          }
        } else if (age >= staleSec) {
          if (h.status != HostStatus::STALE && h.status != HostStatus::OFFLINE) {
            h.status = HostStatus::STALE;
            LogEvent ev;
            ev.ts = now; ev.host = name; ev.ip = h.ip;
            ev.type = LogEventType::STALE;
            ev.detail = "no metric for " + std::to_string(age) + "s";
            eventsToPush.push_back(ev);
          }
          stale++;
        } else {
          if (h.status == HostStatus::ONLINE || h.status == HostStatus::WARNING ||
              h.status == HostStatus::ALERT)
            online++;
        }
      }
    }
    if (!eventsToPush.empty()) {
      std::lock_guard<std::mutex> lk(logMtx_);
      for (const auto &ev : eventsToPush) {
        pushLog(ev);
      }
    }
    return {online, stale};
  }

  // Update lastSeen without a full metric push (heartbeat)
  void touchLastSeen(const std::string &host) {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    auto it = hosts_.find(host);
    if (it != hosts_.end())
      it->second.lastSeen = time(nullptr);
  }

  std::vector<HostState> snapshot() const {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    std::vector<HostState> v;
    v.reserve(hosts_.size());
    for (auto &[k, h] : hosts_)
      v.push_back(h);
    return v;
  }

  std::vector<LogEvent> logSnapshot() const {
    std::lock_guard<std::mutex> lk(logMtx_);
    return {log_.begin(), log_.end()};
  }

  // ── Query helpers for viewer protocol ────────────────────────────────────
  std::string hostsJson() const {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    std::string o = "[";
    bool first = true;
    // Sort by status severity then name
    std::vector<const HostState *> sorted;
    for (auto &[k, h] : hosts_)
      sorted.push_back(&h);
    std::sort(sorted.begin(), sorted.end(),
              [](const HostState *a, const HostState *b) {
                if (a->status != b->status)
                  return (int)a->status < (int)b->status;
                return a->name < b->name;
              });
    for (auto *h : sorted) {
      if (!first)
        o += ',';
      first = false;
      char buf[256];
      snprintf(buf, sizeof(buf),
               "{\"host\":%s,\"ip\":%s,\"status\":%s,"
               "\"cpu\":%.1f,\"ram\":%.1f,\"disk\":%.1f,"
               "\"load\":%.2f,\"procs\":%d,\"lastSeen\":%lld}",
               jsonStr(h->name).c_str(), jsonStr(h->ip).c_str(),
               jsonStr(statusStr(h->status)).c_str(), h->cpu, h->ram, h->disk,
               h->loadAvg, h->procCount, (long long)h->lastSeen);
      o += buf;
    }
    o += ']';
    return o;
  }

  std::string historyJson(const std::string &host, int n) const {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    auto it = hosts_.find(host);
    if (it == hosts_.end())
      return "[]";
    const auto &hist = it->second.history;
    int start = std::max(0, (int)hist.size() - n);
    std::string o = "[";
    for (int i = start; i < (int)hist.size(); i++) {
      if (i > start)
        o += ',';
      const auto &s = hist[i];
      char buf[256];
      snprintf(buf, sizeof(buf),
               "{\"ts\":%lld,\"cpu\":%.1f,\"ram\":%.1f,\"disk\":%.1f,"
               "\"rx\":%.1f,\"tx\":%.1f,\"load\":%.2f}",
               (long long)s.ts, s.cpu, s.ram, s.disk, s.netRxKB, s.netTxKB,
               s.loadAvg);
      o += buf;
    }
    o += ']';
    return o;
  }

  std::string logJson(int n) const {
    std::lock_guard<std::mutex> lk(logMtx_);
    int start = std::max(0, (int)log_.size() - n);
    std::string o = "[";
    static const char *typeNames[] = {"CONNECT", "METRIC", "ALERT",
                                      "DISCONNECT", "STALE"};
    for (int i = start; i < (int)log_.size(); i++) {
      if (i > start)
        o += ',';
      const auto &ev = log_[i];
      int ti = std::min((int)ev.type, 4);
      char buf[512];
      snprintf(buf, sizeof(buf),
               "{\"ts\":%lld,\"host\":%s,\"ip\":%s,\"type\":\"%s\","
               "\"cpu\":%.1f,\"ram\":%.1f,\"disk\":%.1f,\"detail\":%s}",
               (long long)ev.ts, jsonStr(ev.host).c_str(),
               jsonStr(ev.ip).c_str(), typeNames[ti], ev.cpu, ev.ram, ev.disk,
               jsonStr(ev.detail).c_str());
      o += buf;
    }
    o += ']';
    return o;
  }

  // ── Persistence ──────────────────────────────────────────────────────────

  // Configure the directory where per-host JSONL history files are stored.
  void setHistoryDir(const std::string &dir, int maxLines) {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    historyDir_ = dir;
    historyMaxLines_ = maxLines;
    std::filesystem::create_directories(dir);
  }

  // Pre-load up to `n` recent history samples per host from JSONL files in
  // historyDir_. Files are named <host>.jsonl, one JSON object per line.
  void loadHistoryFiles(int n) {
    std::lock_guard<std::mutex> lk(hostsMtx_);
    if (historyDir_.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(historyDir_, ec)) return;
    for (auto &entry : std::filesystem::directory_iterator(historyDir_, ec)) {
      if (entry.path().extension() != ".jsonl") continue;
      std::string host = entry.path().stem().string();
      std::ifstream in(entry.path());
      if (!in) continue;
      std::vector<HistorySample> samples;
      std::string line;
      while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
          auto j = nlohmann::json::parse(line);
          HistorySample s;
          s.ts        = static_cast<time_t>(j.value("ts", 0LL));
          s.cpu       = j.value("cpu", 0.0f);
          s.ram       = j.value("ram", 0.0f);
          s.disk      = j.value("disk", 0.0f);
          s.netRxKB   = j.value("rx", 0.0f);
          s.netTxKB   = j.value("tx", 0.0f);
          s.loadAvg   = j.value("load", 0.0f);
          s.procCount = j.value("procs", 0);
          samples.push_back(s);
        } catch (...) {
          // Skip corrupt history line
        }
      }
      // Keep only last n samples
      int start = std::max(0, (int)samples.size() - n);
      auto &h = hosts_[host];
      if (h.name.empty()) h.name = host;
      for (int i = start; i < (int)samples.size(); i++)
        h.history.push_back(samples[i]);
      if ((int)h.history.size() > MAX_HISTORY)
        h.history.erase(h.history.begin(),
                        h.history.begin() + (h.history.size() - MAX_HISTORY));
    }
  }

  bool saveToFile(const std::string &path) const {
    std::vector<HostState> hostsCopy;
    std::vector<LogEvent> logCopy;
    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      for (const auto &[n, h] : hosts_) {
        hostsCopy.push_back(h);
      }
    }
    {
      std::lock_guard<std::mutex> lk(logMtx_);
      for (const auto &ev : log_) {
        logCopy.push_back(ev);
      }
    }

    std::string tmpPath = path + ".tmp";
    std::ofstream out(tmpPath, std::ios::trunc);
    if (!out)
      return false;

    try {
      for (const auto &h : hostsCopy) {
        nlohmann::json j;
        j["type"] = "host";
        j["name"] = h.name;
        j["ip"] = h.ip;
        j["cpu"] = h.cpu;
        j["ram"] = h.ram;
        j["disk"] = h.disk;
        j["lastSeen"] = static_cast<long long>(h.lastSeen);
        j["status"] = static_cast<int>(h.status);
        j["cores"] = h.cores;
        j["netRxKB"] = h.netRxKB;
        j["netTxKB"] = h.netTxKB;
        j["loadAvg"] = h.loadAvg;
        j["procCount"] = h.procCount;
        out << j.dump() << "\n";
      }
      for (const auto &ev : logCopy) {
        nlohmann::json j;
        j["type"] = "log";
        j["ts"] = static_cast<long long>(ev.ts);
        j["host"] = ev.host;
        j["ip"] = ev.ip;
        j["event"] = static_cast<int>(ev.type);
        j["cpu"] = ev.cpu;
        j["ram"] = ev.ram;
        j["disk"] = ev.disk;
        j["detail"] = ev.detail;
        out << j.dump() << "\n";
      }
      out.close();

      std::error_code ec;
      std::filesystem::rename(tmpPath, path, ec);
      return !ec;
    } catch (...) {
      return false;
    }
  }

  bool loadFromFile(const std::string &path) {
    std::ifstream in(path);
    if (!in)
      return false;

    std::vector<HostState> loadedHosts;
    std::vector<LogEvent> loadedLog;
    std::string line;

    while (std::getline(in, line)) {
      if (line.empty())
        continue;

      // Migration: support old pipe-delimited format
      if (line.rfind("HOST|", 0) == 0 || line.rfind("LOG|", 0) == 0) {
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, '|'))
          cols.push_back(tok);

        if (cols.size() >= 8 && cols[0] == "HOST") {
          HostState h;
          h.name = pipeDecode(cols[1]);
          h.ip = pipeDecode(cols[2]);
          try {
            h.cpu = std::stof(cols[3]);
            h.ram = std::stof(cols[4]);
            h.disk = std::stof(cols[5]);
            h.lastSeen = (time_t)std::stoll(cols[6]);
            h.status = HostStatus::STALE;
          } catch (...) {
            continue;
          }
          h.fd = -1;
          loadedHosts.push_back(h);
        } else if (cols.size() >= 9 && cols[0] == "LOG") {
          LogEvent ev;
          try {
            ev.ts = (time_t)std::stoll(cols[1]);
            ev.host = pipeDecode(cols[2]);
            ev.ip = pipeDecode(cols[3]);
            int ti = std::stoi(cols[4]);
            if (ti < 0 || ti > 4)
              ti = 0;
            ev.type = (LogEventType)ti;
            ev.cpu = std::stof(cols[5]);
            ev.ram = std::stof(cols[6]);
            ev.disk = std::stof(cols[7]);
            ev.detail = pipeDecode(cols[8]);
            loadedLog.push_back(ev);
          } catch (...) {
            continue;
          }
        }
      } else {
        // Parse new JSONL format
        try {
          auto j = nlohmann::json::parse(line);
          if (!j.is_object()) continue;
          std::string type = j.value("type", "");
          if (type == "host") {
            HostState h;
            h.name = j.value("name", "");
            h.ip = j.value("ip", "");
            h.cpu = j.value("cpu", 0.0f);
            h.ram = j.value("ram", 0.0f);
            h.disk = j.value("disk", 0.0f);
            h.lastSeen = static_cast<time_t>(j.value("lastSeen", 0LL));
            h.status = HostStatus::STALE;
            h.fd = -1;
            h.netRxKB = j.value("netRxKB", 0.0f);
            h.netTxKB = j.value("netTxKB", 0.0f);
            h.loadAvg = j.value("loadAvg", 0.0f);
            h.procCount = j.value("procCount", 0);
            if (j.contains("cores") && j["cores"].is_array()) {
              for (auto& cv : j["cores"]) {
                if (cv.is_number()) h.cores.push_back(cv.get<float>());
              }
              h.coreCount = static_cast<int>(h.cores.size());
            }
            loadedHosts.push_back(h);
          } else if (type == "log") {
            LogEvent ev;
            ev.ts = static_cast<time_t>(j.value("ts", 0LL));
            ev.host = j.value("host", "");
            ev.ip = j.value("ip", "");
            int ti = j.value("event", 0);
            if (ti < 0 || ti > 4) ti = 0;
            ev.type = static_cast<LogEventType>(ti);
            ev.cpu = j.value("cpu", 0.0f);
            ev.ram = j.value("ram", 0.0f);
            ev.disk = j.value("disk", 0.0f);
            ev.detail = j.value("detail", "");
            loadedLog.push_back(ev);
          }
        } catch (...) {
          // Skip corrupt line
        }
      }
    }

    {
      std::lock_guard<std::mutex> lk(hostsMtx_);
      hosts_.clear();
      for (const auto &h : loadedHosts) {
        hosts_[h.name] = h;
      }
    }
    {
      std::lock_guard<std::mutex> lk(logMtx_);
      log_.clear();
      for (const auto &ev : loadedLog) {
        log_.push_back(ev);
      }
    }
    return true;
  }

private:
  void pushLog(const LogEvent &ev) {
    log_.push_back(ev);
    if (log_.size() > MAX_LOG_ENTRIES)
      log_.pop_front();
  }
  mutable std::mutex hostsMtx_;
  mutable std::mutex logMtx_;
  std::unordered_map<std::string, HostState> hosts_;
  std::deque<LogEvent> log_;
  std::string historyDir_;
  int historyMaxLines_ = 10000;
};

} // namespace monitor
