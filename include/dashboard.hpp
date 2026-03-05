/*
 * dashboard.hpp — btop++-style ncurses dashboard
 *
 * Two view modes:
 *  - OVERVIEW: unified frame with sparkline graphs, host table, connection log
 *  - DETAIL:   per-host resource detail view with individual graphs + history
 *
 * Key bindings:
 *  Tab        → enter detail / next host
 *  Shift+Tab  → previous host
 *  Esc / Backspace → return to overview
 *  Q          → quit
 *  ↑↓ PgUp/Dn → scroll log (overview) or history (detail)
 */
#pragma once
#include "metrics_store.hpp"
#include "thresholds.hpp"
#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#define NCURSES_WIDECHAR 1
#include <ncursesw/curses.h>
#include <string>
#include <vector>

namespace monitor::ui {

// ── View modes ──────────────────────────────────────────────────────────────
enum class ViewMode { OVERVIEW, DETAIL };

enum Color {
  C_NORMAL = 1,
  C_GREEN = 2,
  C_YELLOW = 3,
  C_RED = 4,
  C_GRAY = 5,
  C_HEADER = 6,
  C_BOX = 7,
  C_CYAN = 8,
  C_MAGENTA = 9,
  C_WHITE_BD = 10,
};

static void initColors() {
  start_color();
  use_default_colors();
  init_pair(C_NORMAL, COLOR_WHITE, -1);
  init_pair(C_GREEN, COLOR_GREEN, -1);
  init_pair(C_YELLOW, COLOR_YELLOW, -1);
  init_pair(C_RED, COLOR_RED, -1);
  init_pair(C_GRAY, COLOR_WHITE, -1);
  init_pair(C_HEADER, COLOR_BLACK, COLOR_CYAN);
  init_pair(C_BOX, COLOR_CYAN, -1);
  init_pair(C_CYAN, COLOR_CYAN, -1);
  init_pair(C_MAGENTA, COLOR_MAGENTA, -1);
  init_pair(C_WHITE_BD, COLOR_WHITE, -1);
}

static std::string fmtTime(time_t t) {
  char buf[16];
  struct tm *tm_ = localtime(&t);
  strftime(buf, sizeof(buf), "%H:%M:%S", tm_);
  return buf;
}

static std::string trunc(const std::string &s, int w) {
  if (w <= 0)
    return "";
  if ((int)s.size() <= w) {
    std::string r = s;
    while ((int)r.size() < w)
      r += ' ';
    return r;
  }
  return s.substr(0, w);
}

static int pctColor(float pct, const std::string &host, const Thresholds &th,
                    char metric) {
  float alTh = metric == 'c'   ? th.getCPU(host)
               : metric == 'r' ? th.getRAM(host)
                               : th.getDisk(host);
  if (pct >= alTh)
    return C_RED;
  if (pct >= alTh * 0.80f)
    return C_YELLOW;
  return C_GREEN;
}

// ── Unicode helpers ─────────────────────────────────────────────────────────
static const char *SPARK_CHARS[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
static const char *BLOCK_FULL = "█";
static const char *BLOCK_EMPTY = "░";

static const char *BOX_TL = "╔";
static const char *BOX_TR = "╗";
static const char *BOX_BL = "╚";
static const char *BOX_BR = "╝";
static const char *BOX_H = "═";
static const char *BOX_V = "║";
static const char *BOX_LT = "╠";
static const char *BOX_RT = "╣";

static const char *SYM_ONLINE = "●";
static const char *SYM_WARN = "◐";
static const char *SYM_DIAMOND = "◈";
static const char *LINE_H = "─";

// ── Dashboard ───────────────────────────────────────────────────────────────
class Dashboard {
public:
  Dashboard() = default;
  ~Dashboard() { teardown(); }

  void init() {
    setlocale(LC_ALL, "");
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    keypad(stdscr, TRUE);
    halfdelay(2);
    initColors();
    getmaxyx(stdscr, rows_, cols_);
  }

  void teardown() {
    if (!isendwin())
      endwin();
  }

  void render(const std::vector<HostState> &hosts,
              const std::vector<LogEvent> &log, const Thresholds &thresh) {
    int nr, nc;
    getmaxyx(stdscr, nr, nc);
    if (nr != rows_ || nc != cols_) {
      rows_ = nr;
      cols_ = nc;
      clearok(stdscr, TRUE);
      erase();
      refresh();
    }

    // Build sorted hosts list (shared between modes)
    sortedHosts_ = hosts;
    std::sort(sortedHosts_.begin(), sortedHosts_.end(),
              [](const HostState &a, const HostState &b) {
                bool ao = (a.status != HostStatus::OFFLINE);
                bool bo = (b.status != HostStatus::OFFLINE);
                if (ao != bo)
                  return ao;
                return a.name < b.name;
              });

    handleInput();

    erase();

    if (viewMode_ == ViewMode::DETAIL && !sortedHosts_.empty()) {
      renderDetailView(thresh);
    } else {
      viewMode_ = ViewMode::OVERVIEW;
      renderOverview(log, thresh);
    }

    refresh();
  }

private:
  int rows_ = 24, cols_ = 80;
  int logScroll_ = 0;
  int histScroll_ = 0;

  ViewMode viewMode_ = ViewMode::OVERVIEW;
  int selectedIdx_ = 0;

  std::vector<HostState> sortedHosts_;

  // Layout (overview)
  int hdrY_ = 0, graphY_ = 0, graphH_ = 0;
  int tableY_ = 0, tableH_ = 0;
  int logY_ = 0, logH_ = 0;

  // ── Input handling ──────────────────────────────────────────────────────
  void handleInput() {
    int ch = getch();
    int hostCount = (int)sortedHosts_.size();

    switch (ch) {
    case 'q':
    case 'Q':
      endwin();
      exit(0);

    case '\t': // Tab → next host / enter detail
      if (hostCount > 0) {
        if (viewMode_ == ViewMode::OVERVIEW) {
          viewMode_ = ViewMode::DETAIL;
          histScroll_ = 0;
        } else {
          selectedIdx_ = (selectedIdx_ + 1) % hostCount;
          histScroll_ = 0;
        }
      }
      break;

    case KEY_BTAB: // Shift+Tab → previous host
      if (hostCount > 0) {
        if (viewMode_ == ViewMode::OVERVIEW) {
          viewMode_ = ViewMode::DETAIL;
          selectedIdx_ = hostCount - 1;
          histScroll_ = 0;
        } else {
          selectedIdx_ = (selectedIdx_ - 1 + hostCount) % hostCount;
          histScroll_ = 0;
        }
      }
      break;

    case 27: // Esc
    case KEY_BACKSPACE:
    case 127:
      if (viewMode_ == ViewMode::DETAIL) {
        viewMode_ = ViewMode::OVERVIEW;
      }
      break;

    case KEY_UP:
      if (viewMode_ == ViewMode::DETAIL)
        histScroll_ = std::max(0, histScroll_ - 1);
      else
        logScroll_ = std::max(0, logScroll_ - 1);
      break;
    case KEY_DOWN:
      if (viewMode_ == ViewMode::DETAIL)
        histScroll_++;
      else
        logScroll_++;
      break;
    case KEY_PPAGE:
      if (viewMode_ == ViewMode::DETAIL)
        histScroll_ = std::max(0, histScroll_ - 8);
      else
        logScroll_ = std::max(0, logScroll_ - 8);
      break;
    case KEY_NPAGE:
      if (viewMode_ == ViewMode::DETAIL)
        histScroll_ += 8;
      else
        logScroll_ += 8;
      break;
    default:
      break;
    }

    // Clamp selectedIdx
    if (hostCount > 0)
      selectedIdx_ = std::clamp(selectedIdx_, 0, hostCount - 1);
    else
      selectedIdx_ = 0;
  }

  // ── OVERVIEW MODE ───────────────────────────────────────────────────────
  void renderOverview(const std::vector<LogEvent> &log,
                      const Thresholds &thresh) {
    calcOverviewLayout();
    drawOuterFrame();
    renderHeader("[Tab] Detail [Q] Quit");
    renderGraphs(thresh);
    renderTable(thresh);
    renderLog(log, thresh);
  }

  void calcOverviewLayout() {
    hdrY_ = 0;
    graphY_ = 2;
    graphH_ = 4;
    int graphEnd = graphY_ + 1 + graphH_;

    tableY_ = graphEnd;
    int hostCount = std::max(1, (int)sortedHosts_.size());
    tableH_ = 2 + hostCount;
    if (tableH_ > rows_ / 3)
      tableH_ = rows_ / 3;
    if (tableH_ < 3)
      tableH_ = 3;

    int tableEnd = tableY_ + 1 + tableH_;
    logY_ = tableEnd;
    logH_ = rows_ - logY_ - 1;
    if (logH_ < 2)
      logH_ = 2;
  }

  // ── Outer Frame ─────────────────────────────────────────────────────────
  void drawOuterFrame() {
    attron(COLOR_PAIR(C_BOX) | A_BOLD);

    mvaddstr(0, 0, BOX_TL);
    for (int i = 1; i < cols_ - 1; i++)
      addstr(BOX_H);
    addstr(BOX_TR);

    mvaddstr(rows_ - 1, 0, BOX_BL);
    for (int i = 1; i < cols_ - 1; i++)
      addstr(BOX_H);
    addstr(BOX_BR);

    for (int r = 1; r < rows_ - 1; r++) {
      mvaddstr(r, 0, BOX_V);
      mvaddstr(r, cols_ - 1, BOX_V);
    }

    // Separator after header
    drawHSep(graphY_);
    // Separator after graphs
    drawHSep(tableY_);
    // Separator after table header
    int ths = tableY_ + 2;
    if (ths < rows_ - 1) {
      mvaddstr(ths, 0, BOX_LT);
      for (int i = 1; i < cols_ - 1; i++)
        mvaddch(ths, i, '-');
      mvaddstr(ths, cols_ - 1, BOX_RT);
    }
    // Separator before log
    drawHSep(logY_);

    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
  }

  void drawHSep(int y) {
    if (y <= 0 || y >= rows_ - 1)
      return;
    mvaddstr(y, 0, BOX_LT);
    for (int i = 1; i < cols_ - 1; i++)
      addstr(BOX_H);
    addstr(BOX_RT);
  }

  // ── Header ──────────────────────────────────────────────────────────────
  void renderHeader(const char *hint) {
    int y = 1;
    attron(COLOR_PAIR(C_HEADER) | A_BOLD);
    for (int i = 1; i < cols_ - 1; i++)
      mvaddch(y, i, ' ');

    std::string ts = fmtTime(time(nullptr));
    mvaddstr(y, 2, ts.c_str());

    std::string title =
        std::string(SYM_DIAMOND) + " DISTRIBUTED SYSTEM MONITOR " + SYM_DIAMOND;
    int tx = (cols_ - (int)title.size()) / 2;
    if (tx > 12)
      mvaddstr(y, tx, title.c_str());

    int hintLen = (int)strlen(hint);
    if (cols_ - hintLen - 3 > 0)
      mvaddstr(y, cols_ - hintLen - 2, hint);

    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
  }

  // ── Sparkline Graphs (overview — aggregated) ────────────────────────────
  void renderGraphs(const Thresholds &th) {
    size_t maxLen = 0;
    int online = 0;
    for (auto &h : sortedHosts_)
      if (h.status != HostStatus::OFFLINE) {
        maxLen = std::max(maxLen, h.history.size());
        online++;
      }

    std::vector<float> cv(maxLen, 0), rv(maxLen, 0), dv(maxLen, 0);
    float ac = 0, ar = 0, ad = 0;
    if (online > 0) {
      for (auto &h : sortedHosts_) {
        if (h.status == HostStatus::OFFLINE)
          continue;
        ac += h.cpu;
        ar += h.ram;
        ad += h.disk;
        size_t off = maxLen - h.history.size();
        for (size_t i = 0; i < h.history.size(); i++) {
          cv[off + i] += h.history[i].cpu;
          rv[off + i] += h.history[i].ram;
          dv[off + i] += h.history[i].disk;
        }
      }
      for (auto &v : cv)
        v /= online;
      for (auto &v : rv)
        v /= online;
      for (auto &v : dv)
        v /= online;
      ac /= online;
      ar /= online;
      ad /= online;
    }

    int innerW = cols_ - 2;
    int panelW = innerW / 3;
    int panelWLast = innerW - panelW * 2;
    int startY = graphY_ + 1;

    struct G {
      const char *t;
      std::vector<float> &v;
      float a;
      char m;
      int c;
      int x;
      int w;
    };
    std::vector<G> gs = {
        {"CPU %", cv, ac, 'c', C_GREEN, 1, panelW},
        {"RAM %", rv, ar, 'r', C_CYAN, 1 + panelW, panelW},
        {"DISK %", dv, ad, 'd', C_YELLOW, 1 + panelW * 2, panelWLast},
    };

    for (auto &g : gs) {
      int x0 = g.x, pw = g.w;
      if (x0 > 1) {
        attron(COLOR_PAIR(C_BOX) | A_BOLD);
        for (int r = startY; r < startY + graphH_; r++)
          mvaddstr(r, x0, BOX_V);
        attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      }
      int cx = (x0 > 1) ? x0 + 1 : x0;
      int cw = (x0 > 1) ? pw - 1 : pw;
      drawSparkTitle(startY, cx, cw, x0 + pw, g.t);
      drawSparkline(startY + 1, cx, cw - 1, 2, g.v, g.m, th);
      // Avg value
      char buf[16];
      snprintf(buf, sizeof(buf), "%5.1f%%", g.a);
      int co = pctColor(g.a, "", th, g.m);
      attron(COLOR_PAIR(co) | A_BOLD);
      mvaddstr(startY + graphH_ - 1, x0 + pw - (int)strlen(buf) - 1, buf);
      attroff(COLOR_PAIR(co) | A_BOLD);
    }
  }

  // ── Sparkline drawing helpers ───────────────────────────────────────────
  void drawSparkTitle(int y, int x, int w, int endX, const char *title) {
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    mvaddstr(y, x, LINE_H);
    addstr(" ");
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    addstr(title);
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    addstr(" ");
    int cur = getcurx(stdscr);
    for (int i = cur; i < endX; i++)
      addstr(LINE_H);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
  }

  void drawSparkline(int y, int x, int w, int rows,
                     const std::vector<float> &data, char metric,
                     const Thresholds &th) {
    if (w < 1 || rows < 1)
      return;
    std::vector<float> vals = data;
    while ((int)vals.size() < w)
      vals.insert(vals.begin(), 0.0f);
    if ((int)vals.size() > w)
      vals.erase(vals.begin(), vals.begin() + (int)vals.size() - w);

    for (int row = 0; row < rows; row++) {
      int ry = y + row;
      move(ry, x);
      for (int i = 0; i < w; i++) {
        float pct = std::clamp(vals[i], 0.0f, 100.0f);
        float eff;
        if (rows == 1) {
          eff = pct;
        } else if (row == 0) {
          eff = (pct > 50.0f) ? (pct - 50.0f) * 2.0f : 0.0f;
        } else {
          eff = std::min(pct, 50.0f) * 2.0f;
        }
        int level = std::clamp((int)(eff / 100.0f * 8.0f), 0, 7);
        int co = pctColor(pct, "", th, metric);
        attron(COLOR_PAIR(co));
        if (eff > 0.5f)
          addstr(SPARK_CHARS[level]);
        else
          addch(' ');
        attroff(COLOR_PAIR(co));
      }
    }
  }

  // ── Host Table ──────────────────────────────────────────────────────────
  void renderTable(const Thresholds &th) {
    int innerW = cols_ - 2;
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(tableY_, 3, " HOST TABLE ");
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);

    int cHost = std::min(16, innerW / 6);
    int cBar = std::min(16, (innerW - cHost - 24) / 3);
    if (cBar < 6)
      cBar = 6;
    int cPct = 5, cStatus = 8;

    int hy = tableY_ + 1;
    int x = 2;
    attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
    mvprintw(hy, x, "%-*s", cHost, "HOST");
    x += cHost + 1;
    mvprintw(hy, x, "%-*s", cBar + cPct, "CPU");
    x += cBar + cPct + 1;
    mvprintw(hy, x, "%-*s", cBar + cPct, "RAM");
    x += cBar + cPct + 1;
    mvprintw(hy, x, "%-*s", cBar + cPct, "DISK");
    mvprintw(hy, innerW - cStatus, "STATUS");
    attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);

    int dataY = tableY_ + 3;
    int maxRows = logY_ - dataY;
    if (maxRows < 0)
      maxRows = 0;

    int row = 0;
    for (int idx = 0; idx < (int)sortedHosts_.size() && row < maxRows; idx++) {
      auto &h = sortedHosts_[idx];
      int ry = dataY + row;
      x = 2;
      bool off = (h.status == HostStatus::OFFLINE);

      // Highlight selected host with marker
      if (idx == selectedIdx_ && viewMode_ == ViewMode::OVERVIEW) {
        attron(COLOR_PAIR(C_CYAN) | A_BOLD);
        mvaddstr(ry, 1, ">");
        attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
      }

      attron(COLOR_PAIR(off ? C_GRAY : C_WHITE_BD) | (off ? A_DIM : A_BOLD));
      mvprintw(ry, x, "%-*s", cHost, trunc(h.name, cHost).c_str());
      attroff(COLOR_PAIR(off ? C_GRAY : C_WHITE_BD) | (off ? A_DIM : A_BOLD));
      x += cHost + 1;

      if (off) {
        attron(COLOR_PAIR(C_GRAY) | A_DIM);
        mvprintw(ry, x, "--- OFFLINE ---");
        mvprintw(ry, innerW - cStatus, "OFFLINE");
        attroff(COLOR_PAIR(C_GRAY) | A_DIM);
        row++;
        continue;
      }

      auto drawBar = [&](float val, char m, int col) {
        int co = pctColor(val, h.name, th, m);
        int filled = std::clamp((int)(val / 100.0f * cBar), 0, cBar);
        move(ry, col);
        attron(COLOR_PAIR(co) | A_BOLD);
        for (int i = 0; i < filled; i++)
          addstr(BLOCK_FULL);
        attroff(COLOR_PAIR(co) | A_BOLD);
        attron(COLOR_PAIR(C_GRAY) | A_DIM);
        for (int i = filled; i < cBar; i++)
          addstr(BLOCK_EMPTY);
        attroff(COLOR_PAIR(C_GRAY) | A_DIM);
        attron(COLOR_PAIR(co) | A_BOLD);
        printw("%3.0f%% ", val);
        attroff(COLOR_PAIR(co) | A_BOLD);
      };

      int bx = 2 + cHost + 1;
      drawBar(h.cpu, 'c', bx);
      drawBar(h.ram, 'r', bx + cBar + cPct + 1);
      drawBar(h.disk, 'd', bx + (cBar + cPct + 1) * 2);

      const char *sym;
      const char *label;
      int sc;
      switch (h.status) {
      case HostStatus::ALERT:
        sc = C_RED;
        sym = SYM_ONLINE;
        label = " ALERT";
        break;
      case HostStatus::WARNING:
        sc = C_YELLOW;
        sym = SYM_WARN;
        label = " WARN";
        break;
      default:
        sc = C_GREEN;
        sym = SYM_ONLINE;
        label = " OK";
        break;
      }
      attron(COLOR_PAIR(sc) | A_BOLD);
      mvaddstr(ry, innerW - cStatus, sym);
      addstr(label);
      attroff(COLOR_PAIR(sc) | A_BOLD);
      row++;
    }
  }

  // ── Connection Log ──────────────────────────────────────────────────────
  void renderLog(const std::vector<LogEvent> &log, const Thresholds &th) {
    std::string logTitle = " CONNECTION LOG  [↑↓/PgUp/PgDn to scroll] ";
    int ltx = (cols_ - (int)logTitle.size()) / 2;
    if (ltx < 3)
      ltx = 3;
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(logY_, ltx, logTitle.c_str());
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);

    int contentY = logY_ + 1;
    int innerH = logH_;
    if (innerH <= 0)
      return;

    int maxScroll = std::max(0, (int)log.size() - innerH);
    logScroll_ = std::clamp(logScroll_, 0, maxScroll);
    int startIdx = (int)log.size() - innerH - logScroll_;
    if (startIdx < 0)
      startIdx = 0;

    for (int i = 0; i < innerH; i++) {
      int idx = startIdx + i;
      if (idx < 0 || idx >= (int)log.size())
        continue;
      const auto &ev = log[idx];
      int ry = contentY + i;
      if (ry >= rows_ - 1)
        break;
      int cx = 2;

      int ec;
      const char *es;
      switch (ev.type) {
      case LogEventType::CONNECT:
        ec = C_GREEN;
        es = "CONNECT  ";
        break;
      case LogEventType::METRIC:
        ec = C_CYAN;
        es = "METRIC   ";
        break;
      case LogEventType::ALERT:
        ec = C_RED;
        es = "ALERT    ";
        break;
      case LogEventType::DISCONNECT:
        ec = C_GRAY;
        es = "DISCONN  ";
        break;
      default:
        ec = C_NORMAL;
        es = "UNKNOWN  ";
        break;
      }

      attron(COLOR_PAIR(C_CYAN));
      mvaddstr(ry, cx, fmtTime(ev.ts).c_str());
      cx += 10;
      attroff(COLOR_PAIR(C_CYAN));

      attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      mvprintw(ry, cx, "%-16s", trunc(ev.host, 16).c_str());
      cx += 17;
      attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);

      attron(COLOR_PAIR(C_GRAY) | A_DIM);
      mvprintw(ry, cx, "%-15s", trunc(ev.ip, 15).c_str());
      cx += 16;
      attroff(COLOR_PAIR(C_GRAY) | A_DIM);

      attron(COLOR_PAIR(ec) | A_BOLD);
      mvaddstr(ry, cx, es);
      cx += 9;
      attroff(COLOR_PAIR(ec) | A_BOLD);

      if ((ev.type == LogEventType::METRIC || ev.type == LogEventType::ALERT) &&
          cx + 24 < cols_ - 1) {
        auto pm = [&](float val, char m, const char *lbl) {
          attron(COLOR_PAIR(pctColor(val, ev.host, th, m)));
          char mb[16];
          snprintf(mb, sizeof(mb), "%s%3.0f%%", lbl, val);
          mvaddstr(ry, cx, mb);
          cx += 8;
          attroff(COLOR_PAIR(pctColor(val, ev.host, th, m)));
        };
        pm(ev.cpu, 'c', "C:");
        pm(ev.ram, 'r', "R:");
        pm(ev.disk, 'd', "D:");
        if (!ev.detail.empty() && cx + 3 < cols_ - 1) {
          attron(COLOR_PAIR(C_RED) | A_BOLD);
          mvaddstr(ry, cx, ("!" + ev.detail.substr(0, cols_ - cx - 3)).c_str());
          attroff(COLOR_PAIR(C_RED) | A_BOLD);
        }
      }
    }

    char si[32];
    snprintf(si, sizeof(si), " %d/%d ", (int)log.size(), MAX_LOG_ENTRIES);
    attron(COLOR_PAIR(C_GRAY) | A_DIM);
    mvaddstr(rows_ - 1, cols_ - (int)strlen(si) - 2, si);
    attroff(COLOR_PAIR(C_GRAY) | A_DIM);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // ── DETAIL VIEW MODE ─────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════
  void renderDetailView(const Thresholds &th) {
    if (selectedIdx_ >= (int)sortedHosts_.size())
      return;
    const auto &host = sortedHosts_[selectedIdx_];
    int hostCount = (int)sortedHosts_.size();

    // Draw full-screen frame
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    mvaddstr(0, 0, BOX_TL);
    for (int i = 1; i < cols_ - 1; i++)
      addstr(BOX_H);
    addstr(BOX_TR);
    mvaddstr(rows_ - 1, 0, BOX_BL);
    for (int i = 1; i < cols_ - 1; i++)
      addstr(BOX_H);
    addstr(BOX_BR);
    for (int r = 1; r < rows_ - 1; r++) {
      mvaddstr(r, 0, BOX_V);
      mvaddstr(r, cols_ - 1, BOX_V);
    }
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    // Header
    int y = 1;
    attron(COLOR_PAIR(C_HEADER) | A_BOLD);
    for (int i = 1; i < cols_ - 1; i++)
      mvaddch(y, i, ' ');
    std::string ts = fmtTime(time(nullptr));
    mvaddstr(y, 2, ts.c_str());

    std::string dtitle = std::string(SYM_DIAMOND) +
                         " HOST DETAIL: " + host.name + " " + SYM_DIAMOND;
    int dtx = (cols_ - (int)dtitle.size()) / 2;
    if (dtx > 12)
      mvaddstr(y, dtx, dtitle.c_str());

    const char *dhint = "[Tab]Next [Esc]Back [Q]Quit";
    int dhLen = (int)strlen(dhint);
    if (cols_ - dhLen - 3 > 0)
      mvaddstr(y, cols_ - dhLen - 2, dhint);
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);

    // Separator after header
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    drawHSep(2);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    // Check offline
    if (host.status == HostStatus::OFFLINE) {
      attron(COLOR_PAIR(C_GRAY) | A_DIM);
      std::string offMsg = "--- " + host.name + " is OFFLINE ---";
      mvaddstr(rows_ / 2, (cols_ - (int)offMsg.size()) / 2, offMsg.c_str());
      attroff(COLOR_PAIR(C_GRAY) | A_DIM);
      renderDetailFooter(hostCount);
      return;
    }

    // Collect per-host history
    std::vector<float> cpuH, ramH, diskH;
    for (auto &s : host.history) {
      cpuH.push_back(s.cpu);
      ramH.push_back(s.ram);
      diskH.push_back(s.disk);
    }

    // Layout starts after header separator (row 0=top, 1=header, 2=sep)
    // row 0: top border, row 1: header, row 2: sep
    // then graphs, then seps, info, history

    int curY = 3; // start after header separator

    // ── CPU graph ──
    int graphW = cols_ - 4;
    if (graphW < 10)
      graphW = 10;

    struct MetricInfo {
      const char *name;
      std::vector<float> &hist;
      float current;
      char metric;
      int color;
    };
    std::vector<MetricInfo> metrics = {
        {"CPU", cpuH, host.cpu, 'c', C_GREEN},
        {"RAM", ramH, host.ram, 'r', C_CYAN},
        {"DISK", diskH, host.disk, 'd', C_YELLOW},
    };

    for (auto &m : metrics) {
      if (curY >= rows_ - 2)
        break;

      // Separator line
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);

      // Title with percentage: ─ CPU ──────────── 87.3% ──
      curY++;
      if (curY >= rows_ - 2)
        break;
      {
        attron(COLOR_PAIR(C_BOX) | A_BOLD);
        mvaddstr(curY, 2, LINE_H);
        addstr(" ");
        attroff(COLOR_PAIR(C_BOX) | A_BOLD);

        attron(COLOR_PAIR(C_CYAN) | A_BOLD);
        addstr(m.name);
        attroff(COLOR_PAIR(C_CYAN) | A_BOLD);

        attron(COLOR_PAIR(C_BOX) | A_BOLD);
        addstr(" ");
        int cur = getcurx(stdscr);
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), " %5.1f%% ", m.current);
        int pctPos = cols_ - (int)strlen(pctBuf) - 3;
        for (int i = cur; i < pctPos; i++)
          addstr(LINE_H);
        attroff(COLOR_PAIR(C_BOX) | A_BOLD);

        int co = pctColor(m.current, host.name, th, m.metric);
        attron(COLOR_PAIR(co) | A_BOLD);
        addstr(pctBuf);
        attroff(COLOR_PAIR(co) | A_BOLD);

        attron(COLOR_PAIR(C_BOX) | A_BOLD);
        addstr(LINE_H);
        attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      }

      // Sparkline (2 rows)
      curY++;
      int sparkRows = 2;
      if (curY + sparkRows >= rows_ - 2)
        sparkRows = rows_ - 2 - curY;
      if (sparkRows > 0)
        drawSparkline(curY, 2, graphW, sparkRows, m.hist, m.metric, th);
      curY += sparkRows;
    }

    // ── Host Info Section ──
    if (curY < rows_ - 6) {
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      curY++;

      // HOST INFO title
      attron(COLOR_PAIR(C_CYAN) | A_BOLD);
      mvaddstr(curY - 1, 3, " HOST INFO ");
      attroff(COLOR_PAIR(C_CYAN) | A_BOLD);

      int leftCol = 2, rightCol = cols_ / 2;

      // Row 1: Name + Status
      attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      mvprintw(curY, leftCol, "Name:    ");
      attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      attron(COLOR_PAIR(C_CYAN));
      addstr(host.name.c_str());
      attroff(COLOR_PAIR(C_CYAN));

      const char *sym;
      const char *slbl;
      int sc;
      switch (host.status) {
      case HostStatus::ALERT:
        sc = C_RED;
        sym = SYM_ONLINE;
        slbl = " ALERT";
        break;
      case HostStatus::WARNING:
        sc = C_YELLOW;
        sym = SYM_WARN;
        slbl = " WARN";
        break;
      default:
        sc = C_GREEN;
        sym = SYM_ONLINE;
        slbl = " OK";
        break;
      }
      attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      mvprintw(curY, rightCol, "Status:   ");
      attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      attron(COLOR_PAIR(sc) | A_BOLD);
      addstr(sym);
      addstr(slbl);
      attroff(COLOR_PAIR(sc) | A_BOLD);
      curY++;

      // Row 2: IP + Last Seen
      if (curY < rows_ - 2) {
        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, leftCol, "IP:      ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_GRAY));
        addstr(host.ip.c_str());
        attroff(COLOR_PAIR(C_GRAY));

        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, rightCol, "Last Seen: ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_CYAN));
        addstr(fmtTime(host.lastSeen).c_str());
        attroff(COLOR_PAIR(C_CYAN));
        curY++;
      }

      // Row 3: Thresholds
      if (curY < rows_ - 2) {
        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, leftCol, "Thresholds: ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_YELLOW));
        char thBuf[64];
        snprintf(thBuf, sizeof(thBuf), "CPU≥%.0f%%  RAM≥%.0f%%  DISK≥%.0f%%",
                 th.getCPU(host.name), th.getRAM(host.name),
                 th.getDisk(host.name));
        addstr(thBuf);
        attroff(COLOR_PAIR(C_YELLOW));
        curY++;
      }
    }

    // ── Recent History Table ──
    if (curY < rows_ - 4) {
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);

      // Title with host counter
      char histTitle[64];
      snprintf(histTitle, sizeof(histTitle), " RECENT HISTORY [%d/%d hosts] ",
               selectedIdx_ + 1, hostCount);
      attron(COLOR_PAIR(C_CYAN) | A_BOLD);
      mvaddstr(curY, 3, histTitle);
      attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
      curY++;

      // Header
      attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      mvprintw(curY, 3, "%-10s %8s %8s %8s", "TIME", "CPU%", "RAM%", "DISK%");
      attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
      curY++;

      // Separator
      attron(COLOR_PAIR(C_BOX));
      mvaddstr(curY, 0, BOX_LT);
      for (int i = 1; i < cols_ - 1; i++)
        mvaddch(curY, i, '-');
      mvaddstr(curY, cols_ - 1, BOX_RT);
      attroff(COLOR_PAIR(C_BOX));
      curY++;

      // History data (newest first)
      int availRows = rows_ - curY - 1;
      if (availRows < 1)
        availRows = 1;
      int histSize = (int)host.history.size();
      int maxHistScroll = std::max(0, histSize - availRows);
      histScroll_ = std::clamp(histScroll_, 0, maxHistScroll);

      int startI = histSize - 1 - histScroll_;
      for (int r = 0; r < availRows && startI - r >= 0; r++) {
        int ri = startI - r;
        int ry = curY + r;
        if (ry >= rows_ - 1)
          break;
        const auto &s = host.history[ri];

        attron(COLOR_PAIR(C_CYAN));
        mvprintw(ry, 3, "%-10s", fmtTime(s.ts).c_str());
        attroff(COLOR_PAIR(C_CYAN));

        int cc = pctColor(s.cpu, host.name, th, 'c');
        attron(COLOR_PAIR(cc) | A_BOLD);
        printw(" %7.1f%%", s.cpu);
        attroff(COLOR_PAIR(cc) | A_BOLD);

        int cr = pctColor(s.ram, host.name, th, 'r');
        attron(COLOR_PAIR(cr) | A_BOLD);
        printw(" %7.1f%%", s.ram);
        attroff(COLOR_PAIR(cr) | A_BOLD);

        int cd = pctColor(s.disk, host.name, th, 'd');
        attron(COLOR_PAIR(cd) | A_BOLD);
        printw(" %7.1f%%", s.disk);
        attroff(COLOR_PAIR(cd) | A_BOLD);
      }
    }

    renderDetailFooter(hostCount);
  }

  void renderDetailFooter(int hostCount) {
    // Host counter on bottom border
    char fi[32];
    snprintf(fi, sizeof(fi), " %d/%d ", selectedIdx_ + 1, hostCount);
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(rows_ - 1, cols_ - (int)strlen(fi) - 2, fi);
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
  }
};

} // namespace monitor::ui
