#pragma once
/*
 * dashboard.hpp — btop++-style ncurses dashboard
 * Completely remade UX/UI with C++20 and ncursesw.
 */
#include "metrics_store.hpp"
#include "thresholds.hpp"
#include "protocol.hpp"

#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <deque>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace monitor::ui {

enum class ViewMode { OVERVIEW, DETAIL, HELP, HISTORY };
enum class PanelFocus { LIST, LOG, DETAIL };

// Color pairs index definitions
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
  int bg, fg;           // base background / foreground
  int accent;           // borders, headers
  int ok, warn, alert;  // status colors
  int dim;              // secondary text
  int highlight;        // selected row
  int graphLow, graphMid, graphHigh;  // sparkline gradient
  int stale;
  int offline;
};

static constexpr int NUM_THEMES = 6;
static const char *THEME_NAMES[NUM_THEMES] = {
  "BLOODLINE", "MOCHA", "NORD", "DRACULA", "MATRIX", "CYBERPUNK"
};

// 256-color palettes
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

// ── Helpers ──────────────────────────────────────────────────────────────────
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

inline std::string formatAge(time_t lastSeen, HostStatus status) {
  if (lastSeen == 0) return "never";
  time_t diff = time(nullptr) - lastSeen;
  if (diff < 0) diff = 0;
  if (status == HostStatus::OFFLINE) return "offline";
  if (status == HostStatus::STALE) return "stale " + std::to_string(diff) + "s";
  if (diff < 60) return std::to_string(diff) + "s";
  time_t mins = diff / 60;
  if (mins < 60) return std::to_string(mins) + "m";
  time_t hrs = mins / 60;
  return std::to_string(hrs) + "h";
}

inline const char* getBlockGlyph(float pct) {
  static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  int idx = std::clamp(static_cast<int>((pct / 100.f) * 8), 0, 8);
  return blocks[idx];
}

inline std::string drawBar(float pct, int width) {
  int filled = static_cast<int>((pct / 100.f) * width);
  filled = std::clamp(filled, 0, width);
  std::string out;
  for (int i = 0; i < filled; ++i) out += "█";
  for (int i = filled; i < width; ++i) out += "░";
  return out;
}

inline std::string makeBrailleChar(int h1, int h2) {
  static const int mask_left[] = {0, 0x40, 0x44, 0x46, 0x47};
  static const int mask_right[] = {0, 0x80, 0x20 | 0x80, 0x10 | 0x20 | 0x80, 0x08 | 0x10 | 0x20 | 0x80};
  int val = mask_left[std::clamp(h1, 0, 4)] + mask_right[std::clamp(h2, 0, 4)];
  int cp = 0x2800 + val;
  std::string utf8;
  utf8 += static_cast<char>(0xE0 | (cp >> 12));
  utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  utf8 += static_cast<char>(0x80 | (cp & 0x3F));
  return utf8;
}

inline void drawBrailleSparkline(int start_y, int start_x, int W, int H,
                                 const std::vector<float>& values, float thresh) {
  std::vector<float> data(2 * W, 0.0f);
  int offset = static_cast<int>(2 * W) - static_cast<int>(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    int target_idx = static_cast<int>(i) + offset;
    if (target_idx >= 0 && target_idx < 2 * W) {
      data[target_idx] = values[i];
    }
  }

  for (int row_idx = H - 1; row_idx >= 0; --row_idx) {
    int screen_y = start_y + (H - 1 - row_idx);
    move(screen_y, start_x);
    for (int char_x = 0; char_x < W; ++char_x) {
      int col1 = 2 * char_x;
      int col2 = 2 * char_x + 1;
      
      float v1 = data[col1];
      float v2 = data[col2];
      
      int height1 = std::clamp(static_cast<int>((v1 / 100.f) * (4 * H)), 0, 4 * H);
      int height2 = std::clamp(static_cast<int>((v2 / 100.f) * (4 * H)), 0, 4 * H);
      
      int h1 = std::clamp(height1 - 4 * row_idx, 0, 4);
      int h2 = std::clamp(height2 - 4 * row_idx, 0, 4);
      
      float avg = (v1 + v2) / 2.0f;
      int cp = CP_GRAPH_LOW;
      if (avg >= thresh) cp = CP_GRAPH_HIGH;
      else if (avg >= thresh * 0.8f) cp = CP_GRAPH_MID;
      
      attron(COLOR_PAIR(cp));
      if (h1 > 0 || h2 > 0) {
        addstr(makeBrailleChar(h1, h2).c_str());
      } else {
        addch(' ');
      }
      attroff(COLOR_PAIR(cp));
    }
  }
}

inline void drawBlockSparkline(int start_y, int start_x, int W, int H,
                               const std::vector<float>& values, float thresh) {
  std::vector<float> data(W, 0.0f);
  int offset = W - static_cast<int>(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    int target_idx = static_cast<int>(i) + offset;
    if (target_idx >= 0 && target_idx < W) {
      data[target_idx] = values[i];
    }
  }

  static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

  for (int row_idx = H - 1; row_idx >= 0; --row_idx) {
    int screen_y = start_y + (H - 1 - row_idx);
    move(screen_y, start_x);
    for (int char_x = 0; char_x < W; ++char_x) {
      float v = data[char_x];
      int total_levels = H * 8;
      int height = std::clamp(static_cast<int>((v / 100.f) * total_levels), 0, total_levels);
      int h = std::clamp(height - 8 * row_idx, 0, 8);
      
      int cp = CP_GRAPH_LOW;
      if (v >= thresh) cp = CP_GRAPH_HIGH;
      else if (v >= thresh * 0.8f) cp = CP_GRAPH_MID;
      
      attron(COLOR_PAIR(cp));
      addstr(blocks[h]);
      attroff(COLOR_PAIR(cp));
    }
  }
}

inline void drawSparkline(int start_y, int start_x, int W, int H,
                          const std::vector<float>& values, float thresh) {
#ifdef HAVE_NCURSESW
  drawBrailleSparkline(start_y, start_x, W, H, values, thresh);
#else
  drawBlockSparkline(start_y, start_x, W, H, values, thresh);
#endif
}

inline void drawPanelBox(int y, int x, int w, int h, const std::string &title, bool focused) {
  static const char *TL = "┌";
  static const char *TR = "┐";
  static const char *BL = "└";
  static const char *BR = "┘";
  static const char *H  = "─";
  static const char *V  = "│";

  int cp = focused ? CP_ACCENT : CP_DIM;
  attron(COLOR_PAIR(cp));
  if (focused) attron(A_BOLD);

  mvaddstr(y, x, TL);
  mvaddstr(y, x + w - 1, TR);
  mvaddstr(y + h - 1, x, BL);
  mvaddstr(y + h - 1, x + w - 1, BR);

  for (int col = x + 1; col < x + w - 1; ++col) {
    mvaddstr(y, col, H);
    mvaddstr(y + h - 1, col, H);
  }

  for (int row = y + 1; row < y + h - 1; ++row) {
    mvaddstr(row, x, V);
    mvaddstr(row, x + w - 1, V);
  }

  if (!title.empty()) {
    std::string formatted = " " + title + " ";
    if (static_cast<int>(formatted.size()) < w - 4) {
      mvaddstr(y, x + 2, formatted.c_str());
    }
  }

  if (focused) attroff(A_BOLD);
  attroff(COLOR_PAIR(cp));
}

inline std::string padRight(const std::string &s, size_t n) {
  if (s.size() >= n) return s.substr(0, n);
  return s + std::string(n - s.size(), ' ');
}

inline std::string padLeft(const std::string &s, size_t n) {
  if (s.size() >= n) return s.substr(0, n);
  return std::string(n - s.size(), ' ') + s;
}

inline std::string formatNet(float kb) {
  if (kb < 0.1f) return "0B";
  if (kb < 1024.f) {
    char b[16];
    snprintf(b, sizeof(b), "%.0fK", kb);
    return b;
  }
  char b[16];
  snprintf(b, sizeof(b), "%.1fM", kb / 1024.f);
  return b;
}

inline std::string formatUptime(time_t seconds) {
  if (seconds < 60) return std::to_string(seconds) + "s";
  time_t mins = seconds / 60;
  time_t secs = seconds % 60;
  if (mins < 60) return std::to_string(mins) + "m " + std::to_string(secs) + "s";
  time_t hrs = mins / 60;
  mins = mins % 60;
  return std::to_string(hrs) + "h " + std::to_string(mins) + "m";
}

// ── Dashboard Class ──────────────────────────────────────────────────────────
class Dashboard {
public:
  Dashboard() : uiMode_(0), logScroll_(0), histScroll_(0), selectedIdx_(0),
                listScroll_(0), viewMode_(ViewMode::OVERVIEW), focus_(PanelFocus::LIST),
                cmdActive_(false), cmdErrorTimer_(0), startTime_(time(nullptr)) {}

  ~Dashboard() {
    teardown();
  }

  void init() {
    setlocale(LC_ALL, "");
    initscr(); noecho(); cbreak(); curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    initColors(uiMode_);
    getmaxyx(stdscr, rows_, cols_);
    startTime_ = time(nullptr);
  }

  void teardown() {
    if (!isendwin()) endwin();
  }

  void render(const std::vector<HostState> &hosts,
              const std::vector<LogEvent> &log,
              const Thresholds &thresh) {
    getmaxyx(stdscr, rows_, cols_);

    // Guard: Terminal too small
    if (rows_ < 24 || cols_ < 80) {
      int ch = getch();
      if (ch == 'q' || ch == 'Q') {
        teardown();
        exit(0);
      }
      if (ch == KEY_RESIZE) {
        endwin(); refresh();
        getmaxyx(stdscr, rows_, cols_);
        clear();
      }
      renderTooSmall();
      return;
    }

    // Populate first seen timestamps
    for (const auto &h : hosts) {
      if (firstSeen_.find(h.name) == firstSeen_.end()) {
        firstSeen_[h.name] = time(nullptr);
      }
    }

    // Copy and stable sort
    sortedHosts_ = hosts;
    std::stable_sort(sortedHosts_.begin(), sortedHosts_.end(),
                     [](const HostState &a, const HostState &b) {
                       if (a.status != b.status)
                         return (int)a.status < (int)b.status;
                       return a.name < b.name;
                     });

    // Draining all pending input
    int ch;
    while ((ch = getch()) != ERR) {
      handleKey(ch, log);
    }

    // Process errors timers
    if (cmdErrorTimer_ > 0) {
      cmdErrorTimer_--;
    }

    // Dynamic layout coordinates
    int headerHeight = 1;
    int statusBarHeight = 1;
    
    int logHeight = 0;
    int logY = 0;
    if (rows_ >= 30) {
      logHeight = static_cast<int>(rows_ * 0.20);
      logY = rows_ - 1 - logHeight;
    }
    
    int contentHeight = (logHeight > 0) ? (logY - headerHeight) : (rows_ - headerHeight - statusBarHeight);
    int contentY = headerHeight;
    
    int listWidth = cols_;
    int detailWidth = 0;
    int detailX = 0;
    if (cols_ >= 100) {
      listWidth = static_cast<int>(cols_ * 0.35);
      detailWidth = cols_ - listWidth;
      detailX = listWidth;
    }

    erase();
    
    // Draw Header
    drawHeader();

    // Draw Panels
    drawHostList(contentY, 0, listWidth, contentHeight, thresh);
    if (detailWidth > 0) {
      drawDetailPanel(contentY, detailX, detailWidth, contentHeight, thresh);
    }
    if (logHeight > 0) {
      drawLogPanel(logY, 0, cols_, logHeight, thresh, log);
    }

    // Draw Status Bar
    drawStatusBar(rows_ - 1, cols_);

    refresh();
  }

private:
  void renderTooSmall() {
    erase();
    int r = rows_, c = cols_;
    if (r >= 6 && c >= 36) {
      attron(COLOR_PAIR(CP_BOX)|A_BOLD);
      mvaddstr(0, 0, "┌"); for (int i = 1; i < c - 1; i++) addstr("─"); addstr("┐");
      mvaddstr(r - 1, 0, "└"); for (int i = 1; i < c - 1; i++) addstr("─"); addstr("┘");
      for (int i = 1; i < r - 1; i++) { mvaddstr(i, 0, "│"); mvaddstr(i, c - 1, "│"); }
      attroff(COLOR_PAIR(CP_BOX)|A_BOLD);
    }

    int my = r / 2 - 2;
    if (my < 1) my = 0;

    attron(COLOR_PAIR(CP_ALERT)|A_BOLD);
    std::string warn = " Terminal size too small ";
    mvaddstr(my, (c - static_cast<int>(warn.size())) / 2, warn.c_str());
    attroff(COLOR_PAIR(CP_ALERT)|A_BOLD);

    attron(COLOR_PAIR(CP_NORMAL));
    std::string size_cur = "Current: Width=" + std::to_string(c) + " Height=" + std::to_string(r);
    std::string size_req = "Needed : Width=80  Height=24";
    mvaddstr(my + 2, (c - static_cast<int>(size_cur.size())) / 2, size_cur.c_str());
    mvaddstr(my + 3, (c - static_cast<int>(size_req.size())) / 2, size_req.c_str());
    attroff(COLOR_PAIR(CP_NORMAL));
    refresh();
  }

  void drawHeader() {
    int onlineCount = 0, staleCount = 0, offlineCount = 0;
    for (const auto &h : sortedHosts_) {
      if (h.status == HostStatus::ONLINE || h.status == HostStatus::WARNING || h.status == HostStatus::ALERT)
        onlineCount++;
      else if (h.status == HostStatus::STALE)
        staleCount++;
      else if (h.status == HostStatus::OFFLINE)
        offlineCount++;
    }

    time_t now = time(nullptr);
    struct tm *tm_ = localtime(&now);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm_);

    std::string uptimeStr = "Uptime: " + formatUptime(time(nullptr) - startTime_);
    std::string statusStr = "ONLINE:" + std::to_string(onlineCount) +
                            " STALE:" + std::to_string(staleCount) +
                            " OFFLINE:" + std::to_string(offlineCount);
    std::string themeStr = "Theme: " + std::string(THEME_NAMES[uiMode_]);

    attron(COLOR_PAIR(CP_HEADER)|A_BOLD);
    mvhline(0, 0, ' ', cols_);
    mvprintw(0, 1, "◈ DISTRIBUTED MONITOR ◈  %s", uptimeStr.c_str());
    
    int statsX = cols_ / 2 - static_cast<int>(statusStr.size()) / 2;
    mvaddstr(0, statsX, statusStr.c_str());
    
    int rightX = cols_ - 1 - static_cast<int>(themeStr.size()) - static_cast<int>(strlen(tbuf)) - 2;
    if (rightX > statsX + static_cast<int>(statusStr.size())) {
      mvprintw(0, rightX, "%s  %s", themeStr.c_str(), tbuf);
    } else {
      mvprintw(0, cols_ - 1 - static_cast<int>(strlen(tbuf)), "%s", tbuf);
    }
    attroff(COLOR_PAIR(CP_HEADER)|A_BOLD);
  }

  void drawHostList(int y, int x, int w, int h, const Thresholds &thresh) {
    std::string title = "HOSTS (" + std::to_string(sortedHosts_.size()) + ")";
    drawPanelBox(y, x, w, h, title, focus_ == PanelFocus::LIST);
    
    int max_visible = h - 2;
    int hostCount = static_cast<int>(sortedHosts_.size());
    if (selectedIdx_ >= hostCount) selectedIdx_ = std::max(0, hostCount - 1);
    
    if (selectedIdx_ < listScroll_) {
      listScroll_ = selectedIdx_;
    } else if (selectedIdx_ >= listScroll_ + max_visible) {
      listScroll_ = selectedIdx_ - max_visible + 1;
    }
    
    int internalY = y + 1;
    for (int i = listScroll_; i < listScroll_ + max_visible && i < hostCount; ++i) {
      const auto &h_state = sortedHosts_[i];
      bool isSelected = (i == selectedIdx_);
      int rowAttr = COLOR_PAIR(CP_NORMAL);
      if (isSelected) {
        rowAttr = COLOR_PAIR(CP_HIGHLIGHT) | (focus_ == PanelFocus::LIST ? A_BOLD : A_NORMAL);
      }
      
      attron(rowAttr);
      mvhline(internalY, x + 1, ' ', w - 2);
      
      // Status icon
      int sCol = statusColorPair(h_state.status);
      if (isSelected) sCol = CP_HIGHLIGHT;
      attron(COLOR_PAIR(sCol));
      mvaddstr(internalY, x + 2, statusGlyph(h_state.status));
      attroff(COLOR_PAIR(sCol));
      
      // Name
      std::string name = h_state.name;
      int nameLimit = w - 30;
      if (nameLimit < 6) nameLimit = 6;
      if (static_cast<int>(name.size()) > nameLimit) {
        name = name.substr(0, nameLimit - 1) + "…";
      }
      mvaddstr(internalY, x + 4, padRight(name, nameLimit).c_str());
      
      // CPU/RAM/Disk compact bars
      std::string compactBars = std::string("[") + 
                                getBlockGlyph(h_state.cpu) + 
                                getBlockGlyph(h_state.ram) + 
                                getBlockGlyph(h_state.disk) + "]";
      mvaddstr(internalY, x + 4 + nameLimit + 1, compactBars.c_str());
      
      // Net RX/TX
      std::string netStr = "↓" + formatNet(h_state.netRxKB) + " ↑" + formatNet(h_state.netTxKB);
      mvaddstr(internalY, x + 4 + nameLimit + 7, padRight(netStr, 12).c_str());
      
      // Time since last seen
      std::string ageStr = formatAge(h_state.lastSeen, h_state.status);
      mvaddstr(internalY, x + w - 2 - static_cast<int>(ageStr.size()), ageStr.c_str());
      
      attroff(rowAttr);
      internalY++;
    }
  }

  void drawHelpCard(int y, int x, int w, int h) {
    std::string title = "HELP & COMMANDS  [Esc] Back";
    drawPanelBox(y, x, w, h, title, focus_ == PanelFocus::DETAIL);
    
    int lx = x + 3;
    int rx = x + 22;
    int currY = y + 2;
    
    // 1. Navigation
    attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    mvaddstr(currY++, lx, "NAVIGATION");
    attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
    
    struct KeyHelp { const char *key; const char *desc; };
    KeyHelp navHelp[] = {
      {"Tab / S-Tab", "Next/Prev host in details"},
      {"Enter",       "Open detail view"},
      {"Esc",         "Back to overview"},
      {"L / l",       "Toggle log panel focus"},
      {"U / u",       "Cycle theme (6 themes)"},
      {"↑ / ↓",       "Navigate or scroll log"},
      {"PgUp / PgDn", "Fast scroll log/history"},
      {"/",           "Open command bar"},
      {"Q / q",       "Quit application"}
    };
    
    for (const auto &nh : navHelp) {
      if (currY >= y + h - 2) break;
      attron(COLOR_PAIR(CP_OK) | A_BOLD);
      mvprintw(currY, lx, "%-17s", nh.key);
      attroff(COLOR_PAIR(CP_OK) | A_BOLD);
      
      attron(COLOR_PAIR(CP_NORMAL));
      mvaddstr(currY, rx, nh.desc);
      attroff(COLOR_PAIR(CP_NORMAL));
      currY++;
    }
    
    currY++;
    if (currY < y + h - 2) {
      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvaddstr(currY++, lx, "THEMES");
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      
      for (int i = 0; i < NUM_THEMES; ++i) {
        if (currY >= y + h - 2) break;
        bool isCurrent = (i == uiMode_);
        int cp = isCurrent ? CP_WARN : CP_DIM;
        attron(COLOR_PAIR(cp) | (isCurrent ? A_BOLD : 0));
        mvprintw(currY++, lx, "%d. %s%s", i + 1, THEME_NAMES[i], isCurrent ? " (active)" : "");
        attroff(COLOR_PAIR(cp) | (isCurrent ? A_BOLD : 0));
      }
    }
    
    currY++;
    if (currY < y + h - 2) {
      attron(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      mvaddstr(currY++, lx, "COMMANDS");
      attroff(COLOR_PAIR(CP_ACCENT) | A_BOLD);
      
      KeyHelp cmdHelp[] = {
        {"/help",           "Show this help screen"},
        {"/clear",          "Clear scrollback"},
        {"/host <name>",    "Focus & view host detail"},
        {"/history <name>", "Show history list for host"}
      };
      
      for (const auto &ch : cmdHelp) {
        if (currY >= y + h - 2) break;
        attron(COLOR_PAIR(CP_ALERT) | A_BOLD);
        mvprintw(currY, lx, "%-17s", ch.key);
        attroff(COLOR_PAIR(CP_ALERT) | A_BOLD);
        
        attron(COLOR_PAIR(CP_NORMAL));
        mvaddstr(currY, rx, ch.desc);
        attroff(COLOR_PAIR(CP_NORMAL));
        currY++;
      }
    }
  }

  void drawDetailPanel(int y, int x, int w, int h, const Thresholds &thresh) {
    if (viewMode_ == ViewMode::HELP) {
      drawHelpCard(y, x, w, h);
      return;
    }
    
    if (sortedHosts_.empty()) {
      drawPanelBox(y, x, w, h, "DETAIL PANEL", focus_ == PanelFocus::DETAIL);
      attron(COLOR_PAIR(CP_DIM));
      mvaddstr(y + h / 2, x + (w - 20) / 2, "No hosts connected.");
      attroff(COLOR_PAIR(CP_DIM));
      return;
    }
    
    if (selectedIdx_ >= static_cast<int>(sortedHosts_.size())) {
      selectedIdx_ = 0;
    }
    const auto &host = sortedHosts_[selectedIdx_];

    if (viewMode_ == ViewMode::HISTORY) {
      drawHistoryView(y, x, w, h);
      return;
    }

    std::string title = "DETAIL: " + host.name;
    drawPanelBox(y, x, w, h, title, focus_ == PanelFocus::DETAIL);

    // 1. Host Header
    attron(COLOR_PAIR(CP_NORMAL)|A_BOLD);
    mvprintw(y + 1, x + 2, "%s  (%s)", host.name.c_str(), host.ip.c_str());
    attroff(A_BOLD);
    
    int sCol = statusColorPair(host.status);
    attron(COLOR_PAIR(sCol)|A_BOLD);
    std::string sText = " " + std::string(statusStr(host.status)) + " ";
    mvaddstr(y + 1, x + w - 2 - static_cast<int>(sText.size()) - 15, sText.c_str());
    attroff(COLOR_PAIR(sCol)|A_BOLD);

    time_t firstTime = getFirstSeen(host.name);
    std::string trackingStr = "Active: " + formatUptime(time(nullptr) - firstTime);
    attron(COLOR_PAIR(CP_DIM));
    mvaddstr(y + 1, x + w - 2 - static_cast<int>(trackingStr.size()), trackingStr.c_str());
    attroff(COLOR_PAIR(CP_DIM));

    // 2. Metrics summary row
    for (int col = x + 1; col < x + w - 1; ++col) {
      mvaddstr(y + 2, col, "─");
    }
    
    char summaryBuf[512];
    snprintf(summaryBuf, sizeof(summaryBuf), 
             "CPU:%5.1f%% | RAM:%5.1f%% | DISK:%5.1f%% | LOAD:%4.2f | PROCS:%4d | RX:%6s/s | TX:%6s/s",
             host.cpu, host.ram, host.disk, host.loadAvg, host.procCount, 
             formatNet(host.netRxKB).c_str(), formatNet(host.netTxKB).c_str());
    attron(COLOR_PAIR(CP_NORMAL));
    mvaddstr(y + 3, x + 2, summaryBuf);
    attroff(COLOR_PAIR(CP_NORMAL));

    for (int col = x + 1; col < x + w - 1; ++col) {
      mvaddstr(y + 4, col, "─");
    }

    // 3. Bento Grid for Sparklines
    int gridH = (h - 7) / 2;
    if (gridH < 5) gridH = 5;
    int gridW = (w - 4) / 2;
    
    std::vector<float> cpu_h, ram_h, dsk_h, net_h;
    for (const auto &s : host.history) {
      cpu_h.push_back(s.cpu);
      ram_h.push_back(s.ram);
      dsk_h.push_back(s.disk);
      net_h.push_back(s.netRxKB + s.netTxKB);
    }

    auto drawGraphBox = [&](int gy, int gx, const std::string &gTitle, const std::vector<float> &vals, float tVal) {
      drawPanelBox(gy, gx, gridW, gridH, gTitle, false);
      int graphW = gridW - 9;
      int graphH = gridH - 2;
      if (graphW > 2 && graphH > 1) {
        drawSparkline(gy + 1, gx + 1, graphW, graphH, vals, tVal);
        
        float minV = 100.f, maxV = 0.f, sumV = 0.f;
        for (float v : vals) {
          if (v < minV) minV = v;
          if (v > maxV) maxV = v;
          sumV += v;
        }
        float avgV = vals.empty() ? 0.f : sumV / vals.size();
        if (vals.empty()) minV = 0.f;

        attron(COLOR_PAIR(CP_DIM));
        mvprintw(gy + 1, gx + 1 + graphW + 1, "▲ %3.0f", maxV);
        mvprintw(gy + 1 + graphH/2, gx + 1 + graphW + 1, "■ %3.0f", avgV);
        mvprintw(gy + 1 + graphH - 1, gx + 1 + graphW + 1, "▼ %3.0f", minV);
        attroff(COLOR_PAIR(CP_DIM));
      }
    };

    drawGraphBox(y + 5, x + 2, "CPU HIST (60s)", cpu_h, thresh.getCPU(host.name));
    drawGraphBox(y + 5, x + 2 + gridW, "RAM HIST (60s)", ram_h, thresh.getRAM(host.name));
    drawGraphBox(y + 5 + gridH, x + 2, "DISK HIST (60s)", dsk_h, thresh.getDisk(host.name));
    
    // Max Net value scaling for Net graph
    float maxNet = 10.f;
    for (float v : net_h) if (v > maxNet) maxNet = v;
    // We normalize Net history to percentage scaled by maxNet
    std::vector<float> net_scaled;
    for (float v : net_h) net_scaled.push_back((v / maxNet) * 100.f);
    drawGraphBox(y + 5 + gridH, x + 2 + gridW, "NET I/O HIST", net_scaled, 80.f);

    // 4. Per-core CPU grid
    int coresY = y + 5 + 2 * gridH;
    int cols_count = 4;
    int col_w = (w - 4) / cols_count;
    int max_cores_visible = std::max(1, h - 1 - (5 + 2 * gridH)) * cols_count;
    int cores_to_draw = std::min(host.coreCount, max_cores_visible);

    for (int i = 0; i < cores_to_draw; ++i) {
      int core_row = i / cols_count;
      int core_col = i % cols_count;
      int cy = coresY + core_row;
      int cx = x + 2 + core_col * col_w;
      
      if (cy >= y + h - 1) break;
      
      float val = host.cores[i];
      std::string bar = drawBar(val, 6);
      char buf[64];
      snprintf(buf, sizeof(buf), "C%-2d [%s] %2.0f%%", i, bar.c_str(), val);
      
      int cp = CP_NORMAL;
      if (val >= thresh.getCPU(host.name)) cp = CP_ALERT;
      else if (val >= thresh.getCPU(host.name) * 0.8f) cp = CP_WARN;
      
      attron(COLOR_PAIR(cp));
      mvaddnstr(cy, cx, buf, col_w - 1);
      attroff(COLOR_PAIR(cp));
    }
  }

  void drawHistoryView(int y, int x, int w, int h) {
    if (sortedHosts_.empty() || selectedIdx_ >= static_cast<int>(sortedHosts_.size())) {
      drawPanelBox(y, x, w, h, "HISTORY VIEW", focus_ == PanelFocus::DETAIL);
      return;
    }
    const auto &host = sortedHosts_[selectedIdx_];
    std::string title = "HISTORY: " + host.name + "  [Esc] Back";
    drawPanelBox(y, x, w, h, title, focus_ == PanelFocus::DETAIL);
    
    int max_visible = h - 4;
    int hist_size = static_cast<int>(host.history.size());
    
    attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
    mvprintw(y + 1, x + 2, "%-10s %7s %7s %7s %8s %8s %8s", "TIME", "CPU%", "RAM%", "DISK%", "LOAD", "RX KB/s", "TX KB/s");
    attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
    
    for (int col = x + 1; col < x + w - 1; ++col) {
      mvaddstr(y + 2, col, "─");
    }
    
    int internalY = y + 3;
    if (histScroll_ >= hist_size) {
      histScroll_ = std::max(0, hist_size - 1);
    }
    int start_idx = hist_size - 1 - histScroll_;
    int end_idx = std::max(0, start_idx - max_visible + 1);
    
    for (int i = start_idx; i >= end_idx && i < hist_size; --i) {
      if (internalY >= y + h - 1) break;
      const auto &s = host.history[i];
      
      struct tm *tm_ = localtime(&s.ts);
      char tbuf[16];
      strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_);
      
      attron(COLOR_PAIR(CP_NORMAL));
      mvprintw(internalY, x + 2, "%-10s %6.1f%% %6.1f%% %6.1f%% %8.2f %8.1f %8.1f",
               tbuf, s.cpu, s.ram, s.disk, s.loadAvg, s.netRxKB, s.netTxKB);
      attroff(COLOR_PAIR(CP_NORMAL));
      internalY++;
    }
  }

  void drawLogPanel(int y, int x, int w, int h, const Thresholds &thresh, const std::vector<LogEvent> &log) {
    std::string title = "LOGS (" + std::to_string(log.size()) + ")";
    if (logScroll_ > 0) {
      title += "  [Scrollback: " + std::to_string(logScroll_) + "]";
    }
    drawPanelBox(y, x, w, h, title, focus_ == PanelFocus::LOG);

    int max_visible = h - 2;
    int log_size = static_cast<int>(log.size());
    if (logScroll_ >= log_size) {
      logScroll_ = std::max(0, log_size - 1);
    }
    
    int start_idx = log_size - 1 - logScroll_;
    int end_idx = std::max(0, start_idx - max_visible + 1);
    
    int internalY = y + 1;
    for (int i = start_idx; i >= end_idx && i < log_size; --i) {
      if (internalY >= y + h - 1) break;
      const auto &ev = log[i];
      
      struct tm *tm_ = localtime(&ev.ts);
      char tbuf[16];
      strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_);
      
      int cp = CP_NORMAL;
      std::string typeStr;
      switch (ev.type) {
        case LogEventType::CONNECT:
          cp = CP_OK; typeStr = "CONNECT"; break;
        case LogEventType::DISCONNECT:
          cp = CP_WARN; typeStr = "DISCONNECT"; break;
        case LogEventType::ALERT:
          cp = CP_ALERT; typeStr = "ALERT"; break;
        case LogEventType::STALE:
          cp = CP_WARN; typeStr = "STALE"; break;
        case LogEventType::METRIC:
          cp = CP_DIM; typeStr = "METRIC"; break;
      }
      
      attron(COLOR_PAIR(CP_DIM));
      mvprintw(internalY, x + 2, "[%s] ", tbuf);
      attroff(COLOR_PAIR(CP_DIM));
      
      attron(COLOR_PAIR(cp)|A_BOLD);
      mvaddstr(internalY, x + 13, padRight(typeStr, 11).c_str());
      attroff(COLOR_PAIR(cp)|A_BOLD);
      
      attron(COLOR_PAIR(CP_NORMAL));
      mvprintw(internalY, x + 25, "● %-12s (%-12s) → %s", 
               ev.host.substr(0,12).c_str(), ev.ip.substr(0,12).c_str(), ev.detail.c_str());
      attroff(COLOR_PAIR(CP_NORMAL));
      
      internalY++;
    }
  }

  void drawStatusBar(int y, int w) {
    attron(COLOR_PAIR(CP_NORMAL));
    mvhline(y, 0, ' ', w);
    
    if (cmdActive_) {
      attron(COLOR_PAIR(CP_ACCENT)|A_BOLD);
      mvaddstr(y, 1, " > /");
      addstr(cmdBuf_.c_str());
      addstr("_");
      attroff(COLOR_PAIR(CP_ACCENT)|A_BOLD);
    } else if (cmdErrorTimer_ > 0) {
      attron(COLOR_PAIR(CP_ALERT)|A_BOLD);
      mvaddnstr(y, 1, cmdError_.c_str(), w - 2);
      attroff(COLOR_PAIR(CP_ALERT)|A_BOLD);
    } else {
      attron(COLOR_PAIR(CP_DIM));
      mvaddstr(y, 1, " [↑↓] Navigate   [Tab] Detail/Next   [L] Toggle Log   [U] Cycle Theme   [/] Command   [Q] Quit");
      attroff(COLOR_PAIR(CP_DIM));
    }
    attroff(COLOR_PAIR(CP_NORMAL));
  }

  bool handleKey(int ch, const std::vector<LogEvent> &log) {
    if (cmdActive_) {
      handleCmdKey(ch);
      return false;
    }
    
    int hostCount = static_cast<int>(sortedHosts_.size());
    int logCount = static_cast<int>(log.size());
    
    switch (ch) {
      case 'q':
      case 'Q':
        teardown();
        exit(0);
        
      case 'u':
      case 'U':
        uiMode_ = (uiMode_ + 1) % NUM_THEMES;
        initColors(uiMode_);
        break;
        
      case '/':
        cmdActive_ = true;
        cmdBuf_.clear();
        break;
        
      case 9: // Tab
        if (viewMode_ == ViewMode::OVERVIEW) {
          if (hostCount > 0) {
            viewMode_ = ViewMode::DETAIL;
            focus_ = PanelFocus::DETAIL;
            selectedIdx_ = 0;
          }
        } else if (viewMode_ == ViewMode::DETAIL) {
          if (hostCount > 0) {
            selectedIdx_ = (selectedIdx_ + 1) % hostCount;
          }
        }
        break;
        
      case KEY_BTAB: // Shift+Tab
        if (viewMode_ == ViewMode::DETAIL) {
          if (hostCount > 0) {
            selectedIdx_ = (selectedIdx_ + hostCount - 1) % hostCount;
          }
        }
        break;
        
      case 27: // Esc
        if (viewMode_ != ViewMode::OVERVIEW) {
          viewMode_ = ViewMode::OVERVIEW;
          focus_ = PanelFocus::LIST;
        }
        break;
        
      case '\n':
      case KEY_ENTER:
        if (viewMode_ == ViewMode::OVERVIEW) {
          if (hostCount > 0) {
            viewMode_ = ViewMode::DETAIL;
            focus_ = PanelFocus::DETAIL;
          }
        }
        break;
        
      case 'l':
      case 'L':
        if (focus_ == PanelFocus::LOG) {
          focus_ = PanelFocus::LIST;
        } else {
          focus_ = PanelFocus::LOG;
        }
        break;
        
      case KEY_UP:
      case 'k':
        if (focus_ == PanelFocus::LOG) {
          logScroll_ = std::min(logCount - 1, logScroll_ + 1);
        } else if (viewMode_ == ViewMode::HISTORY) {
          histScroll_ = std::min(250, histScroll_ + 1);
        } else {
          if (hostCount > 0) {
            selectedIdx_ = (selectedIdx_ + hostCount - 1) % hostCount;
          }
        }
        break;
        
      case KEY_DOWN:
      case 'j':
        if (focus_ == PanelFocus::LOG) {
          logScroll_ = std::max(0, logScroll_ - 1);
        } else if (viewMode_ == ViewMode::HISTORY) {
          histScroll_ = std::max(0, histScroll_ - 1);
        } else {
          if (hostCount > 0) {
            selectedIdx_ = (selectedIdx_ + 1) % hostCount;
          }
        }
        break;
        
      case KEY_PPAGE: // Page Up
        if (focus_ == PanelFocus::LOG) {
          logScroll_ = std::min(logCount - 1, logScroll_ + 10);
        } else if (viewMode_ == ViewMode::HISTORY) {
          histScroll_ = std::min(250, histScroll_ + 10);
        } else {
          if (hostCount > 0) {
            selectedIdx_ = std::max(0, selectedIdx_ - 10);
          }
        }
        break;
        
      case KEY_NPAGE: // Page Down
        if (focus_ == PanelFocus::LOG) {
          logScroll_ = std::max(0, logScroll_ - 10);
        } else if (viewMode_ == ViewMode::HISTORY) {
          histScroll_ = std::max(0, histScroll_ - 10);
        } else {
          if (hostCount > 0) {
            selectedIdx_ = std::min(hostCount - 1, selectedIdx_ + 10);
          }
        }
        break;
        
      case KEY_RESIZE:
        endwin(); refresh();
        getmaxyx(stdscr, rows_, cols_);
        clear();
        break;
    }
    return false;
  }

  void handleCmdKey(int ch) {
    if (ch == 27) {
      cmdActive_ = false;
      cmdBuf_.clear();
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      execCmd(cmdBuf_);
      cmdActive_ = false;
      cmdBuf_.clear();
      return;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (!cmdBuf_.empty()) {
        cmdBuf_.pop_back();
      }
      return;
    }
    if (ch == 9) { // Tab-complete
      std::vector<std::string> matches;
      for (const auto &h : sortedHosts_) {
        if (h.name.rfind(cmdBuf_, 0) == 0) {
          matches.push_back(h.name);
        }
      }
      if (matches.size() == 1) {
        cmdBuf_ = matches[0];
      }
      return;
    }
    if (ch >= 32 && ch < 127) {
      cmdBuf_ += static_cast<char>(ch);
    }
  }

  void execCmd(const std::string &cmd) {
    if (cmd == "help" || cmd == "/help") {
      viewMode_ = ViewMode::HELP;
      focus_ = PanelFocus::DETAIL;
      return;
    }
    if (cmd == "clear" || cmd == "/clear") {
      logScroll_ = 0;
      return;
    }
    
    std::string clean = cmd;
    if (!clean.empty() && clean[0] == '/') {
      clean = clean.substr(1);
    }
    std::istringstream ss(clean);
    std::string verb, target;
    ss >> verb >> target;
    
    if (verb == "host" || verb == "viewer") {
      int idx = findHost(target);
      if (idx >= 0) {
        selectedIdx_ = idx;
        viewMode_ = ViewMode::DETAIL;
        focus_ = PanelFocus::DETAIL;
      } else {
        cmdError_ = "Host not found: " + target;
        cmdErrorTimer_ = 30;
      }
    } else if (verb == "history") {
      int idx = findHost(target);
      if (idx >= 0) {
        selectedIdx_ = idx;
        historyHost_ = target;
        viewMode_ = ViewMode::HISTORY;
        focus_ = PanelFocus::DETAIL;
        histScroll_ = 0;
      } else {
        cmdError_ = "Host not found: " + target;
        cmdErrorTimer_ = 30;
      }
    } else {
      cmdError_ = "Unknown command: /" + verb + " (try /help)";
      cmdErrorTimer_ = 30;
    }
  }

  int findHost(const std::string &name) {
    for (int i = 0; i < (int)sortedHosts_.size(); i++) {
      if (sortedHosts_[i].name == name) return i;
      if (sortedHosts_[i].name.find(name) != std::string::npos) return i;
    }
    return -1;
  }

  time_t getFirstSeen(const std::string &name) const {
    auto it = firstSeen_.find(name);
    if (it == firstSeen_.end()) {
      time_t now = time(nullptr);
      firstSeen_[name] = now;
      return now;
    }
    return it->second;
  }

  int rows_, cols_;
  int uiMode_;
  int logScroll_;
  int histScroll_;
  int selectedIdx_;
  int listScroll_;
  ViewMode viewMode_;
  PanelFocus focus_;

  bool cmdActive_;
  std::string cmdBuf_;
  std::string cmdError_;
  int cmdErrorTimer_;
  std::string historyHost_;

  time_t startTime_;
  std::vector<HostState> sortedHosts_;
  mutable std::unordered_map<std::string, time_t> firstSeen_;
};

} // namespace monitor::ui
