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

namespace monitor::ui {

class EventList {
public:
  static void draw(const Rect& r,
                   const std::vector<LogEvent>& log,
                   DashboardState& state,
                   bool fullScreen) {
    if (r.h <= 0 || r.w <= 0) return;

    std::string title = fullScreen ? "EVENTS & INCIDENTS LOG" : "RECENT EVENTS PREVIEW";
    title += " (" + std::to_string(log.size()) + ")";
    if (state.eventScroll > 0) {
      title += " [Scrollback: " + std::to_string(state.eventScroll) + "]";
    }
    
    drawPanelBox(r.y, r.x, r.w, r.h, title, state.focus == FocusPane::Events);

    int logSize = (int)log.size();
    if (logSize == 0) {
      attron(COLOR_PAIR(CP_DIM));
      mvaddstr(r.y + r.h / 2, r.x + (r.w - 18) / 2, "No event logs yet.");
      attroff(COLOR_PAIR(CP_DIM));
      return;
    }

    // Header row
    int headerY = r.y + 1;
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvprintw(headerY, r.x + 2, "%-10s %-16s %-12s %-s", "TIME", "HOST", "TYPE", "MESSAGE");
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

    int maxRows = r.h - 3; // border, header, border
    if (maxRows <= 0) maxRows = 1;

    if (state.eventScroll >= logSize) {
      state.eventScroll = std::max(0, logSize - 1);
    }

    // Scrollback rendering (latest at bottom or top)
    // To make scrollback navigation natural, let's render from the back
    int startIdx = logSize - 1 - state.eventScroll;
    int endIdx = std::max(0, startIdx - maxRows + 1);

    for (int i = 0; i < maxRows; ++i) {
      int idx = startIdx - i;
      if (idx < endIdx || idx < 0 || idx >= logSize) break;
      const auto& ev = log[idx];
      int rowY = r.y + 2 + i;

      // Time formatting
      struct tm *tm_info = localtime(&ev.ts);
      char tbuf[16];
      strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);

      // Color coding by type
      int typeCp = CP_NORMAL;
      std::string typeStr;
      switch (ev.type) {
        case LogEventType::CONNECT:
          typeCp = CP_OK; typeStr = "CONNECT"; break;
        case LogEventType::DISCONNECT:
          typeCp = CP_WARN; typeStr = "DISCONNECT"; break;
        case LogEventType::ALERT:
          typeCp = CP_ALERT; typeStr = "ALERT"; break;
        case LogEventType::STALE:
          typeCp = CP_WARN; typeStr = "STALE"; break;
        case LogEventType::METRIC:
          typeCp = CP_DIM; typeStr = "METRIC"; break;
      }

      // Time
      attron(COLOR_PAIR(CP_DIM));
      mvprintw(rowY, r.x + 2, "%-10s", tbuf);
      attroff(COLOR_PAIR(CP_DIM));

      // Hostname
      attron(COLOR_PAIR(CP_NORMAL));
      std::string hostDisp = ev.host;
      if (hostDisp.size() > 16) hostDisp = hostDisp.substr(0, 14) + "..";
      mvprintw(rowY, r.x + 13, "%-16s", hostDisp.c_str());
      attroff(COLOR_PAIR(CP_NORMAL));

      // Event Type
      attron(COLOR_PAIR(typeCp) | A_BOLD);
      mvprintw(rowY, r.x + 30, "%-12s", typeStr.c_str());
      attroff(COLOR_PAIR(typeCp) | A_BOLD);

      // Event details
      attron(COLOR_PAIR(CP_NORMAL));
      std::string detailDisp = ev.detail;
      int availW = r.w - 44;
      if (availW > 0) {
        if ((int)detailDisp.size() > availW) {
          detailDisp = detailDisp.substr(0, availW - 3) + "...";
        }
        mvprintw(rowY, r.x + 43, "%s", detailDisp.c_str());
      }
      attroff(COLOR_PAIR(CP_NORMAL));
    }
  }
};

} // namespace monitor::ui
