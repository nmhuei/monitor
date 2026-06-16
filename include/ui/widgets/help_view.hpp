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
#include "panel.hpp"

namespace monitor::ui {

class HelpView {
public:
  static void draw(const Rect& r, DashboardState& state) {
    if (r.h <= 0 || r.w <= 0) return;

    drawPanelBox(r.y, r.x, r.w, r.h, "COMMAND & NAVIGATION REFERENCE", false);

    int currY = r.y + 2;

    auto printHeading = [&](const std::string& heading) {
      if (currY >= r.y + r.h - 1) return;
      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvprintw(currY, r.x + 3, "■ %s", heading.c_str());
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      currY += 2;
    };

    auto printRow = [&](const std::string& key, const std::string& desc) {
      if (currY >= r.y + r.h - 1) return;
      attron(COLOR_PAIR(CP_NORMAL) | A_BOLD);
      mvprintw(currY, r.x + 5, "%-16s", key.c_str());
      attroff(A_BOLD);
      attron(COLOR_PAIR(CP_DIM));
      printw(" %s", desc.c_str());
      attroff(COLOR_PAIR(CP_DIM));
      currY++;
    };

    // Columns approach or single column with scroll
    // Single column with section spacing is very readable
    printHeading("NAVIGATION KEYS");
    printRow("↑/↓ / k/j", "Move selection in host table / scroll lists");
    printRow("Enter", "Open selected host's detail screen");
    printRow("Esc / Backspace", "Return to main overview screen");
    printRow("Tab / Shift-Tab", "Navigate next/prev host in details screen");
    printRow("← / → / h/l", "Cycle detail screen tabs (Metrics, Cores, History, Events)");
    printRow("e / E", "Switch to dedicated events console screen");
    printRow("h / H / ?", "Open this help / command reference screen");
    printRow("U / u", "Cycle UI color themes (Catppuccin, Nord, Dracula, etc.)");
    printRow("q / Q", "Quit application");

    currY += 1;
    printHeading("COMMAND PALETTE SYNTAX (Press '/' to activate)");
    printRow("/host <name>", "Drill-down to specific hostname details");
    printRow("/history <name>", "Open metric historical log table directly");
    printRow("/events", "Open full events logs screen");
    printRow("/log clear", "Scroll current logs view to latest events");
    printRow("/filter <val>", "Quickly search hostnames / IPs");
    printRow("/filter role=<val>", "Filter by host role label (e.g. web, db, api)");
    printRow("/filter env=<val>", "Filter by host environment label (e.g. prod, dev)");
    printRow("/filter status=<val>", "Filter by status (online, stale, offline, warning, alert)");
    printRow("/filter clear", "Clear all active search/label filters");
    printRow("/sort <key> [desc|asc]", "Sort hosts by: status, host, cpu, ram, disk, load, age");
    printRow("/theme <name>", "Set theme directly: bloodline, mocha, nord, dracula, matrix, cyberpunk");
    printRow("/help", "Open this reference panel");
  }
};

} // namespace monitor::ui
