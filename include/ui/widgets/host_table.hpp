#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>
#include <algorithm>
#include "../ui_types.hpp"
#include "../theme.hpp"
#include "../format.hpp"
#include "panel.hpp"

namespace monitor::ui {

// Helper to parse tags/labels from hostnames
inline void parseHostLabels(const std::string& hostname, std::string& role, std::string& env, std::string& cluster) {
  role = "app";
  env = "prod";
  cluster = "default";
  
  std::string temp = hostname;
  std::transform(temp.begin(), temp.end(), temp.begin(), ::tolower);
  
  std::vector<std::string> tokens;
  std::string token;
  for (char c : temp) {
    if (c == '-' || c == '_' || c == '.') {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
    } else {
      token += c;
    }
  }
  if (!token.empty()) tokens.push_back(token);
  
  for (const auto& t : tokens) {
    if (t == "web" || t == "db" || t == "api" || t == "app" || t == "worker" || t == "cache" || t == "proxy" || t == "lb" || t == "dns") {
      role = t;
    }
    else if (t == "prod" || t == "dev" || t == "staging" || t == "test" || t == "uat" || t == "local") {
      env = t;
    }
    else if (t == "hanoi" || t == "sg" || t == "us" || t == "eu" || t == "asia" || t == "tokyo" || t == "saigon") {
      cluster = t;
    }
  }
}

inline bool matchesFilter(const HostState& h, const HostFilter& f) {
  if (!f.search.empty()) {
    if (h.name.find(f.search) == std::string::npos && h.ip.find(f.search) == std::string::npos) {
      return false;
    }
  }
  if (!f.status.empty()) {
    std::string s = statusStr(h.status);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s != f.status) return false;
  }
  
  std::string role, env, cluster;
  parseHostLabels(h.name, role, env, cluster);
  
  if (!f.role.empty() && role != f.role) return false;
  if (!f.env.empty() && env != f.env) return false;
  if (!f.cluster.empty() && cluster != f.cluster) return false;
  
  return true;
}

struct HostComparator {
  SortKey key;
  bool desc;
  
  int severityScore(HostStatus s) const {
    switch (s) {
      case HostStatus::ALERT: return 0;
      case HostStatus::WARNING: return 1;
      case HostStatus::STALE: return 2;
      case HostStatus::OFFLINE: return 3;
      case HostStatus::ONLINE: return 4;
    }
    return 99;
  }
  
  bool operator()(const HostState& a, const HostState& b) const {
    if (key == SortKey::Status) {
      int sA = severityScore(a.status);
      int sB = severityScore(b.status);
      if (sA != sB) {
        return desc ? (sA < sB) : (sA > sB);
      }
      return a.name < b.name;
    }
    
    double valA = 0, valB = 0;
    if (key == SortKey::Host) {
      return desc ? (a.name > b.name) : (a.name < b.name);
    } else if (key == SortKey::Cpu) {
      valA = a.cpu; valB = b.cpu;
    } else if (key == SortKey::Ram) {
      valA = a.ram; valB = b.ram;
    } else if (key == SortKey::Disk) {
      valA = a.disk; valB = b.disk;
    } else if (key == SortKey::Load) {
      valA = a.loadAvg; valB = b.loadAvg;
    } else if (key == SortKey::Age) {
      valA = a.lastSeen; valB = b.lastSeen;
    }
    
    if (valA != valB) {
      return desc ? (valA > valB) : (valA < valB);
    }
    return a.name < b.name;
  }
};

class HostTable {
public:
  static void draw(const Rect& r,
                   const std::vector<HostState>& hosts,
                   DashboardState& state,
                   const Thresholds& thresholds,
                   std::vector<HostState>& outFilteredSorted) {
    if (r.h <= 0 || r.w <= 0) return;

    // Filter hosts
    outFilteredSorted.clear();
    for (const auto& h : hosts) {
      if (matchesFilter(h, state.filter)) {
        outFilteredSorted.push_back(h);
      }
    }

    // Sort hosts
    HostComparator comp{state.sortKey, state.sortDesc};
    std::stable_sort(outFilteredSorted.begin(), outFilteredSorted.end(), comp);

    // Scroll calculations
    int maxRows = r.h - 3; // border, headers, border
    if (maxRows <= 0) maxRows = 1;
    
    int totalCount = (int)outFilteredSorted.size();
    if (state.selectedHost < 0) state.selectedHost = 0;
    if (state.selectedHost >= totalCount) {
      state.selectedHost = std::max(0, totalCount - 1);
    }
    if (state.selectedHost < state.hostScroll) {
      state.hostScroll = state.selectedHost;
    }
    if (state.selectedHost >= state.hostScroll + maxRows) {
      state.hostScroll = state.selectedHost - maxRows + 1;
    }

    std::string title = "HOSTS (" + std::to_string(totalCount) + "/" + std::to_string(hosts.size()) + ")";
    if (state.sortDesc) {
      title += " [Sort: " + sortKeyName(state.sortKey) + " Desc]";
    } else {
      title += " [Sort: " + sortKeyName(state.sortKey) + " Asc]";
    }
    drawPanelBox(r.y, r.x, r.w, r.h, title, state.focus == FocusPane::HostTable);

    // If no hosts, show a warning message inside
    if (outFilteredSorted.empty()) {
      attron(COLOR_PAIR(CP_DIM));
      mvaddstr(r.y + r.h / 2, r.x + (r.w - 18) / 2, "No hosts matched.");
      attroff(COLOR_PAIR(CP_DIM));
      return;
    }

    // Print headers depending on width
    int headerY = r.y + 1;
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    
    if (r.w >= 140) {
      // Wide layout columns
      mvprintw(headerY, r.x + 2, "%-3s %-18s %-8s %-6s %-12s %-12s %-12s %-5s %-10s %-5s",
               "ST", "HOST", "ROLE", "ENV", "CPU", "RAM", "DISK", "LOAD", "NET RX/TX", "AGE");
    } else if (r.w >= 100) {
      // Standard layout columns
      mvprintw(headerY, r.x + 2, "%-3s %-16s %-12s %-12s %-12s %-5s %-10s %-5s",
               "ST", "HOST", "CPU", "RAM", "DISK", "LOAD", "NET RX/TX", "AGE");
    } else {
      // Compact layout columns
      mvprintw(headerY, r.x + 2, "%-3s %-14s %-8s %-8s %-5s",
               "ST", "HOST", "CPU", "RAM", "AGE");
    }
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

    // Draw rows
    for (int i = 0; i < maxRows; ++i) {
      int idx = state.hostScroll + i;
      if (idx >= totalCount) break;
      const auto& h = outFilteredSorted[idx];
      int rowY = r.y + 2 + i;

      bool isSelected = (idx == state.selectedHost);
      int normalCp = isSelected ? CP_HIGHLIGHT : CP_NORMAL;
      int dimCp = isSelected ? CP_HIGHLIGHT : CP_DIM;

      if (isSelected) {
        attron(COLOR_PAIR(CP_HIGHLIGHT));
        mvhline(rowY, r.x + 1, ' ', r.w - 2);
        attroff(COLOR_PAIR(CP_HIGHLIGHT));
      }

      // Draw status glyph
      int statCp = statusColorPair(h.status);
      attron(COLOR_PAIR(isSelected ? CP_HIGHLIGHT : statCp) | (isSelected ? A_NORMAL : A_BOLD));
      mvaddstr(rowY, r.x + 2, statusGlyph(h.status));
      attroff(COLOR_PAIR(isSelected ? CP_HIGHLIGHT : statCp) | (isSelected ? A_NORMAL : A_BOLD));

      // Parse metadata
      std::string role, env, cluster;
      parseHostLabels(h.name, role, env, cluster);

      // Draw hostname
      attron(COLOR_PAIR(normalCp));
      std::string dispName = h.name;
      if (dispName.size() > 18) dispName = dispName.substr(0, 16) + "..";
      mvprintw(rowY, r.x + 6, "%s", padRight(dispName, 18).c_str());
      attroff(COLOR_PAIR(normalCp));

      // Draw based on width
      if (r.w >= 140) {
        // Wide columns
        attron(COLOR_PAIR(dimCp));
        mvprintw(rowY, r.x + 25, "%-8s %-6s", role.substr(0, 8).c_str(), env.substr(0, 6).c_str());
        attroff(COLOR_PAIR(dimCp));

        float cpuCrit = thresholds.getCPU(h.name);
        float cpuWarn = cpuCrit * 0.8f;
        float ramCrit = thresholds.getRAM(h.name);
        float ramWarn = ramCrit * 0.8f;
        float diskCrit = thresholds.getDisk(h.name);
        float diskWarn = diskCrit * 0.8f;

        // CPU progress bar
        drawStatBar(rowY, r.x + 41, h.cpu, cpuWarn, cpuCrit, isSelected);

        // RAM progress bar
        drawStatBar(rowY, r.x + 54, h.ram, ramWarn, ramCrit, isSelected);

        // DISK progress bar
        drawStatBar(rowY, r.x + 67, h.disk, diskWarn, diskCrit, isSelected);

        // Load Avg
        attron(COLOR_PAIR(normalCp));
        mvprintw(rowY, r.x + 80, "%-5.1f", h.loadAvg);
        attroff(COLOR_PAIR(normalCp));

        // RX/TX Net rate
        attron(COLOR_PAIR(dimCp));
        std::string netStr = formatNet(h.netRxKB) + "/" + formatNet(h.netTxKB);
        mvprintw(rowY, r.x + 86, "%-10s", netStr.substr(0, 10).c_str());

        // Age
        mvprintw(rowY, r.x + 97, "%-5s", formatAge(h.lastSeen, h.status).c_str());
        attroff(COLOR_PAIR(dimCp));
      } 
      else if (r.w >= 100) {
        float cpuCrit = thresholds.getCPU(h.name);
        float cpuWarn = cpuCrit * 0.8f;
        float ramCrit = thresholds.getRAM(h.name);
        float ramWarn = ramCrit * 0.8f;
        float diskCrit = thresholds.getDisk(h.name);
        float diskWarn = diskCrit * 0.8f;

        // Standard columns
        // CPU
        drawStatBar(rowY, r.x + 23, h.cpu, cpuWarn, cpuCrit, isSelected);
        // RAM
        drawStatBar(rowY, r.x + 36, h.ram, ramWarn, ramCrit, isSelected);
        // DISK
        drawStatBar(rowY, r.x + 49, h.disk, diskWarn, diskCrit, isSelected);

        // Load Avg
        attron(COLOR_PAIR(normalCp));
        mvprintw(rowY, r.x + 62, "%-5.1f", h.loadAvg);
        attroff(COLOR_PAIR(normalCp));

        // Net RX/TX
        attron(COLOR_PAIR(dimCp));
        std::string netStr = formatNet(h.netRxKB) + "/" + formatNet(h.netTxKB);
        mvprintw(rowY, r.x + 68, "%-10s", netStr.substr(0, 10).c_str());

        // Age
        mvprintw(rowY, r.x + 79, "%-5s", formatAge(h.lastSeen, h.status).c_str());
        attroff(COLOR_PAIR(dimCp));
      } 
      else {
        // Compact columns
        attron(COLOR_PAIR(normalCp));
        // CPU percentage
        mvprintw(rowY, r.x + 21, "%3.0f%%", h.cpu);
        // RAM percentage
        mvprintw(rowY, r.x + 30, "%3.0f%%", h.ram);
        attroff(COLOR_PAIR(normalCp));

        attron(COLOR_PAIR(dimCp));
        // Age
        mvprintw(rowY, r.x + 39, "%-5s", formatAge(h.lastSeen, h.status).c_str());
        attroff(COLOR_PAIR(dimCp));
      }
    }
  }

private:
  static std::string sortKeyName(SortKey key) {
    switch (key) {
      case SortKey::Status: return "Status";
      case SortKey::Host:   return "Host";
      case SortKey::Cpu:    return "CPU";
      case SortKey::Ram:    return "RAM";
      case SortKey::Disk:   return "Disk";
      case SortKey::Load:   return "Load";
      case SortKey::Age:    return "Age";
    }
    return "";
  }

  static void drawStatBar(int y, int x, float pct, float warn, float crit, bool isSelected) {
    int color = CP_OK;
    if (pct >= crit) color = CP_ALERT;
    else if (pct >= warn) color = CP_WARN;

    if (isSelected) {
      attron(COLOR_PAIR(CP_HIGHLIGHT));
      mvprintw(y, x, "%3.0f%% %s", pct, getBlockGlyph(pct));
      attroff(COLOR_PAIR(CP_HIGHLIGHT));
    } else {
      attron(COLOR_PAIR(CP_NORMAL));
      mvprintw(y, x, "%3.0f%% ", pct);
      attroff(COLOR_PAIR(CP_NORMAL));
      
      attron(COLOR_PAIR(color));
      addstr(getBlockGlyph(pct));
      attroff(COLOR_PAIR(color));
    }
  }
};

} // namespace monitor::ui
