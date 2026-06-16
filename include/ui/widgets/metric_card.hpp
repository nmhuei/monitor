#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>
#include <ctime>
#include <algorithm>
#include "../ui_types.hpp"
#include "../theme.hpp"
#include "../format.hpp"
#include "panel.hpp"
#include "sparkline.hpp"
#include "event_list.hpp"

namespace monitor::ui {

class MetricCard {
public:
  static void draw(const Rect& r,
                   const HostState& host,
                   DashboardState& state,
                   const Thresholds& thresholds,
                   const std::vector<LogEvent>& log,
                   time_t firstSeenTime) {
    if (r.h <= 0 || r.w <= 0) return;

    // Outer box
    std::string title = "HOST DRILL-DOWN: " + host.name;
    drawPanelBox(r.y, r.x, r.w, r.h, title, state.focus == FocusPane::Detail);

    // 1. Host Header Information
    attron(COLOR_PAIR(CP_NORMAL) | A_BOLD);
    mvprintw(r.y + 1, r.x + 2, "%s  (%s)", host.name.c_str(), host.ip.c_str());
    attroff(A_BOLD);

    // Dynamic label tags
    std::string role, env, cluster;
    parseHostLabels(host.name, role, env, cluster);
    std::string tagsStr = " [role=" + role + " env=" + env + " cluster=" + cluster + "]";
    attron(COLOR_PAIR(CP_DIM));
    mvaddstr(r.y + 1, r.x + 4 + host.name.size() + host.ip.size(), tagsStr.c_str());
    attroff(COLOR_PAIR(CP_DIM));

    // Status Badge
    int sCol = statusColorPair(host.status);
    attron(COLOR_PAIR(sCol) | A_BOLD);
    std::string sText = " " + std::string(statusStr(host.status)) + " ";
    mvaddstr(r.y + 1, r.w - 25 - (int)sText.size(), sText.c_str());
    attroff(COLOR_PAIR(sCol) | A_BOLD);

    // Uptime / Tracking Duration
    std::string trackingStr = "Active: " + formatUptime(time(nullptr) - firstSeenTime);
    attron(COLOR_PAIR(CP_DIM));
    mvaddstr(r.y + 1, r.w - 22, trackingStr.c_str());
    attroff(COLOR_PAIR(CP_DIM));

    // Separator line
    attron(COLOR_PAIR(CP_ACCENT));
    mvhline(r.y + 2, r.x + 1, ACS_HLINE, r.w - 2);
    attroff(COLOR_PAIR(CP_ACCENT));

    // 2. Tab Bar Navigation
    // We support 4 tabs: Metrics, Cores, History, Events
    std::vector<std::string> tabs = {"[1] METRICS", "[2] CORES", "[3] HISTORY", "[4] EVENTS"};
    int tabX = r.x + 2;
    int tabY = r.y + 3;
    for (int i = 0; i < 4; ++i) {
      bool active = (state.activeTab == i);
      if (active) {
        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        mvprintw(tabY, tabX, " %s ", tabs[i].c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
      } else {
        attron(COLOR_PAIR(CP_DIM));
        mvprintw(tabY, tabX, " %s ", tabs[i].c_str());
        attroff(COLOR_PAIR(CP_DIM));
      }
      tabX += (int)tabs[i].size() + 3;
    }

    // Separator line
    attron(COLOR_PAIR(CP_ACCENT));
    mvhline(r.y + 4, r.x + 1, ACS_HLINE, r.w - 2);
    attroff(COLOR_PAIR(CP_ACCENT));

    // 3. Tab Contents
    int contentY = r.y + 5;
    int contentH = r.h - 6; // subtracting top parts and borders

    if (state.activeTab == 0) {
      // Tab 0: Metrics (overall summary & sparklines)
      // Overall Metrics Stats line
      char summaryBuf[512];
      snprintf(summaryBuf, sizeof(summaryBuf), 
               "CPU:%5.1f%% | RAM:%5.1f%% | DISK:%5.1f%% | LOAD:%4.2f | PROCS:%4d | RX:%6s/s | TX:%6s/s",
               host.cpu, host.ram, host.disk, host.loadAvg, host.procCount, 
               formatNet(host.netRxKB).c_str(), formatNet(host.netTxKB).c_str());
      attron(COLOR_PAIR(CP_NORMAL));
      mvaddstr(contentY, r.x + 2, summaryBuf);
      attroff(COLOR_PAIR(CP_NORMAL));

      attron(COLOR_PAIR(CP_ACCENT));
      mvhline(contentY + 1, r.x + 1, ACS_HLINE, r.w - 2);
      attroff(COLOR_PAIR(CP_ACCENT));

      // Draw Bento Grid with Sparklines
      int gridY = contentY + 2;
      int gridH = (contentH - 3) / 2;
      if (gridH < 4) gridH = 4;
      int gridW = (r.w - 4) / 2;

      std::vector<float> cpu_h, ram_h, dsk_h, net_h;
      for (const auto &s : host.history) {
        cpu_h.push_back(s.cpu);
        ram_h.push_back(s.ram);
        dsk_h.push_back(s.disk);
        net_h.push_back(s.netRxKB + s.netTxKB);
      }

      auto drawGraphBox = [&](int gy, int gx, const std::string &gTitle, const std::vector<float> &vals, float tVal) {
        drawPanelBox(gy, gx, gridW, gridH, gTitle, false);
        int graphW = gridW - 9;
        int graphH = gridH - 2;
        if (graphW > 2 && graphH > 1) {
          drawSparkline(gy + 1, gx + 1, graphW, graphH, vals, tVal);
          
          float minV = 100.f, maxV = 0.f, sumV = 0.f;
          for (float v : vals) {
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            sumV += v;
          }
          float avgV = vals.empty() ? 0.f : sumV / vals.size();
          if (vals.empty()) minV = 0.f;

          attron(COLOR_PAIR(CP_DIM));
          mvprintw(gy + 1, gx + 1 + graphW + 1, "▲ %3.0f", maxV);
          mvprintw(gy + 1 + graphH/2, gx + 1 + graphW + 1, "■ %3.0f", avgV);
          mvprintw(gy + 1 + graphH - 1, gx + 1 + graphW + 1, "▼ %3.0f", minV);
          attroff(COLOR_PAIR(CP_DIM));
        }
      };

      drawGraphBox(gridY, r.x + 2, "CPU HIST (60s)", cpu_h, thresholds.getCPU(host.name) * 0.8f);
      drawGraphBox(gridY, r.x + 2 + gridW, "RAM HIST (60s)", ram_h, thresholds.getRAM(host.name) * 0.8f);
      drawGraphBox(gridY + gridH, r.x + 2, "DISK HIST (60s)", dsk_h, thresholds.getDisk(host.name) * 0.8f);
      
      // Scaling Net
      float maxNet = 10.f;
      for (float v : net_h) if (v > maxNet) maxNet = v;
      std::vector<float> net_scaled;
      for (float v : net_h) net_scaled.push_back((v / maxNet) * 100.f);
      drawGraphBox(gridY + gridH, r.x + 2 + gridW, "NET I/O HIST", net_scaled, 80.f);
    }
    else if (state.activeTab == 1) {
      // Tab 1: Cores (Per-core Grid)
      int colsCount = 4;
      if (r.w < 60) colsCount = 2;
      int colW = (r.w - 4) / colsCount;
      int maxCoresVisible = std::max(1, contentH - 1) * colsCount;
      int coresToDraw = std::min(host.coreCount, maxCoresVisible);

      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvprintw(contentY, r.x + 2, "CPU CORES UTILIZATION GRID (%d cores total)", host.coreCount);
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

      for (int i = 0; i < coresToDraw; ++i) {
        int coreRow = i / colsCount;
        int coreCol = i % colsCount;
        int cy = contentY + 2 + coreRow;
        int cx = r.x + 2 + coreCol * colW;

        if (cy >= r.y + r.h - 1) break;

        float val = host.cores[i];
        std::string bar = drawBar(val, 6);
        char buf[64];
        snprintf(buf, sizeof(buf), "C%-2d [%s] %2.0f%%", i, bar.c_str(), val);

        int cp = CP_NORMAL;
        float hCpuCrit = thresholds.getCPU(host.name);
        if (val >= hCpuCrit) cp = CP_ALERT;
        else if (val >= hCpuCrit * 0.8f) cp = CP_WARN;

        attron(COLOR_PAIR(cp));
        mvaddnstr(cy, cx, buf, colW - 1);
        attroff(COLOR_PAIR(cp));
      }
    }
    else if (state.activeTab == 2) {
      // Tab 2: History (Detailed logs table)
      int maxVisible = contentH - 3;
      int histSize = (int)host.history.size();

      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvprintw(contentY, r.x + 2, "%-10s %7s %7s %7s %8s %8s %8s", "TIME", "CPU%", "RAM%", "DISK%", "LOAD", "RX KB/s", "TX KB/s");
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

      attron(COLOR_PAIR(CP_ACCENT));
      mvhline(contentY + 1, r.x + 1, ACS_HLINE, r.w - 2);
      attroff(COLOR_PAIR(CP_ACCENT));

      if (histSize == 0) {
        attron(COLOR_PAIR(CP_DIM));
        mvaddstr(contentY + 3, r.x + 2, "No metric history recorded.");
        attroff(COLOR_PAIR(CP_DIM));
        return;
      }

      if (state.helpScroll >= histSize) {
        state.helpScroll = std::max(0, histSize - 1);
      }
      int startIdx = histSize - 1 - state.helpScroll;
      int endIdx = std::max(0, startIdx - maxVisible + 1);

      int rowOffset = 0;
      for (int i = startIdx; i >= endIdx && i < histSize; --i) {
        int rowY = contentY + 2 + rowOffset;
        if (rowY >= r.y + r.h - 1) break;
        const auto& s = host.history[i];

        struct tm *tm_info = localtime(&s.ts);
        char tbuf[16];
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);

        attron(COLOR_PAIR(CP_NORMAL));
        mvprintw(rowY, r.x + 2, "%-10s %6.1f%% %6.1f%% %6.1f%% %8.2f %8.1f %8.1f",
                 tbuf, s.cpu, s.ram, s.disk, s.loadAvg, s.netRxKB, s.netTxKB);
        attroff(COLOR_PAIR(CP_NORMAL));
        rowOffset++;
      }
    }
    else if (state.activeTab == 3) {
      // Tab 3: Events (Filtered to this host)
      std::vector<LogEvent> hostLogs;
      for (const auto& ev : log) {
        if (ev.host == host.name) {
          hostLogs.push_back(ev);
        }
      }
      
      Rect subRect = {contentY, r.x + 1, contentH, r.w - 2};
      EventList::draw(subRect, hostLogs, state, false);
    }
  }
};

} // namespace monitor::ui
