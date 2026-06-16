#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include "../protocol.hpp"

namespace monitor::ui {

enum ColorPair {
  CP_NORMAL = 1,
  CP_ACCENT,
  CP_OK,
  CP_WARN,
  CP_ALERT,
  CP_DIM,
  CP_HIGHLIGHT,
  CP_GRAPH_LOW,
  CP_GRAPH_MID,
  CP_GRAPH_HIGH,
  CP_BOX,
  CP_HEADER,
  CP_STALE,
  CP_OFFLINE
};

struct ThemePalette {
  int bg, fg;
  int accent;
  int ok, warn, alert;
  int dim;
  int highlight;
  int graphLow, graphMid, graphHigh;
  int stale;
  int offline;
};

static constexpr int NUM_THEMES = 6;
static const char *THEME_NAMES[NUM_THEMES] = {
  "BLOODLINE", "MOCHA", "NORD", "DRACULA", "MATRIX", "CYBERPUNK"
};

static const ThemePalette THEMES[NUM_THEMES] = {
  // 0: BLOODLINE (Crimson + Electric Cyan)
  { -1, 231, 160, 46, 208, 196, 241, 160, 46, 208, 196, 141, 244 },
  // 1: MOCHA (Catppuccin Mocha)
  { -1, 189, 105, 150, 216, 211, 61, 54, 150, 216, 211, 183, 244 },
  // 2: NORD (Arctic blues)
  { -1, 253, 67, 108, 179, 131, 66, 24, 108, 179, 131, 139, 244 },
  // 3: DRACULA (Deep purple + vivid accents)
  { -1, 255, 135, 84, 215, 203, 61, 57, 84, 215, 203, 183, 244 },
  // 4: MATRIX (lime green monochrome)
  { -1, 82, 28, 46, 40, 196, 28, 22, 46, 40, 196, 58, 244 },
  // 5: CYBERPUNK (Neon pink + acid yellow + electric cyan)
  { -1, 231, 198, 51, 228, 201, 238, 91, 51, 228, 201, 141, 244 }
};

static inline int fallbackColor(int c256) {
  if (c256 == 231 || c256 == 255 || c256 == 253 || c256 == 189) return COLOR_WHITE;
  if (c256 == 160 || c256 == 196 || c256 == 131 || c256 == 203 || c256 == 211) return COLOR_RED;
  if (c256 == 46 || c256 == 150 || c256 == 108 || c256 == 84) return COLOR_GREEN;
  if (c256 == 208 || c256 == 216 || c256 == 179 || c256 == 215 || c256 == 228 || c256 == 40) return COLOR_YELLOW;
  if (c256 == 51 || c256 == 117 || c256 == 122 || c256 == 123) return COLOR_CYAN;
  if (c256 == 105 || c256 == 110 || c256 == 135 || c256 == 201 || c256 == 183 || c256 == 139 || c256 == 91) return COLOR_MAGENTA;
  return COLOR_WHITE;
}

static inline void initColors(int themeIdx) {
  if (!has_colors()) return;
  start_color();
  use_default_colors();
  
  const auto &p = THEMES[themeIdx];
  auto initPair = [&](int pairId, int fg, int bg) {
    if (COLORS >= 256) {
      init_pair(pairId, fg, bg);
    } else {
      init_pair(pairId, fallbackColor(fg), bg == -1 ? -1 : fallbackColor(bg));
    }
  };

  initPair(CP_NORMAL,     p.fg, p.bg);
  initPair(CP_ACCENT,     p.accent, p.bg);
  initPair(CP_OK,         p.ok, p.bg);
  initPair(CP_WARN,       p.warn, p.bg);
  initPair(CP_ALERT,      p.alert, p.bg);
  initPair(CP_DIM,        p.dim, p.bg);
  initPair(CP_HIGHLIGHT,  p.fg, p.highlight);
  initPair(CP_GRAPH_LOW,  p.graphLow, p.bg);
  initPair(CP_GRAPH_MID,  p.graphMid, p.bg);
  initPair(CP_GRAPH_HIGH, p.graphHigh, p.bg);
  initPair(CP_BOX,        p.accent, p.bg);
  initPair(CP_HEADER,     p.fg, p.accent);
  initPair(CP_STALE,      p.stale, p.bg);
  initPair(CP_OFFLINE,    p.offline, p.bg);
}

inline const char* statusGlyph(HostStatus s) {
  switch (s) {
    case HostStatus::ONLINE:   return "●";
    case HostStatus::WARNING:  return "◐";
    case HostStatus::ALERT:    return "⬤";
    case HostStatus::STALE:    return "◌";
    case HostStatus::OFFLINE:  return "○";
  }
  return "?";
}

inline int statusColorPair(HostStatus s) {
  switch (s) {
    case HostStatus::ONLINE:   return CP_OK;
    case HostStatus::WARNING:  return CP_WARN;
    case HostStatus::ALERT:    return CP_ALERT;
    case HostStatus::STALE:    return CP_STALE;
    case HostStatus::OFFLINE:  return CP_OFFLINE;
  }
  return CP_NORMAL;
}

} // namespace monitor::ui
