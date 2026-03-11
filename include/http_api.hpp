#pragma once
/*
 * http_api.hpp — Minimal single-threaded HTTP/1.1 API server.
 *
 * Endpoints:
 *   GET /api/hosts            → JSON array of all hosts
 *   GET /api/history/<host>   → JSON history (query: ?n=30)
 *   GET /api/log              → JSON event log (query: ?n=50)
 *   GET /api/stats            → Server internal metrics JSON
 *   GET /metrics              → Prometheus text format
 *   GET /healthz              → 200 OK plain text
 *
 * No external deps — raw POSIX sockets only.
 */
#include "logger.hpp"
#include "metrics_store.hpp"
#include "server_stats.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace monitor {

// ── Tiny HTTP helpers ────────────────────────────────────────────────────────
inline std::string httpResponse(int code, const std::string &ct,
                                const std::string &body) {
    const char *reason = (code == 200) ? "OK"
                       : (code == 404) ? "Not Found"
                       : (code == 405) ? "Method Not Allowed"
                                       : "Bad Request";
    std::string r = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
    r += "Content-Type: " + ct + "\r\n";
    r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    r += "Access-Control-Allow-Origin: *\r\n";
    r += "Connection: close\r\n\r\n";
    r += body;
    return r;
}

inline std::string httpJson(const std::string &json) {
    return httpResponse(200, "application/json", json);
}

// Parse URL path and query string from first request line
inline void parseRequestLine(const std::string &line,
                              std::string &method,
                              std::string &path,
                              std::string &query) {
    method.clear(); path.clear(); query.clear();
    auto s1 = line.find(' ');
    if (s1 == std::string::npos) return;
    method = line.substr(0, s1);
    auto s2 = line.find(' ', s1 + 1);
    std::string url = (s2 == std::string::npos)
                    ? line.substr(s1 + 1)
                    : line.substr(s1 + 1, s2 - s1 - 1);
    auto q = url.find('?');
    if (q == std::string::npos) { path = url; }
    else { path = url.substr(0, q); query = url.substr(q + 1); }
}

// Extract a query param value: ?n=50 → getParam("n","50") = "50"
inline std::string getParam(const std::string &query, const std::string &key,
                             const std::string &def = "") {
    std::string needle = key + "=";
    auto pos = query.find(needle);
    if (pos == std::string::npos) return def;
    pos += needle.size();
    auto end = query.find('&', pos);
    return (end == std::string::npos) ? query.substr(pos) : query.substr(pos, end - pos);
}

// ── HTTP request handler ────────────────────────────────────────────────────
inline void handleHttpClient(int fd, MetricsStore &store, ServerStats &stats) {
    // Read request (max 8KB header)
    char buf[8192];
    int total = 0;
    struct timeval tv{}; tv.tv_sec = 5;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (total < (int)sizeof(buf) - 1) {
        int n = recv(fd, buf + total, sizeof(buf) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") || strstr(buf, "\n\n")) break;
    }
    if (total == 0) { close(fd); return; }
    buf[total] = '\0';

    // Parse first line
    std::string req(buf);
    auto nl = req.find('\n');
    std::string firstLine = (nl == std::string::npos) ? req : req.substr(0, nl);
    // Strip \r
    if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();

    std::string method, path, query;
    parseRequestLine(firstLine, method, path, query);

    if (method != "GET") {
        std::string r = httpResponse(405, "text/plain", "Method Not Allowed");
        send(fd, r.c_str(), r.size(), MSG_NOSIGNAL);
        close(fd); return;
    }

    std::string resp;

    if (path == "/healthz") {
        resp = httpResponse(200, "text/plain", "ok\n");

    } else if (path == "/metrics") {
        resp = httpResponse(200, "text/plain; version=0.0.4", stats.toPrometheus());

    } else if (path == "/api/stats") {
        resp = httpJson(stats.toJson());

    } else if (path == "/api/hosts") {
        resp = httpJson(store.hostsJson());

    } else if (path.rfind("/api/history/", 0) == 0) {
        std::string host = path.substr(13); // after "/api/history/"
        int n = 30;
        std::string ns = getParam(query, "n", "30");
        try { n = std::stoi(ns); } catch(...) {}
        n = std::max(1, std::min(n, 1000));
        if (host.empty())
            resp = httpResponse(400, "application/json", "{\"error\":\"missing host\"}");
        else
            resp = httpJson(store.historyJson(host, n));

    } else if (path == "/api/log") {
        int n = 50;
        std::string ns = getParam(query, "n", "50");
        try { n = std::stoi(ns); } catch(...) {}
        n = std::max(1, std::min(n, 1000));
        resp = httpJson(store.logJson(n));

    } else {
        resp = httpResponse(404, "application/json", "{\"error\":\"not found\"}");
    }

    send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
    close(fd);
}

// ── HTTP API Server (runs in its own thread) ─────────────────────────────────
class HttpApiServer {
public:
    HttpApiServer(MetricsStore &store, ServerStats &stats)
        : store_(store), stats_(stats) {}

    // Start listening on given port; returns false if bind fails
    bool start(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int opt = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (bind(fd_, (sockaddr *)&addr, sizeof(addr)) < 0 ||
            listen(fd_, 32) < 0) {
            close(fd_); fd_ = -1; return false;
        }
        port_ = port;
        thread_ = std::thread(&HttpApiServer::acceptLoop, this);
        return true;
    }

    void stop() {
        running_ = false;
        if (fd_ >= 0) { shutdown(fd_, SHUT_RDWR); close(fd_); fd_ = -1; }
        if (thread_.joinable()) thread_.join();
    }

    ~HttpApiServer() { stop(); }

private:
    void acceptLoop() {
        while (running_) {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            int client = accept(fd_, (sockaddr *)&addr, &len);
            if (client < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                break;
            }
            // Handle inline (one request at a time is fine for monitoring API)
            // Use a short-lived thread to avoid blocking accept
            std::thread([this, client]() {
                handleHttpClient(client, store_, stats_);
            }).detach();
        }
    }

    MetricsStore  &store_;
    ServerStats   &stats_;
    int            fd_     = -1;
    uint16_t       port_   = 8786;
    std::atomic<bool> running_{true};
    std::thread    thread_;
};

} // namespace monitor
