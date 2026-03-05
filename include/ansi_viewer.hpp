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

// Render a complete ANSI dashboard frame
inline std::string renderFrame(const std::vector<HostState> &hosts,
                               const Thresholds &th) {
  std::ostringstream o;
  o << CLR; // clear screen

  int W = 80;
  std::string hline(W, '=');
  std::string sline(W, '-');

  // Header
  o << ABGCYN << BOLD;
  std::string ts = fmtTime(time(nullptr));
  o << " " << ts
    << "   ◈ DISTRIBUTED SYSTEM MONITOR ◈   [nc viewer - read only]";
  int pad = W - (int)ts.size() - 55;
  for (int i = 0; i < pad; i++)
    o << ' ';
  o << RST << "\n";
  o << ACYN << hline << RST << "\n";

  // Count stats
  int online = 0, alerts = 0;
  for (auto &h : hosts) {
    if (h.status != HostStatus::OFFLINE)
      online++;
    if (h.status == HostStatus::ALERT)
      alerts++;
  }
  o << AWHT << " Hosts: " << ACYN << (int)hosts.size() << AWHT
    << "  Online: " << AGRN << online << AWHT
    << "  Alerts: " << (alerts > 0 ? ARED : AGRN) << alerts << RST << "\n";
  o << ACYN << sline << RST << "\n";

  // Table header
  o << AWHT << BOLD;
  char hdr[128];
  snprintf(hdr, sizeof(hdr), " %-14s %-18s %-18s %-18s %s", "HOST", "CPU",
           "RAM", "DISK", "STATUS");
  o << hdr << RST << "\n";
  o << ACYN << sline << RST << "\n";

  // Host rows
  int barW = 10;
  for (auto &h : hosts) {
    bool off = (h.status == HostStatus::OFFLINE);

    // Host name
    std::string name = h.name;
    if (name.size() > 14)
      name = name.substr(0, 14);
    while (name.size() < 14)
      name += ' ';

    if (off) {
      o << AGRY << DIM << " " << name << "  --- OFFLINE ---" << RST << "\n";
      continue;
    }

    o << " " << AWHT << name << " ";

    // CPU bar + pct
    o << pctAnsi(h.cpu, h.name, th, 'c');
    o << makeBar(h.cpu, barW);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%5.1f%% ", h.cpu);
    o << pctBuf << RST;

    // RAM bar + pct
    o << pctAnsi(h.ram, h.name, th, 'r');
    o << makeBar(h.ram, barW);
    snprintf(pctBuf, sizeof(pctBuf), "%5.1f%% ", h.ram);
    o << pctBuf << RST;

    // DISK bar + pct
    o << pctAnsi(h.disk, h.name, th, 'd');
    o << makeBar(h.disk, barW);
    snprintf(pctBuf, sizeof(pctBuf), "%5.1f%% ", h.disk);
    o << pctBuf << RST;

    // Status
    o << statusSymbol(h.status);

    // Core count
    if (h.coreCount > 0) {
      o << AGRY << " [" << h.coreCount << "c]" << RST;
    }

    o << "\n";
  }

  o << ACYN << sline << RST << "\n";

  // Per-core details for online hosts
  for (auto &h : hosts) {
    if (h.status == HostStatus::OFFLINE || h.cores.empty())
      continue;
    o << ACYN << " " << h.name << RST << AGRY << " cores:" << RST << " ";
    for (int i = 0; i < (int)h.cores.size(); i++) {
      float v = h.cores[i];
      o << pctAnsi(v, h.name, th, 'c');
      char cb[24];
      snprintf(cb, sizeof(cb), "%2d:%3.0f%%", i, v);
      o << cb << RST << " ";
    }
    o << "\n";
  }

  o << ACYN << hline << RST << "\n";
  o << AGRY << " Auto-refresh every 2s | Press Ctrl+C to disconnect" << RST
    << "\n";

  return o.str();
}

} // namespace monitor::viewer
