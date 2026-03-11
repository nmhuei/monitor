#pragma once
/*
 * server_stats.hpp — Internal server metrics (thread-safe, atomic).
 * Tracks: agents connected, total messages received, dropped, alerts sent,
 * server uptime. Exposed via HTTP API and CMD protocol.
 */
#include <atomic>
#include <ctime>
#include <string>

namespace monitor {

struct ServerStats {
    std::atomic<int>      agentsOnline{0};
    std::atomic<int>      agentsStale{0};
    std::atomic<uint64_t> msgsTotal{0};
    std::atomic<uint64_t> msgsDropped{0};   // parse errors / auth failures
    std::atomic<uint64_t> alertsSent{0};
    std::atomic<uint64_t> viewerConnects{0};
    time_t                startTime{0};

    void reset() {
        agentsOnline  = 0;
        agentsStale   = 0;
        msgsTotal     = 0;
        msgsDropped   = 0;
        alertsSent    = 0;
        viewerConnects= 0;
        startTime     = time(nullptr);
    }

    std::string toJson() const {
        char buf[512];
        long long uptime = (long long)(time(nullptr) - startTime);
        snprintf(buf, sizeof(buf),
            "{\"agents_online\":%d,\"agents_stale\":%d,"
            "\"msgs_total\":%llu,\"msgs_dropped\":%llu,"
            "\"alerts_sent\":%llu,\"viewer_connects\":%llu,"
            "\"uptime_sec\":%lld}",
            agentsOnline.load(), agentsStale.load(),
            (unsigned long long)msgsTotal.load(),
            (unsigned long long)msgsDropped.load(),
            (unsigned long long)alertsSent.load(),
            (unsigned long long)viewerConnects.load(),
            uptime);
        return buf;
    }

    // Prometheus text format
    std::string toPrometheus() const {
        long long uptime = (long long)(time(nullptr) - startTime);
        std::string o;
        auto line = [&](const char *name, const char *help, long long val) {
            o += "# HELP monitor_"; o += name; o += ' '; o += help; o += '\n';
            o += "# TYPE monitor_"; o += name; o += " gauge\n";
            o += "monitor_"; o += name; o += ' ';
            o += std::to_string(val); o += '\n';
        };
        line("agents_online",   "Number of currently connected agents",   agentsOnline.load());
        line("agents_stale",    "Number of stale agents",                  agentsStale.load());
        line("msgs_total",      "Total metric messages received",          (long long)msgsTotal.load());
        line("msgs_dropped",    "Messages dropped (parse/auth errors)",    (long long)msgsDropped.load());
        line("alerts_sent",     "Total alert webhook calls made",          (long long)alertsSent.load());
        line("viewer_connects", "Total viewer client connections",         (long long)viewerConnects.load());
        line("uptime_seconds",  "Server uptime in seconds",                uptime);
        return o;
    }
};

} // namespace monitor
