#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>
#include <unordered_map>
#include "../ui_types.hpp"
#include "../theme.hpp"
#include "../widgets/panel.hpp"
#include "../widgets/host_table.hpp"
#include "../widgets/metric_card.hpp"
#include "../widgets/event_list.hpp"
#include "iscreen.hpp"

namespace monitor::ui {

class OverviewScreen : public IScreen {
public:
  void render(const Layout& layout,
              const std::vector<HostState>& hosts,
              const std::vector<LogEvent>& log,
              DashboardState& state,
              const Thresholds& thresholds,
              const std::unordered_map<std::string, time_t>& firstSeen,
              std::vector<HostState>& outFilteredSorted) override {

    // 1. Draw central HostTable (always present)
    if (layout.kind == LayoutKind::Compact) {
      HostTable::draw(layout.mainPane, hosts, state, thresholds, outFilteredSorted);
    } 
    else if (layout.kind == LayoutKind::Standard) {
      HostTable::draw(layout.leftPane, hosts, state, thresholds, outFilteredSorted);

      if (!outFilteredSorted.empty()) {
        int selIdx = state.selectedHost;
        if (selIdx >= 0 && selIdx < (int)outFilteredSorted.size()) {
          const auto& selectedHost = outFilteredSorted[selIdx];
          time_t fsTime = firstSeen.count(selectedHost.name) ? firstSeen.at(selectedHost.name) : time(nullptr);
          MetricCard::draw(layout.mainPane, selectedHost, state, thresholds, log, fsTime);
        }
      } else {
        drawPanelBox(layout.mainPane.y, layout.mainPane.x, layout.mainPane.w, layout.mainPane.h, "PREVIEW PANEL", false);
        attron(COLOR_PAIR(CP_DIM));
        mvaddstr(layout.mainPane.y + layout.mainPane.h / 2, layout.mainPane.x + (layout.mainPane.w - 18) / 2, "No hosts selected.");
        attroff(COLOR_PAIR(CP_DIM));
      }
    } 
    else { // LayoutKind::Wide
      drawGroupsSidebar(layout.leftPane, hosts, state);
      HostTable::draw(layout.mainPane, hosts, state, thresholds, outFilteredSorted);

      if (!outFilteredSorted.empty()) {
        int selIdx = state.selectedHost;
        if (selIdx >= 0 && selIdx < (int)outFilteredSorted.size()) {
          const auto& selectedHost = outFilteredSorted[selIdx];
          time_t fsTime = firstSeen.count(selectedHost.name) ? firstSeen.at(selectedHost.name) : time(nullptr);
          MetricCard::draw(layout.rightPane, selectedHost, state, thresholds, log, fsTime);
        }
      } else {
        drawPanelBox(layout.rightPane.y, layout.rightPane.x, layout.rightPane.w, layout.rightPane.h, "PREVIEW PANEL", false);
        attron(COLOR_PAIR(CP_DIM));
        mvaddstr(layout.rightPane.y + layout.rightPane.h / 2, layout.rightPane.x + (layout.rightPane.w - 18) / 2, "No hosts selected.");
        attroff(COLOR_PAIR(CP_DIM));
      }
    }

    // 2. Draw Events preview (if present)
    if (layout.showEvents) {
      EventList::draw(layout.eventPane, log, state, false);
    }
  }

  void handleInput(int key,
                   DashboardState& state,
                   const std::vector<HostState>& hosts,
                   const std::vector<LogEvent>& log) override {
    
    // Build the filtered list to know selection bounds
    std::vector<HostState> filtered;
    for (const auto& h : hosts) {
      if (matchesFilter(h, state.filter)) {
        filtered.push_back(h);
      }
    }
    
    int hostCount = (int)filtered.size();
    int logCount = (int)log.size();

    switch (key) {
      case 's':
      case 'S':
        state.sortKey = static_cast<SortKey>((static_cast<int>(state.sortKey) + 1) % 7);
        break;

      case 'e':
      case 'E':
        state.screen = Screen::Events;
        state.focus = FocusPane::Events;
        break;

      case 'h':
      case 'H':
      case '?':
        state.screen = Screen::Help;
        break;

      case 9: // Tab
        if (state.focus == FocusPane::HostTable) {
          state.focus = FocusPane::Events;
        } else if (state.focus == FocusPane::Events) {
          state.focus = FocusPane::Groups;
        } else {
          state.focus = FocusPane::HostTable;
        }
        break;

      case '\n':
      case KEY_ENTER:
        if (hostCount > 0) {
          state.screen = Screen::HostDetail;
          state.focus = FocusPane::Detail;
          state.activeTab = 0;
        }
        break;

      case KEY_UP:
      case 'k':
        if (state.focus == FocusPane::Events) {
          state.eventScroll = std::min(logCount - 1, state.eventScroll + 1);
        } else {
          if (hostCount > 0) {
            state.selectedHost = (state.selectedHost + hostCount - 1) % hostCount;
          }
        }
        break;

      case KEY_DOWN:
      case 'j':
        if (state.focus == FocusPane::Events) {
          state.eventScroll = std::max(0, state.eventScroll - 1);
        } else {
          if (hostCount > 0) {
            state.selectedHost = (state.selectedHost + 1) % hostCount;
          }
        }
        break;

      case KEY_PPAGE:
        if (state.focus == FocusPane::Events) {
          state.eventScroll = std::min(logCount - 1, state.eventScroll + 10);
        } else {
          if (hostCount > 0) {
            state.selectedHost = std::max(0, state.selectedHost - 10);
          }
        }
        break;

      case KEY_NPAGE:
        if (state.focus == FocusPane::Events) {
          state.eventScroll = std::max(0, state.eventScroll - 10);
        } else {
          if (hostCount > 0) {
            state.selectedHost = std::min(hostCount - 1, state.selectedHost + 10);
          }
        }
        break;
    }
  }

private:
  void drawGroupsSidebar(const Rect& r, const std::vector<HostState>& hosts, const DashboardState& state) {
    drawPanelBox(r.y, r.x, r.w, r.h, "GROUPS", state.focus == FocusPane::Groups);

    int online = 0, warn = 0, alert = 0, stale = 0, offline = 0;
    std::unordered_map<std::string, int> roles;
    std::unordered_map<std::string, int> envs;

    for (const auto& h : hosts) {
      switch (h.status) {
        case HostStatus::ONLINE:   online++; break;
        case HostStatus::WARNING:  warn++; break;
        case HostStatus::ALERT:    alert++; break;
        case HostStatus::STALE:    stale++; break;
        case HostStatus::OFFLINE:  offline++; break;
      }

      std::string role, env, cluster;
      parseHostLabels(h.name, role, env, cluster);
      roles[role]++;
      envs[env]++;
    }

    int currY = r.y + 2;

    auto printCategory = [&](const std::string& name) {
      if (currY >= r.y + r.h - 1) return;
      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvprintw(currY, r.x + 2, "■ %s", name.c_str());
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      currY++;
    };

    auto printItem = [&](const std::string& key, int count, int colorCp) {
      if (currY >= r.y + r.h - 1) return;
      attron(COLOR_PAIR(colorCp) | A_BOLD);
      mvprintw(currY, r.x + 3, "●");
      attroff(COLOR_PAIR(colorCp) | A_BOLD);
      
      attron(COLOR_PAIR(CP_NORMAL));
      printw(" %-9s", key.c_str());
      attroff(COLOR_PAIR(CP_NORMAL));

      attron(COLOR_PAIR(CP_DIM));
      printw(" (%d)", count);
      attroff(COLOR_PAIR(CP_DIM));
      
      currY++;
    };

    printCategory("STATUS");
    if (online > 0) printItem("ONLINE", online, CP_OK);
    if (warn > 0) printItem("WARNING", warn, CP_WARN);
    if (alert > 0) printItem("ALERT", alert, CP_ALERT);
    if (stale > 0) printItem("STALE", stale, CP_STALE);
    if (offline > 0) printItem("OFFLINE", offline, CP_OFFLINE);
    currY++;

    printCategory("ROLES");
    for (const auto& pair : roles) {
      printItem(pair.first, pair.second, CP_NORMAL);
    }
    currY++;

    printCategory("ENVIRONMENTS");
    for (const auto& pair : envs) {
      printItem(pair.first, pair.second, CP_NORMAL);
    }
  }
};

} // namespace monitor::ui
