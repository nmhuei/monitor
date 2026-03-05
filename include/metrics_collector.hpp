#pragma once
/*
 * Metrics collector — reads from /proc/stat, /proc/meminfo, statvfs()
 * Supports per-core CPU collection.
 */
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/statvfs.h>
#include <thread>
#include <vector>

namespace monitor::metrics {

struct Sample {
  float cpu = 0.0f;
  float ram = 0.0f;
  float disk = 0.0f;
  std::vector<float> cores; // per-core CPU %
};

// Internal CPU state for delta calculation
struct CpuState {
  unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0,
                     softirq = 0, steal = 0;
};

inline CpuState parseCpuLine(const std::string &line) {
  CpuState s;
  // Skip the "cpu" or "cpuN" label
  const char *p = line.c_str();
  while (*p && *p != ' ')
    p++;
  sscanf(p, " %llu %llu %llu %llu %llu %llu %llu %llu", &s.user, &s.nice,
         &s.sys, &s.idle, &s.iowait, &s.irq, &s.softirq, &s.steal);
  return s;
}

// Read all CPU states: index 0 = total, 1..N = per-core
inline std::vector<CpuState> readAllCpuStates() {
  std::vector<CpuState> states;
  std::ifstream f("/proc/stat");
  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, 3, "cpu") == 0) {
      states.push_back(parseCpuLine(line));
    } else {
      break; // cpu lines are contiguous at the top
    }
  }
  return states;
}

inline CpuState readCpuState() {
  auto all = readAllCpuStates();
  return all.empty() ? CpuState{} : all[0];
}

inline float cpuPercent(const CpuState &prev, const CpuState &cur) {
  auto prevIdle = prev.idle + prev.iowait;
  auto curIdle = cur.idle + cur.iowait;
  auto prevTotal = prev.user + prev.nice + prev.sys + prevIdle + prev.irq +
                   prev.softirq + prev.steal;
  auto curTotal = cur.user + cur.nice + cur.sys + curIdle + cur.irq +
                  cur.softirq + cur.steal;
  auto dTotal = curTotal - prevTotal;
  auto dIdle = curIdle - prevIdle;
  if (dTotal == 0)
    return 0.0f;
  return 100.0f *
         (1.0f - static_cast<float>(dIdle) / static_cast<float>(dTotal));
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
      if (n == "MemTotal:")
        total = v;
      else if (n == "MemFree:")
        free_ = v;
      else if (n == "Buffers:")
        buffers = v;
      else if (n == "Cached:")
        cached = v;
      else if (n == "SReclaimable:")
        sreclaimable = v;
      else if (n == "Shmem:")
        shmem = v;
    }
  }
  if (total == 0)
    return 0.0f;
  unsigned long long used =
      total - free_ - buffers - cached - sreclaimable + shmem;
  return 100.0f * static_cast<float>(used) / static_cast<float>(total);
}

inline float diskPercent(const std::string &path = "/") {
  struct statvfs st;
  if (statvfs(path.c_str(), &st) != 0)
    return 0.0f;
  unsigned long long total = st.f_blocks * st.f_frsize;
  unsigned long long avail = st.f_bavail * st.f_frsize;
  if (total == 0)
    return 0.0f;
  return 100.0f *
         (1.0f - static_cast<float>(avail) / static_cast<float>(total));
}

// Collect a sample with per-core CPU; blocks ~500ms for CPU measurement
inline Sample collect(const std::string &diskPath = "/") {
  auto states1 = readAllCpuStates();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto states2 = readAllCpuStates();

  Sample s;
  // Total CPU (index 0)
  if (!states1.empty() && !states2.empty())
    s.cpu = cpuPercent(states1[0], states2[0]);

  // Per-core (index 1..N)
  size_t coreCount = std::min(states1.size(), states2.size());
  for (size_t i = 1; i < coreCount; i++)
    s.cores.push_back(cpuPercent(states1[i], states2[i]));

  s.ram = ramPercent();
  s.disk = diskPercent(diskPath);
  return s;
}

} // namespace monitor::metrics
