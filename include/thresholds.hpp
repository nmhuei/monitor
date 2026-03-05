#pragma once
/*
 * Threshold config loader.
 * Supports:
 *   CPU=80
 *   RAM=90
 *   DISK=85
 *   web-1.cpu=85
 *   db-server.ram=95
 */
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace monitor {

struct Thresholds {
    float cpu  = 80.0f;
    float ram  = 90.0f;
    float disk = 85.0f;
    // per-host overrides: key = "host.metric" (lower-case)
    std::unordered_map<std::string, float> perHost;

    float getCPU(const std::string& host) const {
        auto it = perHost.find(host + ".cpu");
        return it != perHost.end() ? it->second : cpu;
    }
    float getRAM(const std::string& host) const {
        auto it = perHost.find(host + ".ram");
        return it != perHost.end() ? it->second : ram;
    }
    float getDisk(const std::string& host) const {
        auto it = perHost.find(host + ".disk");
        return it != perHost.end() ? it->second : disk;
    }
};

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

inline Thresholds loadThresholds(const std::string& path) {
    Thresholds t;
    std::ifstream f(path);
    if (!f) return t;
    std::string line;
    while (std::getline(f, line)) {
        // strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = toLower(line.substr(0, eq));
        // trim
        key.erase(remove_if(key.begin(), key.end(), ::isspace), key.end());
        std::string val = line.substr(eq + 1);
        val.erase(remove_if(val.begin(), val.end(), ::isspace), val.end());
        if (val.empty()) continue;
        float v = std::stof(val);
        if      (key == "cpu")  t.cpu  = v;
        else if (key == "ram")  t.ram  = v;
        else if (key == "disk") t.disk = v;
        else                    t.perHost[key] = v;
    }
    return t;
}

} // namespace monitor
