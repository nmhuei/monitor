#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <vector>
#include <unordered_map>
#include "../ui_types.hpp"
#include "../widgets/metric_card.hpp"
#include "iscreen.hpp"

namespace monitor::ui {

class HostDetailScreen : public IScreen {
public:
  void render(const Layout& layout,
              const std::vector<HostState>& hosts,
              const std::vector<LogEvent>& log,
              DashboardState& state,
              const Thresholds& thresholds,
              const std::unordered_map<std::string, time_t>& firstSeen,
              std::vector<HostState>&) override {

    int contentY = layout.topBar.y + layout.topBar.h;
    int contentH = layout.statusBar.y - contentY;
    Rect r = {contentY, 0, contentH, layout.topBar.w};

    if (hosts.empty()) {
      drawPanelBox(r.y, r.x, r.w, r.h, "HOST DETAILS", false);
      attron(COLOR_PAIR(CP_DIM));
      mvaddstr(r.y + r.h / 2, r.x + (r.w - 18) / 2, "No hosts connected.");
      attroff(COLOR_PAIR(CP_DIM));
      return;
    }

    if (state.selectedHost < 0) state.selectedHost = 0;
    if (state.selectedHost >= (int)hosts.size()) {
      state.selectedHost = (int)hosts.size() - 1;
    }

    const auto& host = hosts[state.selectedHost];
    time_t fsTime = firstSeen.count(host.name) ? firstSeen.at(host.name) : time(nullptr);

    MetricCard::draw(r, host, state, thresholds, log, fsTime);
  }

  void handleInput(int key,
                   DashboardState& state,
                   const std::vector<HostState>& hosts,
                   const std::vector<LogEvent>&) override {
    
    // Filter hosts on current active filters
    std::vector<HostState> filtered;
    for (const auto& h : hosts) {
      if (matchesFilter(h, state.filter)) {
        filtered.push_back(h);
      }
    }

    int hostCount = (int)filtered.size();

    switch (key) {
      case 9: // Tab
        if (hostCount > 0) {
          state.selectedHost = (state.selectedHost + 1) % hostCount;
        }
        break;

      case KEY_BTAB: // Shift+Tab
        if (hostCount > 0) {
          state.selectedHost = (state.selectedHost + hostCount - 1) % hostCount;
        }
        break;

      case 27: // Esc
      case KEY_BACKSPACE:
      case 127:
      case 8:
        state.screen = Screen::Overview;
        state.focus = FocusPane::HostTable;
        break;

      case KEY_UP:
      case 'k':
        if (state.activeTab == 2) {
          state.helpScroll = std::min(500, state.helpScroll + 1);
        }
        break;

      case KEY_DOWN:
      case 'j':
        if (state.activeTab == 2) {
          state.helpScroll = std::max(0, state.helpScroll - 1);
        }
        break;

      case KEY_LEFT:
        state.activeTab = (state.activeTab + 3) % 4;
        break;

      case KEY_RIGHT:
        state.activeTab = (state.activeTab + 1) % 4;
        break;

      case KEY_PPAGE:
        if (state.activeTab == 2) {
          state.helpScroll = std::min(500, state.helpScroll + 10);
        }
        break;

      case KEY_NPAGE:
        if (state.activeTab == 2) {
          state.helpScroll = std::max(0, state.helpScroll - 10);
        }
        break;
    }
  }
};

} // namespace monitor::ui
