#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "../ui_types.hpp"
#include "../theme.hpp"
#include "../format.hpp"

namespace monitor::ui {

class TopBar {
public:
  static void draw(const Rect& r, const std::vector<HostState>& hosts, const DashboardState& state) {
    if (r.h <= 0 || r.w <= 0) return;

    // Clear top bar row
    attron(COLOR_PAIR(CP_HEADER));
    mvhline(r.y, r.x, ' ', r.w);
    attroff(COLOR_PAIR(CP_HEADER));

    // Draw App Logo/Name
    attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvaddstr(r.y, r.x + 1, " ❖ OPS CONSOLE TUI ");
    attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

    // Calculate host status counts
    int total = (int)hosts.size();
    int online = 0;
    int warn = 0;
    int alert = 0;
    int stale = 0;
    int offline = 0;

    for (const auto& h : hosts) {
      switch (h.status) {
        case HostStatus::ONLINE:   online++; break;
        case HostStatus::WARNING:  warn++; break;
        case HostStatus::ALERT:    alert++; break;
        case HostStatus::STALE:    stale++; break;
        case HostStatus::OFFLINE:  offline++; break;
      }
    }

    // Prepare cluster/filter info
    std::string clusterInfo = "Default";
    if (!state.filter.cluster.empty()) {
      clusterInfo = state.filter.cluster;
    }
    
    std::string metaStr = " | CLUSTER: " + clusterInfo;
    if (!state.filter.role.empty()) metaStr += " ROLE: " + state.filter.role;
    if (!state.filter.env.empty()) metaStr += " ENV: " + state.filter.env;
    if (!state.filter.search.empty()) metaStr += " SEARCH: " + state.filter.search;

    attron(COLOR_PAIR(CP_HEADER));
    mvaddstr(r.y, r.x + 20, metaStr.c_str());
    attroff(COLOR_PAIR(CP_HEADER));

    // Summary cards
    std::vector<std::pair<std::string, int>> cards;
    cards.push_back({"TOT", total});
    cards.push_back({"OK", online});
    cards.push_back({"WRN", warn});
    cards.push_back({"ALT", alert});
    cards.push_back({"STL", stale});
    cards.push_back({"OFF", offline});

    int cardX = r.x + 22 + (int)metaStr.size() + 2;
    for (const auto& card : cards) {
      if (cardX + 10 >= r.w - 15) break; // Don't overflow into clock
      
      int color = CP_NORMAL;
      if (card.first == "OK" && card.second > 0) color = CP_OK;
      else if (card.first == "WRN" && card.second > 0) color = CP_WARN;
      else if (card.first == "ALT" && card.second > 0) color = CP_ALERT;
      else if (card.first == "STL" && card.second > 0) color = CP_STALE;
      else if (card.first == "OFF" && card.second > 0) color = CP_OFFLINE;

      // Draw card box
      mvaddch(r.y, cardX, '[');
      attron(COLOR_PAIR(color) | A_BOLD);
      printw("%s:%d", card.first.c_str(), card.second);
      attroff(COLOR_PAIR(color) | A_BOLD);
      printw("]");
      cardX += (int)card.first.size() + 2 + std::to_string(card.second).size() + 2;
    }

    // Clock
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);

    std::string timeStr = " " + std::string(timeBuf) + " ";
    int timeX = r.w - (int)timeStr.size() - 1;
    if (timeX > cardX) {
      attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
      mvaddstr(r.y, timeX, timeStr.c_str());
      attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
    }
  }
};

} // namespace monitor::ui
