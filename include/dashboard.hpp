/*
 * dashboard.hpp — btop++-style ncurses dashboard
 *
 * Two view modes:
 *  - OVERVIEW: unified frame with sparkline graphs, host table, connection log
 *  - DETAIL:   per-host resource detail with per-core CPU bars + RAM/DISK
 * graphs
 *
 * Key bindings:
 *  Tab        → enter detail / next host
 *  Shift+Tab  → previous host
 *  Esc / Backspace → return to overview
 *  Q          → quit
 *  ↑↓ PgUp/Dn → scroll log (overview) or cores (detail)
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
enum class ViewMode { OVERVIEW, DETAIL, HELP, HISTORY };

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
  C_RAIN1 = 11,
  C_RAIN2 = 12,
  C_RAIN3 = 13,
  C_RAIN4 = 14,
  C_RAIN5 = 15,
  C_RAIN6 = 16,
};

static void initColors(int themeMode = 0) {
  start_color();
  use_default_colors();

  // Base fallback
  init_pair(C_NORMAL, COLOR_WHITE, -1);
  init_pair(C_GREEN, COLOR_GREEN, -1);
  init_pair(C_YELLOW, COLOR_YELLOW, -1);
  init_pair(C_RED, COLOR_RED, -1);
  init_pair(C_GRAY, COLOR_WHITE, -1);
  init_pair(C_HEADER, COLOR_BLACK, COLOR_MAGENTA);
  init_pair(C_BOX, COLOR_YELLOW, -1);
  init_pair(C_CYAN, COLOR_BLUE, -1);
  init_pair(C_MAGENTA, COLOR_MAGENTA, -1);
  init_pair(C_WHITE_BD, COLOR_WHITE, -1);
  init_pair(C_RAIN1, COLOR_RED, -1);
  init_pair(C_RAIN2, COLOR_YELLOW, -1);
  init_pair(C_RAIN3, COLOR_GREEN, -1);
  init_pair(C_RAIN4, COLOR_CYAN, -1);
  init_pair(C_RAIN5, COLOR_BLUE, -1);
  init_pair(C_RAIN6, COLOR_MAGENTA, -1);

  if (COLORS < 256)
    return;

  // Theme 0: Matrix Hacker (default)
  if (themeMode == 0) {
    init_pair(C_HEADER, 16, 46);
    init_pair(C_BOX, 46, -1);
    init_pair(C_CYAN, 118, -1);
    init_pair(C_MAGENTA, 196, -1);
    init_pair(C_WHITE_BD, 231, -1);
    init_pair(C_GRAY, 245, -1);
    init_pair(C_GREEN, 118, -1);
    init_pair(C_YELLOW, 220, -1);
    init_pair(C_RED, 203, -1);
    init_pair(C_RAIN1, 196, -1);
    init_pair(C_RAIN2, 208, -1);
    init_pair(C_RAIN3, 226, -1);
    init_pair(C_RAIN4, 82, -1);
    init_pair(C_RAIN5, 45, -1);
    init_pair(C_RAIN6, 201, -1);
    return;
  }

  // Theme 1: Neon Cyberpunk
  if (themeMode == 1) {
    init_pair(C_HEADER, 16, 201);
    init_pair(C_BOX, 51, -1);
    init_pair(C_CYAN, 51, -1);
    init_pair(C_MAGENTA, 201, -1);
    init_pair(C_WHITE_BD, 230, -1);
    init_pair(C_GRAY, 244, -1);
    init_pair(C_GREEN, 118, -1);
    init_pair(C_YELLOW, 220, -1);
    init_pair(C_RED, 197, -1);
    init_pair(C_RAIN1, 196, -1);
    init_pair(C_RAIN2, 208, -1);
    init_pair(C_RAIN3, 226, -1);
    init_pair(C_RAIN4, 46, -1);
    init_pair(C_RAIN5, 51, -1);
    init_pair(C_RAIN6, 201, -1);
    return;
  }

  // Theme 2: Solar Amber
  init_pair(C_HEADER, 16, 220);
  init_pair(C_BOX, 46, -1);
  init_pair(C_CYAN, 178, -1);
  init_pair(C_MAGENTA, 208, -1);
  init_pair(C_WHITE_BD, 230, -1);
  init_pair(C_GRAY, 246, -1);
  init_pair(C_GREEN, 82, -1);
  init_pair(C_YELLOW, 220, -1);
  init_pair(C_RED, 203, -1);
  init_pair(C_RAIN1, 196, -1);
  init_pair(C_RAIN2, 202, -1);
  init_pair(C_RAIN3, 226, -1);
  init_pair(C_RAIN4, 118, -1);
  init_pair(C_RAIN5, 45, -1);
  init_pair(C_RAIN6, 201, -1);
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
    initColors(themeMode_);
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

    if (cmdErrorTimer_ > 0)
      cmdErrorTimer_--;

    if (viewMode_ == ViewMode::HELP)
      renderHelp();
    else if (viewMode_ == ViewMode::HISTORY && !sortedHosts_.empty())
      renderHistoryView();
    else if (viewMode_ == ViewMode::DETAIL && !sortedHosts_.empty())
      renderDetailView(thresh);
    else {
      viewMode_ = ViewMode::OVERVIEW;
      renderOverview(log, thresh);
    }

    // Command bar overlay (bottom)
    if (cmdMode_)
      renderCmdBar();
    // Error message overlay
    else if (cmdErrorTimer_ > 0 && !cmdError_.empty())
      renderCmdError();

    refresh();
  }

private:
  int rows_ = 24, cols_ = 80;
  int logScroll_ = 0, histScroll_ = 0;
  int themeMode_ = 0; // 0=vivid, 1=neon, 2=amber
  ViewMode viewMode_ = ViewMode::OVERVIEW;
  ViewMode prevMode_ = ViewMode::OVERVIEW;
  int selectedIdx_ = 0;
  std::vector<HostState> sortedHosts_;
  int hdrY_ = 0, graphY_ = 0, graphH_ = 0, tableY_ = 0, tableH_ = 0, logY_ = 0,
      logH_ = 0;

  // Command input state
  bool cmdMode_ = false;
  std::string cmdBuf_;
  std::string cmdError_;
  int cmdErrorTimer_ = 0;
  std::string historyHost_; // for /history view

  // ── Input ─────────────────────────────────────────────────────────────────
  void handleInput() {
    int ch = getch();
    if (ch == ERR)
      return;
    int hostCount = (int)sortedHosts_.size();

    // ── Command input mode ──
    if (cmdMode_) {
      if (ch == 27) {
        cmdMode_ = false;
        cmdBuf_.clear();
        halfdelay(2);
      } else if (ch == '\n' || ch == KEY_ENTER) {
        executeCmd();
        cmdMode_ = false;
        halfdelay(2);
      } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (!cmdBuf_.empty())
          cmdBuf_.pop_back();
      } else if (ch >= 32 && ch < 127) {
        cmdBuf_ += (char)ch;
      }
      return;
    }

    // ── Normal mode ──
    switch (ch) {
    case 't':
    case 'T':
    case 20: // Ctrl+T
      themeMode_ = (themeMode_ + 1) % 3;
      initColors(themeMode_);
      break;
    case 'q':
    case 'Q':
      endwin();
      exit(0);
    case '/':
      cmdMode_ = true;
      cmdBuf_.clear();
      cmdError_.clear();
      cbreak(); // responsive typing
      break;
    case '\t':
      if (hostCount > 0) {
        if (viewMode_ == ViewMode::OVERVIEW) {
          viewMode_ = ViewMode::DETAIL;
          histScroll_ = 0;
        } else if (viewMode_ == ViewMode::DETAIL) {
          selectedIdx_ = (selectedIdx_ + 1) % hostCount;
          histScroll_ = 0;
        }
      }
      break;
    case KEY_BTAB:
      if (hostCount > 0) {
        if (viewMode_ == ViewMode::OVERVIEW) {
          viewMode_ = ViewMode::DETAIL;
          selectedIdx_ = hostCount - 1;
          histScroll_ = 0;
        } else if (viewMode_ == ViewMode::DETAIL) {
          selectedIdx_ = (selectedIdx_ - 1 + hostCount) % hostCount;
          histScroll_ = 0;
        }
      }
      break;
    case 27:
    case KEY_BACKSPACE:
    case 127:
      if (viewMode_ == ViewMode::HELP || viewMode_ == ViewMode::HISTORY)
        viewMode_ = prevMode_;
      else if (viewMode_ == ViewMode::DETAIL)
        viewMode_ = ViewMode::OVERVIEW;
      break;
    case KEY_UP:
      if (viewMode_ == ViewMode::DETAIL || viewMode_ == ViewMode::HISTORY)
        histScroll_ = std::max(0, histScroll_ - 1);
      else
        logScroll_ = std::max(0, logScroll_ - 1);
      break;
    case KEY_DOWN:
      if (viewMode_ == ViewMode::DETAIL || viewMode_ == ViewMode::HISTORY)
        histScroll_++;
      else
        logScroll_++;
      break;
    case KEY_PPAGE:
      if (viewMode_ == ViewMode::DETAIL || viewMode_ == ViewMode::HISTORY)
        histScroll_ = std::max(0, histScroll_ - 8);
      else
        logScroll_ = std::max(0, logScroll_ - 8);
      break;
    case KEY_NPAGE:
      if (viewMode_ == ViewMode::DETAIL || viewMode_ == ViewMode::HISTORY)
        histScroll_ += 8;
      else
        logScroll_ += 8;
      break;
    default:
      break;
    }
    if (hostCount > 0)
      selectedIdx_ = std::clamp(selectedIdx_, 0, hostCount - 1);
    else
      selectedIdx_ = 0;
  }

  // ── Command execution ─────────────────────────────────────────────────────
  void executeCmd() {
    std::string cmd = cmdBuf_;
    cmdBuf_.clear();
    // trim leading/trailing spaces
    while (!cmd.empty() && cmd.front() == ' ')
      cmd.erase(cmd.begin());
    while (!cmd.empty() && cmd.back() == ' ')
      cmd.pop_back();
    if (cmd.empty())
      return;

    // Parse command and args
    std::vector<std::string> parts;
    std::string cur;
    for (char c : cmd) {
      if (c == ' ') {
        if (!cur.empty()) {
          parts.push_back(cur);
          cur.clear();
        }
      } else
        cur += c;
    }
    if (!cur.empty())
      parts.push_back(cur);
    if (parts.empty())
      return;

    std::string verb = parts[0];
    // normalize: remove leading /
    if (!verb.empty() && verb[0] == '/')
      verb = verb.substr(1);

    if (verb == "help" || verb == "h") {
      prevMode_ = viewMode_;
      viewMode_ = ViewMode::HELP;
    } else if (verb == "viewer" || verb == "view" || verb == "v") {
      if (parts.size() < 2) {
        cmdError_ = "Usage: /viewer <host>";
        cmdErrorTimer_ = 20;
        return;
      }
      std::string target = parts[1];
      int idx = findHost(target);
      if (idx < 0) {
        cmdError_ = "Host not found: " + target;
        cmdErrorTimer_ = 20;
        return;
      }
      selectedIdx_ = idx;
      viewMode_ = ViewMode::DETAIL;
      histScroll_ = 0;
    } else if (verb == "history" || verb == "hist") {
      if (parts.size() < 2) {
        cmdError_ = "Usage: /history <host>";
        cmdErrorTimer_ = 20;
        return;
      }
      std::string target = parts[1];
      int idx = findHost(target);
      if (idx < 0) {
        cmdError_ = "Host not found: " + target;
        cmdErrorTimer_ = 20;
        return;
      }
      selectedIdx_ = idx;
      historyHost_ = target;
      prevMode_ = viewMode_;
      viewMode_ = ViewMode::HISTORY;
      histScroll_ = 0;
    } else {
      cmdError_ = "Unknown command: /" + verb + "  (try /help)";
      cmdErrorTimer_ = 20;
    }
  }

  int findHost(const std::string &name) {
    for (int i = 0; i < (int)sortedHosts_.size(); i++) {
      if (sortedHosts_[i].name == name)
        return i;
      // partial match
      if (sortedHosts_[i].name.find(name) != std::string::npos)
        return i;
    }
    return -1;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // ── OVERVIEW MODE ─────────────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════════
  void renderOverview(const std::vector<LogEvent> &log,
                      const Thresholds &thresh) {
    calcOverviewLayout();
    drawOuterFrame();
    renderHeader("[Tab] Detail [T] Theme [Q] Quit");
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
    drawHSep(graphY_);
    drawHSep(tableY_);
    int ths = tableY_ + 2;
    if (ths < rows_ - 1) {
      mvaddstr(ths, 0, BOX_LT);
      for (int i = 1; i < cols_ - 1; i++)
        mvaddch(ths, i, '-');
      mvaddstr(ths, cols_ - 1, BOX_RT);
    }
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
    if (tx > 12) {
      const int rainbow[] = {C_RAIN1, C_RAIN2, C_RAIN3, C_RAIN4, C_RAIN5, C_RAIN6};
      for (size_t i = 0; i < title.size(); i++) {
        attron(COLOR_PAIR(rainbow[i % 6]) | A_BOLD);
        mvaddch(y, tx + (int)i, title[i]);
        attroff(COLOR_PAIR(rainbow[i % 6]) | A_BOLD);
      }
    }
    int hintLen = (int)strlen(hint);
    if (cols_ - hintLen - 3 > 0)
      mvaddstr(y, cols_ - hintLen - 2, hint);
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
  }

  // ── Sparkline Graphs (aggregated) ─────────────────────────────────────────
  void renderGraphs(const Thresholds &th) {
    int innerW = cols_ - 2, panelW = innerW / 3,
        panelWLast = innerW - panelW * 2;
    int startY = graphY_ + 1;

    int online = 0, offline = 0, warn = 0, alert = 0;
    float sumCpu = 0, sumRam = 0, sumDisk = 0;
    std::string hotCpu = "-", hotRam = "-", hotDisk = "-";
    float maxCpu = -1, maxRam = -1, maxDisk = -1;

    for (const auto &h : sortedHosts_) {
      if (h.status == HostStatus::OFFLINE) {
        offline++;
        continue;
      }
      online++;
      sumCpu += h.cpu;
      sumRam += h.ram;
      sumDisk += h.disk;
      if (h.status == HostStatus::WARNING)
        warn++;
      if (h.status == HostStatus::ALERT)
        alert++;
      if (h.cpu > maxCpu) { maxCpu = h.cpu; hotCpu = h.name; }
      if (h.ram > maxRam) { maxRam = h.ram; hotRam = h.name; }
      if (h.disk > maxDisk){ maxDisk = h.disk; hotDisk = h.name; }
    }

    float avgCpu = online ? sumCpu / online : 0;
    float avgRam = online ? sumRam / online : 0;
    float avgDisk = online ? sumDisk / online : 0;
    int threat = std::min(100, alert * 30 + warn * 10 + (int)(avgCpu * 0.2f));

    auto drawPanel = [&](int x0, int pw, const char *title) {
      if (x0 > 1) {
        attron(COLOR_PAIR(C_BOX) | A_BOLD);
        for (int r = startY; r < startY + graphH_; r++)
          mvaddstr(r, x0, BOX_V);
        attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      }
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      mvaddstr(startY, x0 + (x0 > 1 ? 1 : 0), LINE_H);
      addstr(" ");
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      attron(COLOR_PAIR(C_MAGENTA) | A_BOLD);
      addstr(title);
      attroff(COLOR_PAIR(C_MAGENTA) | A_BOLD);
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      addstr(" ");
      int cur = getcurx(stdscr);
      for (int i = cur; i < x0 + pw; i++)
        addstr(LINE_H);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
    };

    // Panel 1: threat score
    int x1 = 1, w1 = panelW;
    drawPanel(x1, w1, "TACTICAL RISK");
    int cRisk = threat >= 70 ? C_RED : threat >= 35 ? C_YELLOW : C_GREEN;
    attron(COLOR_PAIR(cRisk) | A_BOLD);
    mvprintw(startY + 1, x1 + 2, "THREAT: %3d%%", threat);
    attroff(COLOR_PAIR(cRisk) | A_BOLD);
    attron(COLOR_PAIR(C_CYAN));
    mvprintw(startY + 2, x1 + 2, "ALERT:%d  WARN:%d", alert, warn);
    mvprintw(startY + 3, x1 + 2, "ONLINE:%d OFF:%d", online, offline);
    attroff(COLOR_PAIR(C_CYAN));

    // Panel 2: hot targets
    int x2 = 1 + panelW, w2 = panelW;
    drawPanel(x2, w2, "HOT TARGETS");
    attron(COLOR_PAIR(C_RED) | A_BOLD);
    mvprintw(startY + 1, x2 + 2, "CPU : %s %4.1f%%", trunc(hotCpu, 10).c_str(), std::max(0.f,maxCpu));
    attroff(COLOR_PAIR(C_RED) | A_BOLD);
    attron(COLOR_PAIR(C_YELLOW) | A_BOLD);
    mvprintw(startY + 2, x2 + 2, "RAM : %s %4.1f%%", trunc(hotRam, 10).c_str(), std::max(0.f,maxRam));
    attroff(COLOR_PAIR(C_YELLOW) | A_BOLD);
    attron(COLOR_PAIR(C_MAGENTA) | A_BOLD);
    mvprintw(startY + 3, x2 + 2, "DISK: %s %4.1f%%", trunc(hotDisk, 10).c_str(), std::max(0.f,maxDisk));
    attroff(COLOR_PAIR(C_MAGENTA) | A_BOLD);

    // Panel 3: fleet health
    int x3 = 1 + panelW * 2, w3 = panelWLast;
    drawPanel(x3, w3, "FLEET HEALTH");
    attron(COLOR_PAIR(pctColor(avgCpu, "", th, 'c')) | A_BOLD);
    mvprintw(startY + 1, x3 + 2, "AVG CPU : %5.1f%%", avgCpu);
    attroff(COLOR_PAIR(pctColor(avgCpu, "", th, 'c')) | A_BOLD);
    attron(COLOR_PAIR(pctColor(avgRam, "", th, 'r')) | A_BOLD);
    mvprintw(startY + 2, x3 + 2, "AVG RAM : %5.1f%%", avgRam);
    attroff(COLOR_PAIR(pctColor(avgRam, "", th, 'r')) | A_BOLD);
    attron(COLOR_PAIR(pctColor(avgDisk, "", th, 'd')) | A_BOLD);
    mvprintw(startY + 3, x3 + 2, "AVG DISK: %5.1f%%", avgDisk);
    attroff(COLOR_PAIR(pctColor(avgDisk, "", th, 'd')) | A_BOLD);
  }

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
        if (rows == 1)
          eff = pct;
        else if (row == 0)
          eff = (pct > 50.0f) ? (pct - 50.0f) * 2.0f : 0.0f;
        else
          eff = std::min(pct, 50.0f) * 2.0f;
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

  // ── Scrollbar helper ──────────────────────────────────────────────────────
  // Draws a vertical scrollbar on column `x`, from row `y0` to `y1`.
  // `scroll` = current scroll offset, `visible` = visible rows, `total` = total
  // items.
  void drawScrollbar(int x, int y0, int y1, int scroll, int visible,
                     int total) {
    int trackH = y1 - y0 + 1;
    if (trackH < 2 || total <= visible)
      return;

    // Thumb size and position
    int thumbH = std::max(1, trackH * visible / total);
    int thumbPos = (total - visible > 0)
                       ? (trackH - thumbH) * scroll / (total - visible)
                       : 0;
    thumbPos = std::clamp(thumbPos, 0, trackH - thumbH);

    for (int i = 0; i < trackH; i++) {
      int ry = y0 + i;
      bool isThumb = (i >= thumbPos && i < thumbPos + thumbH);
      if (isThumb) {
        attron(COLOR_PAIR(C_CYAN) | A_BOLD);
        mvaddstr(ry, x, "┃");
        attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
      } else {
        attron(COLOR_PAIR(C_GRAY) | A_DIM);
        mvaddstr(ry, x, "│");
        attroff(COLOR_PAIR(C_GRAY) | A_DIM);
      }
    }
  }

  // ── Host Table ────────────────────────────────────────────────────────────
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

    int hy = tableY_ + 1, x = 2;
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

    int dataY = tableY_ + 3, maxRows = logY_ - dataY;
    if (maxRows < 0)
      maxRows = 0;

    int row = 0;
    for (int idx = 0; idx < (int)sortedHosts_.size() && row < maxRows; idx++) {
      auto &h = sortedHosts_[idx];
      int ry = dataY + row;
      x = 2;
      bool off = (h.status == HostStatus::OFFLINE);


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

      const char *sym, *label;
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

  // ── Connection Log ────────────────────────────────────────────────────────
  void renderLog(const std::vector<LogEvent> &log, const Thresholds &th) {
    std::string logTitle = " CONNECTION LOG  [↑↓/PgUp/PgDn to scroll] ";
    int ltx = (cols_ - (int)logTitle.size()) / 2;
    if (ltx < 3)
      ltx = 3;
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(logY_, ltx, logTitle.c_str());
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);

    int contentY = logY_ + 1, innerH = logH_;
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

    // Scrollbar on right edge of log panel
    if ((int)log.size() > innerH) {
      drawScrollbar(cols_ - 2, contentY, contentY + innerH - 1, logScroll_,
                    innerH, (int)log.size());
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // ── DETAIL VIEW MODE ──────────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════════
  void renderDetailView(const Thresholds &th) {
    if (selectedIdx_ >= (int)sortedHosts_.size())
      return;
    const auto &host = sortedHosts_[selectedIdx_];
    int hostCount = (int)sortedHosts_.size();

    // Full-screen frame
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
    const char *dhint = "[Tab]Next [T]Theme [Esc]Back [Q]Quit";
    int dhLen = (int)strlen(dhint);
    if (cols_ - dhLen - 3 > 0)
      mvaddstr(y, cols_ - dhLen - 2, dhint);
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);

    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    drawHSep(2);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    // Offline
    if (host.status == HostStatus::OFFLINE) {
      attron(COLOR_PAIR(C_GRAY) | A_DIM);
      std::string offMsg = "--- " + host.name + " is OFFLINE ---";
      mvaddstr(rows_ / 2, (cols_ - (int)offMsg.size()) / 2, offMsg.c_str());
      attroff(COLOR_PAIR(C_GRAY) | A_DIM);
      renderDetailFooter(hostCount);
      return;
    }

    int curY = 3;
    int graphW = cols_ - 4;
    if (graphW < 10)
      graphW = 10;

    // ── CPU OVERVIEW sparkline ──
    {
      std::vector<float> cpuH;
      for (auto &s : host.history)
        cpuH.push_back(s.cpu);

      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      curY++;
      if (curY < rows_ - 2) {
        drawMetricTitle(curY, "CPU OVERVIEW", host.cpu, host.name, th, 'c');
        curY++;
      }
      if (curY < rows_ - 2) {
        drawSparkline(curY, 2, graphW, 1, cpuH, 'c', th);
        curY++;
      }
    }

    // ── PROCESSORS (per-core CPU bars) ──
    if (curY < rows_ - 2 && !host.cores.empty()) {
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);

      char coreTitle[48];
      snprintf(coreTitle, sizeof(coreTitle), " PROCESSORS (%d cores) ",
               (int)host.cores.size());
      attron(COLOR_PAIR(C_CYAN) | A_BOLD);
      mvaddstr(curY, 3, coreTitle);
      attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
      curY++;

      int barMaxW = cols_ - 24;
      if (barMaxW < 10)
        barMaxW = 10;

      // Reserve space for RAM + DISK + INFO below
      int reserveBelow = 9; // RAM(3) + DISK(3) + INFO(3)
      int availCoreRows = rows_ - curY - reserveBelow - 1;
      if (availCoreRows < 2)
        availCoreRows = 2;

      int totalCores = (int)host.cores.size();
      int maxCoreScroll = std::max(0, totalCores - availCoreRows);
      histScroll_ = std::clamp(histScroll_, 0, maxCoreScroll);

      int startCore = histScroll_;
      int showCores = std::min(totalCores - startCore, availCoreRows);

      for (int ci = 0; ci < showCores; ci++) {
        int coreIdx = startCore + ci;
        if (curY >= rows_ - reserveBelow)
          break;
        float val = host.cores[coreIdx];
        int co = pctColor(val, host.name, th, 'c');

        // Label
        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        char lbl[24];
        snprintf(lbl, sizeof(lbl), " core %2d ", coreIdx);
        mvaddstr(curY, 2, lbl);
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);

        int barStart = 11;
        int barW = barMaxW;
        int filled = std::clamp((int)(val / 100.0f * barW), 0, barW);

        attron(COLOR_PAIR(co) | A_BOLD);
        move(curY, barStart);
        for (int b = 0; b < filled; b++)
          addstr(BLOCK_FULL);
        attroff(COLOR_PAIR(co) | A_BOLD);

        attron(COLOR_PAIR(C_GRAY) | A_DIM);
        for (int b = filled; b < barW; b++)
          addstr(BLOCK_EMPTY);
        attroff(COLOR_PAIR(C_GRAY) | A_DIM);

        attron(COLOR_PAIR(co) | A_BOLD);
        char pctBuf[12];
        snprintf(pctBuf, sizeof(pctBuf), " %5.1f%%", val);
        addstr(pctBuf);
        attroff(COLOR_PAIR(co) | A_BOLD);

        curY++;
      }

      // Scrollbar for cores
      if (totalCores > availCoreRows) {
        int coreListTop = curY - showCores;
        int coreListBot = curY - 1;
        drawScrollbar(cols_ - 2, coreListTop, coreListBot, histScroll_,
                      availCoreRows, totalCores);
      }
    }

    // ── RAM sparkline ──
    if (curY < rows_ - 5) {
      std::vector<float> ramH;
      for (auto &s : host.history)
        ramH.push_back(s.ram);

      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      curY++;
      if (curY < rows_ - 4) {
        drawMetricTitle(curY, "RAM", host.ram, host.name, th, 'r');
        curY++;
      }
      if (curY < rows_ - 3) {
        drawSparkline(curY, 2, graphW, 1, ramH, 'r', th);
        curY++;
      }
    }

    // ── DISK sparkline ──
    if (curY < rows_ - 4) {
      std::vector<float> diskH;
      for (auto &s : host.history)
        diskH.push_back(s.disk);

      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      curY++;
      if (curY < rows_ - 3) {
        drawMetricTitle(curY, "DISK", host.disk, host.name, th, 'd');
        curY++;
      }
      if (curY < rows_ - 2) {
        drawSparkline(curY, 2, graphW, 1, diskH, 'd', th);
        curY++;
      }
    }

    // ── Host Info ──
    if (curY < rows_ - 2) {
      attron(COLOR_PAIR(C_BOX) | A_BOLD);
      drawHSep(curY);
      attroff(COLOR_PAIR(C_BOX) | A_BOLD);
      attron(COLOR_PAIR(C_CYAN) | A_BOLD);
      mvaddstr(curY, 3, " HOST INFO ");
      attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
      curY++;

      int leftCol = 2, rightCol = cols_ / 2;

      // Name + Status
      if (curY < rows_ - 1) {
        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, leftCol, "Name: ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_CYAN));
        addstr(host.name.c_str());
        attroff(COLOR_PAIR(C_CYAN));

        const char *sym, *slbl;
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
        mvprintw(curY, rightCol, "Status: ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(sc) | A_BOLD);
        addstr(sym);
        addstr(slbl);
        attroff(COLOR_PAIR(sc) | A_BOLD);
        curY++;
      }

      // IP + Cores + Last Seen
      if (curY < rows_ - 1) {
        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, leftCol, "IP:   ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_GRAY));
        addstr(host.ip.c_str());
        attroff(COLOR_PAIR(C_GRAY));

        attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        mvprintw(curY, rightCol, "Cores: ");
        attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
        attron(COLOR_PAIR(C_CYAN));
        char cBuf[8];
        snprintf(cBuf, sizeof(cBuf), "%d", host.coreCount);
        addstr(cBuf);
        attroff(COLOR_PAIR(C_CYAN));

        int tsCol = rightCol + 16;
        if (tsCol + 20 < cols_) {
          attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
          mvprintw(curY, tsCol, "Last: ");
          attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
          attron(COLOR_PAIR(C_CYAN));
          addstr(fmtTime(host.lastSeen).c_str());
          attroff(COLOR_PAIR(C_CYAN));
        }
      }
    }

    renderDetailFooter(hostCount);
  }

  // ── Detail helpers ────────────────────────────────────────────────────────
  void drawMetricTitle(int y, const char *label, float val,
                       const std::string &hostname, const Thresholds &th,
                       char metric) {
    int titleColor = C_CYAN;
    if (metric == 'r')
      titleColor = C_GREEN;
    else if (metric == 'd')
      titleColor = C_MAGENTA;

    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    mvaddstr(y, 2, LINE_H);
    addstr(" ");
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
    attron(COLOR_PAIR(titleColor) | A_BOLD);
    addstr(label);
    attroff(COLOR_PAIR(titleColor) | A_BOLD);
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    addstr(" ");
    int cur = getcurx(stdscr);
    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), " %5.1f%% ", val);
    int pctPos = cols_ - (int)strlen(pctBuf) - 3;
    for (int i = cur; i < pctPos; i++)
      addstr(LINE_H);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
    int co = pctColor(val, hostname, th, metric);
    attron(COLOR_PAIR(co) | A_BOLD);
    addstr(pctBuf);
    attroff(COLOR_PAIR(co) | A_BOLD);
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    addstr(LINE_H);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);
  }

  void renderDetailFooter(int hostCount) {
    char fi[32];
    snprintf(fi, sizeof(fi), " %d/%d ", selectedIdx_ + 1, hostCount);
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(rows_ - 1, cols_ - (int)strlen(fi) - 2, fi);
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // ── COMMAND BAR & ERROR ───────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════════
  void renderCmdBar() {
    int y = rows_ - 1;
    // Clear bottom line
    attron(COLOR_PAIR(C_HEADER) | A_BOLD);
    for (int i = 0; i < cols_; i++)
      mvaddch(y, i, ' ');
    mvaddstr(y, 1, "> /");
    addstr(cmdBuf_.c_str());
    // Cursor blink
    addstr("_");
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
    curs_set(0);
  }

  void renderCmdError() {
    int y = rows_ - 1;
    int len = (int)cmdError_.size() + 4;
    int x = (cols_ - len) / 2;
    if (x < 0)
      x = 0;
    attron(COLOR_PAIR(C_RED) | A_BOLD);
    mvaddstr(y, x, (" " + cmdError_ + " ").c_str());
    attroff(COLOR_PAIR(C_RED) | A_BOLD);
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // ── HELP VIEW ─────────────────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════════
  void renderHelp() {
    // Frame
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
    std::string htitle = std::string(SYM_DIAMOND) + " HELP " + SYM_DIAMOND;
    mvaddstr(y, (cols_ - (int)htitle.size()) / 2, htitle.c_str());
    mvaddstr(y, cols_ - 24, "[T] Theme  [Esc] Close");
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);

    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    drawHSep(2);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    y = 4;
    int lx = 4, rx = 28;

    // Navigation section
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(y++, lx, "NAVIGATION");
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
    y++;

    struct HelpLine {
      const char *key;
      const char *desc;
    };
    HelpLine nav[] = {
        {"Tab", "Enter detail view / next host"},
        {"Shift+Tab", "Previous host"},
        {"T / Ctrl+T", "Cycle theme (Vivid/Neon/Amber)"},
        {"Esc", "Back to overview / close"},
        {"\u2191 \u2193", "Scroll up / down"},
        {"PgUp / PgDn", "Scroll fast"},
        {"Q", "Quit"},
        {"/", "Open command bar"},
    };
    for (auto &h : nav) {
      if (y >= rows_ - 6)
        break;
      attron(COLOR_PAIR(C_GREEN) | A_BOLD);
      mvprintw(y, lx, "%-16s", h.key);
      attroff(COLOR_PAIR(C_GREEN) | A_BOLD);
      attron(COLOR_PAIR(C_WHITE_BD));
      mvaddstr(y, rx, h.desc);
      attroff(COLOR_PAIR(C_WHITE_BD));
      y++;
    }

    y += 2;
    // Commands section
    attron(COLOR_PAIR(C_CYAN) | A_BOLD);
    mvaddstr(y++, lx, "COMMANDS (press / to open command bar)");
    attroff(COLOR_PAIR(C_CYAN) | A_BOLD);
    y++;

    HelpLine cmds[] = {
        {"/help", "Show this help screen"},
        {"/viewer <host>", "Jump to host detail view"},
        {"/history <host>", "Show host history table"},
    };
    for (auto &h : cmds) {
      if (y >= rows_ - 2)
        break;
      attron(COLOR_PAIR(C_YELLOW) | A_BOLD);
      mvprintw(y, lx, "%-22s", h.key);
      attroff(COLOR_PAIR(C_YELLOW) | A_BOLD);
      attron(COLOR_PAIR(C_WHITE_BD));
      mvaddstr(y, rx, h.desc);
      attroff(COLOR_PAIR(C_WHITE_BD));
      y++;
    }

    y += 2;
    if (y < rows_ - 2) {
      attron(COLOR_PAIR(C_GRAY) | A_DIM);
      mvaddstr(y, lx, "Tip: commands support partial host name matching");
      attroff(COLOR_PAIR(C_GRAY) | A_DIM);
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // ── HISTORY VIEW ──────────────────────────────────────────────────────────
  // ═══════════════════════════════════════════════════════════════════════════
  void renderHistoryView() {
    if (selectedIdx_ >= (int)sortedHosts_.size())
      return;
    const auto &host = sortedHosts_[selectedIdx_];

    // Frame
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
    std::string htitle =
        std::string(SYM_DIAMOND) + " HISTORY: " + host.name + " " + SYM_DIAMOND;
    mvaddstr(y, (cols_ - (int)htitle.size()) / 2, htitle.c_str());
    mvaddstr(y, cols_ - 14, "[Esc] Back");
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);

    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    drawHSep(2);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    if (host.status == HostStatus::OFFLINE) {
      attron(COLOR_PAIR(C_GRAY) | A_DIM);
      std::string offMsg = "--- " + host.name + " is OFFLINE (no history) ---";
      mvaddstr(rows_ / 2, (cols_ - (int)offMsg.size()) / 2, offMsg.c_str());
      attroff(COLOR_PAIR(C_GRAY) | A_DIM);
      return;
    }

    // Table header
    y = 3;
    int cTime = 12, cCpu = 10, cRam = 10, cDisk = 10;
    attron(COLOR_PAIR(C_WHITE_BD) | A_BOLD);
    mvprintw(y, 4, "%-*s", cTime, "TIME");
    mvprintw(y, 4 + cTime, "%-*s", cCpu, "CPU%");
    mvprintw(y, 4 + cTime + cCpu, "%-*s", cRam, "RAM%");
    mvprintw(y, 4 + cTime + cCpu + cRam, "%-*s", cDisk, "DISK%");
    if (cols_ > 60)
      mvaddstr(y, 4 + cTime + cCpu + cRam + cDisk, "STATUS");
    attroff(COLOR_PAIR(C_WHITE_BD) | A_BOLD);

    // Separator
    attron(COLOR_PAIR(C_BOX) | A_BOLD);
    drawHSep(4);
    attroff(COLOR_PAIR(C_BOX) | A_BOLD);

    // Data rows (newest first)
    int dataY = 5;
    int innerH = rows_ - dataY - 1;
    if (innerH < 1)
      return;

    int total = (int)host.history.size();
    int maxScroll = std::max(0, total - innerH);
    histScroll_ = std::clamp(histScroll_, 0, maxScroll);

    for (int i = 0; i < innerH && i < total; i++) {
      int idx = total - 1 - i - histScroll_;
      if (idx < 0)
        break;
      const auto &s = host.history[idx];
      int ry = dataY + i;
      if (ry >= rows_ - 1)
        break;

      attron(COLOR_PAIR(C_CYAN));
      mvprintw(ry, 4, "%-*s", cTime, fmtTime(s.ts).c_str());
      attroff(COLOR_PAIR(C_CYAN));

      auto drawPct = [&](float val, char m, int col) {
        int co = pctColor(val, host.name, Thresholds{}, m);
        attron(COLOR_PAIR(co) | A_BOLD);
        mvprintw(ry, col, "%6.1f%%", val);
        attroff(COLOR_PAIR(co) | A_BOLD);
      };
      drawPct(s.cpu, 'c', 4 + cTime);
      drawPct(s.ram, 'r', 4 + cTime + cCpu);
      drawPct(s.disk, 'd', 4 + cTime + cCpu + cRam);

      // Mini status indicator
      if (cols_ > 60) {
        bool alert = (s.cpu >= 85 || s.ram >= 80 || s.disk >= 85);
        bool warn = (s.cpu >= 65 || s.ram >= 60 || s.disk >= 65);
        if (alert) {
          attron(COLOR_PAIR(C_RED) | A_BOLD);
          mvaddstr(ry, 4 + cTime + cCpu + cRam + cDisk, SYM_ONLINE);
          addstr(" HIGH");
          attroff(COLOR_PAIR(C_RED) | A_BOLD);
        } else if (warn) {
          attron(COLOR_PAIR(C_YELLOW));
          mvaddstr(ry, 4 + cTime + cCpu + cRam + cDisk, SYM_WARN);
          addstr(" MED");
          attroff(COLOR_PAIR(C_YELLOW));
        } else {
          attron(COLOR_PAIR(C_GREEN));
          mvaddstr(ry, 4 + cTime + cCpu + cRam + cDisk, SYM_ONLINE);
          addstr(" OK");
          attroff(COLOR_PAIR(C_GREEN));
        }
      }
    }

    // Scrollbar
    if (total > innerH)
      drawScrollbar(cols_ - 2, dataY, dataY + innerH - 1, histScroll_, innerH,
                    total);

    // Counter
    char cnt[32];
    snprintf(cnt, sizeof(cnt), " %d samples ", total);
    attron(COLOR_PAIR(C_GRAY) | A_DIM);
    mvaddstr(rows_ - 1, cols_ - (int)strlen(cnt) - 2, cnt);
    attroff(COLOR_PAIR(C_GRAY) | A_DIM);
  }
};

} // namespace monitor::ui
