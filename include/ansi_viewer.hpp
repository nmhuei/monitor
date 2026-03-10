#pragma once
/*
 * ansi_viewer.hpp — ANSI escape code renderer for remote nc viewers.
 * Produces a colored text dashboard that can be streamed over TCP.
 */
#include "metrics_store.hpp"
#include "thresholds.hpp"
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace monitor::viewer {

// ANSI color codes
static const char *RST = "\033[0m";
static const char *BOLD = "\033[1m";
static const char *DIM = "\033[2m";
static const char *ARED = "\033[1;31m";
static const char *AGRN = "\033[1;32m";
static const char *AYEL = "\033[1;33m";
static const char *ACYN = "\033[1;36m";
static const char *AWHT = "\033[1;37m";
static const char *AGRY = "\033[0;37m";
static const char *ABGCYN = "\033[30;46m";
static const char *CLR = "\033[2J\033[H"; // clear screen + home

static std::string fmtTime(time_t t) {
  char buf[16];
  struct tm *tm_ = localtime(&t);
  strftime(buf, sizeof(buf), "%H:%M:%S", tm_);
  return buf;
}

static const char *pctAnsi(float pct, const std::string &host,
                           const Thresholds &th, char m) {
  float alTh = m == 'c'   ? th.getCPU(host)
               : m == 'r' ? th.getRAM(host)
                          : th.getDisk(host);
  if (pct >= alTh)
    return ARED;
  if (pct >= alTh * 0.80f)
    return AYEL;
  return AGRN;
}

static std::string makeBar(float pct, int width) {
  int filled = (int)(pct / 100.0f * width);
  if (filled < 0)
    filled = 0;
  if (filled > width)
    filled = width;
  std::string bar;
  for (int i = 0; i < filled; i++)
    bar += "█";
  for (int i = filled; i < width; i++)
    bar += "░";
  return bar;
}

static std::string padRight(const std::string &s, int n) {
  if ((int)s.size() >= n)
    return s.substr(0, n);
  return s + std::string(n - (int)s.size(), ' ');
}

static std::string statusSymbol(HostStatus s) {
  switch (s) {
  case HostStatus::ALERT:
    return std::string(ARED) + "● ALERT" + RST;
  case HostStatus::WARNING:
    return std::string(AYEL) + "◐ WARN" + RST;
  case HostStatus::ONLINE:
    return std::string(AGRN) + "● OK" + RST;
  case HostStatus::OFFLINE:
    return std::string(AGRY) + "○ OFF" + RST;
  }
  return "?";
}

// Render a complete ANSI dashboard frame (hacker style, close to ncurses UI)
inline std::string renderFrame(const std::vector<HostState> &hosts,
                               const Thresholds &th) {
  std::ostringstream o;
  o << CLR;

  const int W = 118;
  auto line = [&](char c) { return std::string(W, c); };

  int online = 0, offline = 0, warn = 0, alert = 0;
  float sumCpu = 0, sumRam = 0, sumDisk = 0;
  std::string hotCpu = "-", hotRam = "-", hotDisk = "-";
  float maxCpu = -1, maxRam = -1, maxDisk = -1;

  for (const auto &h : hosts) {
    if (h.status == HostStatus::OFFLINE) {
      offline++;
      continue;
    }
    online++;
    sumCpu += h.cpu;
    sumRam += h.ram;
    sumDisk += h.disk;
    if (h.status == HostStatus::WARNING)
      warn++;
    if (h.status == HostStatus::ALERT)
      alert++;
    if (h.cpu > maxCpu) {
      maxCpu = h.cpu;
      hotCpu = h.name;
    }
    if (h.ram > maxRam) {
      maxRam = h.ram;
      hotRam = h.name;
    }
    if (h.disk > maxDisk) {
      maxDisk = h.disk;
      hotDisk = h.name;
    }
  }

  float avgCpu = online ? sumCpu / online : 0;
  float avgRam = online ? sumRam / online : 0;
  float avgDisk = online ? sumDisk / online : 0;
  int threat = std::min(100, alert * 30 + warn * 10 + (int)(avgCpu * 0.2f));

  // Header
  std::string ts = fmtTime(time(nullptr));
  std::string title = "◈ DISTRIBUTED SYSTEM MONITOR ◈";
  o << ABGCYN << BOLD << " " << ts << "  " << title
    << "  [nc viewer - read only]" << RST << "\n";
  o << ACYN << line('=') << RST << "\n";

  // Tactical panels (single-line compact)
  const char *riskCol = threat >= 70 ? ARED : threat >= 35 ? AYEL : AGRN;
  o << AWHT << " RISK:" << riskCol << padRight(std::to_string(threat) + "%", 5)
    << RST << AWHT << " | ALERT:" << ARED << alert << RST << AWHT
    << " WARN:" << AYEL << warn << RST << AWHT << " ONLINE:" << AGRN << online
    << RST << AWHT << " OFF:" << AGRY << offline << RST << AWHT << " || HOT -> "
    << ARED << "CPU " << padRight(hotCpu, 12) << " " << std::max(0.f, maxCpu)
    << "% " << AYEL << "RAM " << padRight(hotRam, 12) << " "
    << std::max(0.f, maxRam) << "% " << ACYN << "DSK " << padRight(hotDisk, 12)
    << " " << std::max(0.f, maxDisk) << "%" << RST << "\n";

  o << AWHT << " AVG:" << pctAnsi(avgCpu, "", th, 'c') << "CPU " << avgCpu
    << "% " << pctAnsi(avgRam, "", th, 'r') << "RAM " << avgRam << "% "
    << pctAnsi(avgDisk, "", th, 'd') << "DISK " << avgDisk << "%" << RST
    << "\n";

  o << ACYN << line('-') << RST << "\n";

  // Host table
  o << AWHT << BOLD << " " << padRight("HOST", 14) << " " << padRight("CPU", 22)
    << " " << padRight("RAM", 22) << " " << padRight("DISK", 22) << " STATUS"
    << RST << "\n";
  o << ACYN << line('-') << RST << "\n";

  const int barW = 12;
  for (const auto &h : hosts) {
    std::string name = padRight(h.name, 14);
    if (h.status == HostStatus::OFFLINE) {
      o << AGRY << DIM << " " << name << "  --- OFFLINE ---" << RST << "\n";
      continue;
    }

    char b1[10], b2[10], b3[10];
    snprintf(b1, sizeof(b1), "%5.1f%%", h.cpu);
    snprintf(b2, sizeof(b2), "%5.1f%%", h.ram);
    snprintf(b3, sizeof(b3), "%5.1f%%", h.disk);

    o << " " << AWHT << name << RST << " ";
    o << pctAnsi(h.cpu, h.name, th, 'c') << makeBar(h.cpu, barW) << " " << b1
      << RST << " ";
    o << pctAnsi(h.ram, h.name, th, 'r') << makeBar(h.ram, barW) << " " << b2
      << RST << " ";
    o << pctAnsi(h.disk, h.name, th, 'd') << makeBar(h.disk, barW) << " " << b3
      << RST << " ";
    o << statusSymbol(h.status);
    if (h.coreCount > 0)
      o << AGRY << " [" << h.coreCount << "c]" << RST;
    o << "\n";
  }

  o << ACYN << line('-') << RST << "\n";

  // Compact per-core line(s)
  for (const auto &h : hosts) {
    if (h.status == HostStatus::OFFLINE || h.cores.empty())
      continue;
    o << ACYN << " " << padRight(h.name, 12) << RST << AGRY << " cores: " << RST;
    int limit = std::min((int)h.cores.size(), 12);
    for (int i = 0; i < limit; i++) {
      float v = h.cores[i];
      o << pctAnsi(v, h.name, th, 'c');
      char cb[16];
      snprintf(cb, sizeof(cb), "%d:%2.0f%%", i, v);
      o << cb << RST << " ";
    }
    if ((int)h.cores.size() > limit)
      o << AGRY << "..." << RST;
    o << "\n";
  }

  o << ACYN << line('=') << RST << "\n";
  o << AGRY
    << " Auto-refresh every 2s | Ctrl+C disconnect | Viewer is read-only snapshot"
    << RST << "\n";

  return o.str();
}

} // namespace monitor::viewer
