#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <vector>
#include "../ui_types.hpp"
#include "../widgets/event_list.hpp"
#include "iscreen.hpp"

namespace monitor::ui {

class EventsScreen : public IScreen {
public:
  void render(const Layout& layout,
              const std::vector<HostState>&,
              const std::vector<LogEvent>& log,
              DashboardState& state,
              const Thresholds&,
              const std::unordered_map<std::string, time_t>&,
              std::vector<HostState>&) override {
    
    int contentY = layout.topBar.y + layout.topBar.h;
    int contentH = layout.statusBar.y - contentY;
    Rect r = {contentY, 0, contentH, layout.topBar.w};

    EventList::draw(r, log, state, true);
  }

  void handleInput(int key,
                   DashboardState& state,
                   const std::vector<HostState>&,
                   const std::vector<LogEvent>& log) override {
    int logCount = (int)log.size();

    switch (key) {
      case 27: // Esc
      case KEY_BACKSPACE:
      case 127:
      case 8:
        state.screen = Screen::Overview;
        state.focus = FocusPane::HostTable;
        break;

      case KEY_UP:
      case 'k':
        state.eventScroll = std::min(logCount - 1, state.eventScroll + 1);
        break;

      case KEY_DOWN:
      case 'j':
        state.eventScroll = std::max(0, state.eventScroll - 1);
        break;

      case KEY_PPAGE:
        state.eventScroll = std::min(logCount - 1, state.eventScroll + 10);
        break;

      case KEY_NPAGE:
        state.eventScroll = std::max(0, state.eventScroll - 10);
        break;
    }
  }
};

} // namespace monitor::ui
