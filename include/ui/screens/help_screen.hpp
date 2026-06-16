#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <vector>
#include "../ui_types.hpp"
#include "../widgets/help_view.hpp"
#include "iscreen.hpp"

namespace monitor::ui {

class HelpScreen : public IScreen {
public:
  void render(const Layout& layout,
              const std::vector<HostState>&,
              const std::vector<LogEvent>&,
              DashboardState& state,
              const Thresholds&,
              const std::unordered_map<std::string, time_t>&,
              std::vector<HostState>&) override {
    
    int contentY = layout.topBar.y + layout.topBar.h;
    int contentH = layout.statusBar.y - contentY;
    Rect r = {contentY, 0, contentH, layout.topBar.w};

    HelpView::draw(r, state);
  }

  void handleInput(int key,
                   DashboardState& state,
                   const std::vector<HostState>&,
                   const std::vector<LogEvent>&) override {
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
        state.helpScroll = std::max(0, state.helpScroll - 1);
        break;

      case KEY_DOWN:
      case 'j':
        state.helpScroll++;
        break;
    }
  }
};

} // namespace monitor::ui
