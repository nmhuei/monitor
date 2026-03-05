#pragma once
/*
 * Thread-safe per-host metric store used by the server.
 */
#include "protocol.hpp"
#include "thresholds.hpp"
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>
#include <chrono>

namespace monitor {

struct HistorySample {
    time_t ts;
    float  cpu, ram, disk;
};

struct HostState {
    std::string   name;
    std::string   ip;
    float         cpu  = 0;
    float         ram  = 0;
    float         disk = 0;
    time_t        lastSeen = 0;
    HostStatus    status   = HostStatus::OFFLINE;
    std::deque<HistorySample> history; // newest at back
    int           fd = -1;            // socket fd

    void push(float c, float r, float d, time_t ts) {
        cpu  = c; ram = r; disk = d; lastSeen = ts;
        history.push_back({ts, c, r, d});
        if (history.size() > MAX_HISTORY)
            history.pop_front();
    }

    // Returns last N history entries (or fewer if not enough data)
    std::vector<HistorySample> recent(size_t n) const {
        if (history.size() <= n) return {history.begin(), history.end()};
        return {history.end() - n, history.end()};
    }
};

// Log event types
enum class LogEventType { CONNECT, METRIC, ALERT, DISCONNECT };

struct LogEvent {
    time_t       ts;
    std::string  host;
    std::string  ip;
    LogEventType type;
    float        cpu = 0, ram = 0, disk = 0;
    std::string  detail; // e.g. "CPU=87%"
};

class MetricsStore {
public:
    void upsert(const MetricPayload& p, const Thresholds& thresh) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto& h = hosts_[p.host];
        h.name = p.host;
        h.ip   = p.ip;
        h.push(p.cpu, p.ram, p.disk, p.timestamp);

        // Determine status
        bool alert = (p.cpu  >= thresh.getCPU(p.host))  ||
                     (p.ram  >= thresh.getRAM(p.host))   ||
                     (p.disk >= thresh.getDisk(p.host));
        bool warn  = (p.cpu  >= thresh.getCPU(p.host)  * 0.85f) ||
                     (p.ram  >= thresh.getRAM(p.host)   * 0.85f) ||
                     (p.disk >= thresh.getDisk(p.host)  * 0.85f);
        h.status = alert ? HostStatus::ALERT
                 : warn  ? HostStatus::WARNING
                          : HostStatus::ONLINE;

        // Build log event
        LogEvent ev;
        ev.ts   = p.timestamp;
        ev.host = p.host;
        ev.ip   = p.ip;
        ev.cpu  = p.cpu; ev.ram = p.ram; ev.disk = p.disk;
        if (alert) {
            ev.type = LogEventType::ALERT;
            if (p.cpu  >= thresh.getCPU(p.host))
                ev.detail += "CPU="  + std::to_string((int)p.cpu)  + "% ";
            if (p.ram  >= thresh.getRAM(p.host))
                ev.detail += "RAM="  + std::to_string((int)p.ram)  + "% ";
            if (p.disk >= thresh.getDisk(p.host))
                ev.detail += "DISK=" + std::to_string((int)p.disk) + "% ";
        } else {
            ev.type = LogEventType::METRIC;
        }
        pushLog(ev);
    }

    void setOnline(const std::string& host, const std::string& ip, int fd) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto& h = hosts_[host];
        h.name   = host;
        h.ip     = ip;
        h.fd     = fd;
        h.status = HostStatus::ONLINE;
        h.lastSeen = time(nullptr);
        LogEvent ev;
        ev.ts = time(nullptr); ev.host = host; ev.ip = ip;
        ev.type = LogEventType::CONNECT;
        pushLog(ev);
    }

    void setOffline(const std::string& host) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = hosts_.find(host);
        if (it != hosts_.end()) {
            it->second.status = HostStatus::OFFLINE;
            it->second.fd     = -1;
        }
        LogEvent ev;
        ev.ts   = time(nullptr);
        ev.host = host;
        ev.ip   = (it != hosts_.end()) ? it->second.ip : "";
        ev.type = LogEventType::DISCONNECT;
        pushLog(ev);
    }

    // Snapshot for rendering (no lock held by caller)
    std::vector<HostState> snapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<HostState> v;
        v.reserve(hosts_.size());
        for (auto& [k, h] : hosts_) v.push_back(h);
        return v;
    }

    std::vector<LogEvent> logSnapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return {log_.begin(), log_.end()};
    }

private:
    void pushLog(const LogEvent& ev) {
        log_.push_back(ev);
        if (log_.size() > MAX_LOG_ENTRIES)
            log_.pop_front();
    }

    mutable std::mutex mtx_;
    std::unordered_map<std::string, HostState> hosts_;
    std::deque<LogEvent> log_;
};

} // namespace monitor
