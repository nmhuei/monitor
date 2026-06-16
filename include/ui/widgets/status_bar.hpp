#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>
#include "../ui_types.hpp"
#include "../theme.hpp"
#include "../command_registry.hpp"

namespace monitor::ui {

class StatusBar {
public:
  static void draw(const Rect& r, DashboardState& state, const std::vector<HostState>& hosts) {
    if (r.h <= 0 || r.w <= 0) return;

    // First, draw the command autocomplete box if command is active and has suggestions
    if (state.commandActive) {
      auto suggestions = CommandRegistry::suggest(state.commandBuffer, hosts);
      if (!suggestions.empty()) {
        int maxSuggestions = std::min((int)suggestions.size(), 5);
        int boxH = maxSuggestions + 2;
        int boxY = r.y - boxH;
        int boxX = 2;

        if (boxY >= 0) {
          // Draw frame
          attron(COLOR_PAIR(CP_ACCENT));
          mvaddstr(boxY, boxX, "┌─ SUGGESTIONS ──────────────────────┐");
          for (int i = 0; i < maxSuggestions; ++i) {
            mvaddstr(boxY + 1 + i, boxX, "│                                    │");
            // Highlight first suggestion or show all
            if (i == 0) {
              attron(A_REVERSE);
              mvprintw(boxY + 1 + i, boxX + 2, " %-34s ", suggestions[i].substr(0, 34).c_str());
              attroff(A_REVERSE);
            } else {
              mvprintw(boxY + 1 + i, boxX + 2, " %-34s ", suggestions[i].substr(0, 34).c_str());
            }
          }
          mvaddstr(boxY + 1 + maxSuggestions, boxX, "└────────────────────────────────────┘");
          attroff(COLOR_PAIR(CP_ACCENT));
        }
      }
    }

    // Clear and draw status bar line
    attron(COLOR_PAIR(CP_NORMAL));
    mvhline(r.y, r.x, ' ', r.w);
    attroff(COLOR_PAIR(CP_NORMAL));

    if (state.commandActive) {
      // Draw command input
      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvaddstr(r.y, r.x + 1, " > ");
      addstr(state.commandBuffer.c_str());
      addstr("_");
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);

      // Draw command tips on the right
      std::string tips = " [Tab] AutoComplete  [Esc] Cancel  [Enter] Execute ";
      int tipsX = r.w - (int)tips.size() - 1;
      if (tipsX > (int)state.commandBuffer.size() + 6) {
        attron(COLOR_PAIR(CP_DIM));
        mvaddstr(r.y, tipsX, tips.c_str());
        attroff(COLOR_PAIR(CP_DIM));
      }
    } 
    else if (state.commandErrorTimer > 0 && !state.commandError.empty()) {
      // Draw error message
      attron(COLOR_PAIR(CP_ALERT) | A_BOLD);
      mvprintw(r.y, r.x + 1, " ERR: %s", state.commandError.substr(0, r.w - 5).c_str());
      attroff(COLOR_PAIR(CP_ALERT) | A_BOLD);
    } 
    else {
      // Draw default hotkey hints based on screen
      std::string hints = " [↑/↓] Navigate  [Enter] Detail  [e] Events  [/] Cmd  [s] Sort  [f] Filter  [U] Theme  [q] Quit";
      if (state.screen == Screen::HostDetail) {
        hints = " [←/→] Tabs  [Tab] Next Host  [S-Tab] Prev Host  [Esc] Back  [/] Cmd  [U] Theme  [q] Quit";
      } else if (state.screen == Screen::Events) {
        hints = " [↑/↓] Scroll  [Esc] Back  [/] Cmd  [q] Quit";
      } else if (state.screen == Screen::Help) {
        hints = " [↑/↓] Scroll  [Esc] Back  [q] Quit";
      }

      attron(COLOR_PAIR(CP_NORMAL));
      mvaddstr(r.y, r.x + 1, hints.c_str());
      attroff(COLOR_PAIR(CP_NORMAL));
    }
  }
};

} // namespace monitor::ui
