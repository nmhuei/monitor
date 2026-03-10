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

inline std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

struct Sample {
  float cpu = 0.0f;
  float ram = 0.0f;
  float disk = 0.0f;
  std::vector<float> cores; // per-core CPU %
  float netRxKBps = 0.0f;
  float netTxKBps = 0.0f;
  float load1 = 0.0f;
  int procCount = 0;
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

inline std::pair<unsigned long long, unsigned long long> readNetBytes() {
  std::ifstream f("/proc/net/dev");
  std::string line;
  unsigned long long rx = 0, tx = 0;
  int skip = 0;
  while (std::getline(f, line)) {
    if (skip < 2) {
      skip++;
      continue;
    }
    auto c = line.find(':');
    if (c == std::string::npos)
      continue;
    std::string ifn = trim(line.substr(0, c));
    if (ifn == "lo")
      continue;
    std::string rest = line.substr(c + 1);
    std::istringstream iss(rest);
    unsigned long long vals[16] = {0};
    for (int i = 0; i < 16 && iss; i++)
      iss >> vals[i];
    rx += vals[0];
    tx += vals[8];
  }
  return {rx, tx};
}

inline float readLoad1() {
  std::ifstream f("/proc/loadavg");
  float l1 = 0.0f;
  f >> l1;
  return l1;
}

inline int readProcCount() {
  std::ifstream f("/proc/loadavg");
  std::string a, b, c, d;
  if (!(f >> a >> b >> c >> d))
    return 0;
  auto slash = d.find('/');
  if (slash == std::string::npos)
    return 0;
  return std::stoi(d.substr(slash + 1));
}

// Collect sample without blocking sleeps.
inline Sample collect(const std::string &diskPath = "/") {
  static bool initialized = false;
  static std::vector<CpuState> prevStates;
  static std::chrono::steady_clock::time_point prevTs;
  static unsigned long long prevRx = 0, prevTx = 0;

  auto now = std::chrono::steady_clock::now();
  auto states = readAllCpuStates();
  auto [rxB, txB] = readNetBytes();

  Sample s;
  if (initialized && !prevStates.empty() && !states.empty()) {
    s.cpu = cpuPercent(prevStates[0], states[0]);
    size_t coreCount = std::min(prevStates.size(), states.size());
    for (size_t i = 1; i < coreCount; i++)
      s.cores.push_back(cpuPercent(prevStates[i], states[i]));

    auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevTs).count();
    if (dtMs > 0) {
      double dt = dtMs / 1000.0;
      s.netRxKBps = (float)((rxB - prevRx) / 1024.0 / dt);
      s.netTxKBps = (float)((txB - prevTx) / 1024.0 / dt);
    }
  }

  s.ram = ramPercent();
  s.disk = diskPercent(diskPath);
  s.load1 = readLoad1();
  s.procCount = readProcCount();

  prevStates = states;
  prevTs = now;
  prevRx = rxB;
  prevTx = txB;
  initialized = true;
  return s;
}

} // namespace monitor::metrics
