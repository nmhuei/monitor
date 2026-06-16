#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <algorithm>
#include "../theme.hpp"

namespace monitor::ui {

inline void drawPanelBox(int y, int x, int w, int h, const std::string &title, bool focused) {
  int cp = focused ? CP_ACCENT : CP_DIM;
  attron(COLOR_PAIR(cp));
  if (focused) attron(A_BOLD);

  mvaddstr(y, x, "┌");
  mvaddstr(y, x + w - 1, "┐");
  mvaddstr(y + h - 1, x, "└");
  mvaddstr(y + h - 1, x + w - 1, "┘");

  for (int col = x + 1; col < x + w - 1; ++col) {
    mvaddstr(y, col, "─");
    mvaddstr(y + h - 1, col, "─");
  }

  for (int row = y + 1; row < y + h - 1; ++row) {
    mvaddstr(row, x, "│");
    mvaddstr(row, x + w - 1, "│");
  }

  if (!title.empty()) {
    std::string t = title;
    std::transform(t.begin(), t.end(), t.begin(), ::toupper);
    std::string formatted = " ■ " + t + " ";
    if (static_cast<int>(formatted.size()) < w - 4) {
      mvaddstr(y, x + 2, formatted.c_str());
    }
  }

  if (focused) attroff(A_BOLD);
  attroff(COLOR_PAIR(cp));
}

} // namespace monitor::ui
