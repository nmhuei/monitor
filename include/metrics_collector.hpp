#pragma once
/*
 * Metrics collector — reads from /proc/stat, /proc/meminfo, statvfs()
 */
#include <string>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace monitor::metrics {

struct Sample {
    float cpu  = 0.0f;
    float ram  = 0.0f;
    float disk = 0.0f;
};

// Internal CPU state for delta calculation
struct CpuState {
    unsigned long long user = 0, nice = 0, sys = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
};

inline CpuState readCpuState() {
    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line);
    CpuState s;
    sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
           &s.user, &s.nice, &s.sys, &s.idle,
           &s.iowait, &s.irq, &s.softirq, &s.steal);
    return s;
}

inline float cpuPercent(const CpuState& prev, const CpuState& cur) {
    auto prevIdle  = prev.idle + prev.iowait;
    auto curIdle   = cur.idle  + cur.iowait;
    auto prevTotal = prev.user + prev.nice + prev.sys + prevIdle
                   + prev.irq  + prev.softirq + prev.steal;
    auto curTotal  = cur.user  + cur.nice  + cur.sys  + curIdle
                   + cur.irq   + cur.softirq  + cur.steal;
    auto dTotal = curTotal - prevTotal;
    auto dIdle  = curIdle  - prevIdle;
    if (dTotal == 0) return 0.0f;
    return 100.0f * (1.0f - static_cast<float>(dIdle) / static_cast<float>(dTotal));
}

inline float ramPercent() {
    std::ifstream f("/proc/meminfo");
    unsigned long long total = 0, free_ = 0, buffers = 0, cached = 0,
                       sreclaimable = 0, shmem = 0;
    std::string line;
    while (std::getline(f, line)) {
        unsigned long long v;
        char name[64];
        if (sscanf(line.c_str(), "%63s %llu", name, &v) == 2) {
            std::string n(name);
            if      (n == "MemTotal:")        total = v;
            else if (n == "MemFree:")         free_ = v;
            else if (n == "Buffers:")         buffers = v;
            else if (n == "Cached:")          cached = v;
            else if (n == "SReclaimable:")    sreclaimable = v;
            else if (n == "Shmem:")           shmem = v;
        }
    }
    if (total == 0) return 0.0f;
    unsigned long long used = total - free_ - buffers - cached - sreclaimable + shmem;
    return 100.0f * static_cast<float>(used) / static_cast<float>(total);
}

inline float diskPercent(const std::string& path = "/") {
    struct statvfs st;
    if (statvfs(path.c_str(), &st) != 0) return 0.0f;
    unsigned long long total = st.f_blocks * st.f_frsize;
    unsigned long long avail = st.f_bavail * st.f_frsize;
    if (total == 0) return 0.0f;
    return 100.0f * (1.0f - static_cast<float>(avail) / static_cast<float>(total));
}

// Collect a sample; blocks ~1 s for CPU measurement
inline Sample collect(const std::string& diskPath = "/") {
    CpuState s1 = readCpuState();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CpuState s2 = readCpuState();

    Sample s;
    s.cpu  = cpuPercent(s1, s2);
    s.ram  = ramPercent();
    s.disk = diskPercent(diskPath);
    return s;
}

} // namespace monitor::metrics
