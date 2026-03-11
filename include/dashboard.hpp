/*
 * dashboard.hpp — btop++-style ncurses dashboard
 * Themes: BLOODLINE | MOCHA | NORD | DRACULA | MATRIX | CYBERPUNK (cycle U)
 */
#pragma once
#include "metrics_store.hpp"
#include "thresholds.hpp"
#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <string>
#include <vector>

namespace monitor::ui {

enum class ViewMode { OVERVIEW, DETAIL, HELP, HISTORY };

enum Color {
  C_NORMAL=1, C_GREEN=2, C_YELLOW=3, C_RED=4, C_GRAY=5,
  C_HEADER=6, C_BOX=7, C_CYAN=8, C_MAGENTA=9, C_WHITE_BD=10,
  C_RAIN1=11, C_RAIN2=12, C_RAIN3=13, C_RAIN4=14, C_RAIN5=15, C_RAIN6=16,
  C_TIME=17, C_OK_DIM=18, C_WARN_ORANGE=19, C_FLEET=20, C_STALE=21,
};

// ── Theme names ──────────────────────────────────────────────────────────────
static const char *THEME_NAMES[] = {
  "BLOODLINE", "MOCHA", "NORD", "DRACULA", "MATRIX", "CYBERPUNK"
};
static constexpr int NUM_THEMES = 6;

static void initColors(int t = 0) {
  start_color(); use_default_colors();
  // 8-color fallback — clean and readable
  init_pair(C_NORMAL,     COLOR_WHITE,  -1);
  init_pair(C_GREEN,      COLOR_GREEN,  -1);
  init_pair(C_YELLOW,     COLOR_YELLOW, -1);
  init_pair(C_RED,        COLOR_RED,    -1);
  init_pair(C_GRAY,       COLOR_WHITE,  -1);
  init_pair(C_HEADER,     COLOR_WHITE,  COLOR_BLUE);
  init_pair(C_BOX,        COLOR_CYAN,   -1);
  init_pair(C_CYAN,       COLOR_CYAN,   -1);
  init_pair(C_MAGENTA,    COLOR_MAGENTA,-1);
  init_pair(C_WHITE_BD,   COLOR_WHITE,  -1);
  init_pair(C_RAIN1,      COLOR_RED,    -1);
  init_pair(C_RAIN2,      COLOR_YELLOW, -1);
  init_pair(C_RAIN3,      COLOR_GREEN,  -1);
  init_pair(C_RAIN4,      COLOR_CYAN,   -1);
  init_pair(C_RAIN5,      COLOR_BLUE,   -1);
  init_pair(C_RAIN6,      COLOR_MAGENTA,-1);
  init_pair(C_TIME,       COLOR_CYAN,   -1);
  init_pair(C_OK_DIM,     COLOR_GREEN,  -1);
  init_pair(C_WARN_ORANGE,COLOR_YELLOW, -1);
  init_pair(C_FLEET,      COLOR_WHITE,  COLOR_BLUE);
  init_pair(C_STALE,      COLOR_MAGENTA,-1);

  if (COLORS < 256) return;

  switch (t) {
  // ── 0 BLOODLINE ──────────────────────────────────────────────────────────
  // Crimson + Electric Cyan — high contrast dark hacker aesthetic
  default:
  case 0:
    init_pair(C_HEADER,     231,  160);  // pure white on crimson
    init_pair(C_BOX,        160,   -1);  // crimson border
    init_pair(C_CYAN,        51,   -1);  // electric cyan accent
    init_pair(C_MAGENTA,    197,   -1);  // hot crimson accent
    init_pair(C_WHITE_BD,   231,   -1);  // pure white text
    init_pair(C_GRAY,       241,   -1);  // readable dark gray
    init_pair(C_GREEN,       46,   -1);  // electric green (ok)
    init_pair(C_OK_DIM,      34,   -1);  // mid green
    init_pair(C_WARN_ORANGE,208,   -1);  // orange
    init_pair(C_YELLOW,     208,   -1);
    init_pair(C_RED,        196,   -1);  // pure red alert
    init_pair(C_TIME,       245,   -1);  // soft gray
    init_pair(C_FLEET,      231,   88);  // white on dark crimson
    init_pair(C_RAIN1,       88,   -1);
    init_pair(C_RAIN2,      124,   -1);
    init_pair(C_RAIN3,      160,   -1);
    init_pair(C_RAIN4,      196,   -1);
    init_pair(C_RAIN5,       46,   -1);
    init_pair(C_RAIN6,       51,   -1);
    init_pair(C_STALE,      141,   -1);  // lavender — distinct from red
    break;

  // ── 1 MOCHA (Catppuccin Mocha) ────────────────────────────────────────────
  // Accurate Catppuccin Mocha palette — warm mauve + pastel
  case 1:
    init_pair(C_HEADER,     183,   54);  // mauve on dark indigo bg
    init_pair(C_BOX,        105,   -1);  // periwinkle border
    init_pair(C_CYAN,       122,   -1);  // teal (catppuccin teal)
    init_pair(C_MAGENTA,    183,   -1);  // mauve accent
    init_pair(C_WHITE_BD,   189,   -1);  // text (cdd6f4 approx)
    init_pair(C_GRAY,        61,   -1);  // subtext dim
    init_pair(C_GREEN,      150,   -1);  // catppuccin green
    init_pair(C_OK_DIM,     108,   -1);
    init_pair(C_WARN_ORANGE,216,   -1);  // peach
    init_pair(C_YELLOW,     216,   -1);
    init_pair(C_RED,        211,   -1);  // catppuccin red-pink
    init_pair(C_TIME,       146,   -1);  // subtext1
    init_pair(C_FLEET,      189,   54);
    init_pair(C_RAIN1,      105,   -1);
    init_pair(C_RAIN2,      147,   -1);
    init_pair(C_RAIN3,      183,   -1);
    init_pair(C_RAIN4,      216,   -1);
    init_pair(C_RAIN5,      150,   -1);
    init_pair(C_RAIN6,      122,   -1);
    init_pair(C_STALE,      183,   -1);  // mauve
    break;

  // ── 2 NORD ───────────────────────────────────────────────────────────────
  // Accurate Nord palette: arctic blues + aurora color accents
  case 2:
    init_pair(C_HEADER,     195,   24);  // frost white-blue on deep arctic
    init_pair(C_BOX,         67,   -1);  // slate blue (nord3)
    init_pair(C_CYAN,       117,   -1);  // sky blue (frost)
    init_pair(C_MAGENTA,    110,   -1);  // frost blue (nord10)
    init_pair(C_WHITE_BD,   253,   -1);  // snow storm
    init_pair(C_GRAY,        66,   -1);  // dark blue-gray
    init_pair(C_GREEN,      108,   -1);  // aurora green
    init_pair(C_OK_DIM,      65,   -1);
    init_pair(C_WARN_ORANGE,179,   -1);  // aurora yellow-orange
    init_pair(C_YELLOW,     222,   -1);  // aurora yellow
    init_pair(C_RED,        131,   -1);  // aurora red
    init_pair(C_TIME,        66,   -1);
    init_pair(C_FLEET,      195,   24);
    init_pair(C_RAIN1,       17,   -1);
    init_pair(C_RAIN2,       24,   -1);
    init_pair(C_RAIN3,       67,   -1);
    init_pair(C_RAIN4,      110,   -1);
    init_pair(C_RAIN5,      117,   -1);
    init_pair(C_RAIN6,      195,   -1);
    init_pair(C_STALE,      139,   -1);  // muted aurora purple
    break;

  // ── 3 DRACULA ────────────────────────────────────────────────────────────
  // Accurate Dracula theme: deep purple bg + vivid accents
  case 3:
    init_pair(C_HEADER,     255,   57);  // white on deep dracula purple
    init_pair(C_BOX,        135,   -1);  // medium purple border
    init_pair(C_CYAN,       123,   -1);  // dracula cyan
    init_pair(C_MAGENTA,    212,   -1);  // dracula pink
    init_pair(C_WHITE_BD,   255,   -1);  // foreground white
    init_pair(C_GRAY,        61,   -1);  // comment color
    init_pair(C_GREEN,       84,   -1);  // dracula green (bright)
    init_pair(C_OK_DIM,      77,   -1);
    init_pair(C_WARN_ORANGE,215,   -1);  // dracula orange
    init_pair(C_YELLOW,     228,   -1);  // dracula yellow
    init_pair(C_RED,        203,   -1);  // dracula red
    init_pair(C_TIME,        61,   -1);
    init_pair(C_FLEET,      255,   57);
    init_pair(C_RAIN1,      212,   -1);
    init_pair(C_RAIN2,      141,   -1);
    init_pair(C_RAIN3,      123,   -1);
    init_pair(C_RAIN4,       84,   -1);
    init_pair(C_RAIN5,      228,   -1);
    init_pair(C_RAIN6,      203,   -1);
    init_pair(C_STALE,      183,   -1);  // mauve/purple
    break;

  // ── 4 MATRIX ─────────────────────────────────────────────────────────────
  // Classic green cascade — pure monochrome hacker
  case 4:
    init_pair(C_HEADER,      82,   22);  // lime on deep forest
    init_pair(C_BOX,         28,   -1);  // forest green border
    init_pair(C_CYAN,        82,   -1);  // lime green primary
    init_pair(C_MAGENTA,     40,   -1);  // medium green accent
    init_pair(C_WHITE_BD,    82,   -1);  // bright green text
    init_pair(C_GRAY,        28,   -1);  // dim green
    init_pair(C_GREEN,       46,   -1);  // pure bright green
    init_pair(C_OK_DIM,      34,   -1);
    init_pair(C_WARN_ORANGE, 40,   -1);  // brighter green for warn
    init_pair(C_YELLOW,     226,   -1);  // acid yellow (critical only)
    init_pair(C_RED,        196,   -1);  // red — breaks theme intentionally
    init_pair(C_TIME,        28,   -1);
    init_pair(C_FLEET,       82,   22);
    init_pair(C_RAIN1,       22,   -1);
    init_pair(C_RAIN2,       28,   -1);
    init_pair(C_RAIN3,       34,   -1);
    init_pair(C_RAIN4,       40,   -1);
    init_pair(C_RAIN5,       46,   -1);
    init_pair(C_RAIN6,       82,   -1);
    init_pair(C_STALE,       58,   -1);  // dark olive
    break;

  // ── 5 CYBERPUNK ──────────────────────────────────────────────────────────
  // Neon pink + electric cyan + acid yellow — eye-searing max contrast
  case 5:
    init_pair(C_HEADER,     228,   91);  // acid yellow on deep magenta
    init_pair(C_BOX,        198,   -1);  // hot pink/red border
    init_pair(C_CYAN,        51,   -1);  // electric cyan
    init_pair(C_MAGENTA,    201,   -1);  // neon pink
    init_pair(C_WHITE_BD,   231,   -1);  // pure white
    init_pair(C_GRAY,       238,   -1);  // dark gray
    init_pair(C_GREEN,      118,   -1);  // chartreuse green
    init_pair(C_OK_DIM,      77,   -1);
    init_pair(C_WARN_ORANGE,214,   -1);  // neon orange
    init_pair(C_YELLOW,     228,   -1);  // acid yellow
    init_pair(C_RED,        197,   -1);  // hot electric red
    init_pair(C_TIME,       238,   -1);
    init_pair(C_FLEET,      228,   91);
    init_pair(C_RAIN1,      201,   -1);
    init_pair(C_RAIN2,      198,   -1);
    init_pair(C_RAIN3,      228,   -1);
    init_pair(C_RAIN4,       51,   -1);
    init_pair(C_RAIN5,       45,   -1);
    init_pair(C_RAIN6,      118,   -1);
    init_pair(C_STALE,      135,   -1);  // medium violet
    break;
  }
}

// ── Minimum terminal dimensions (derived from layout constants) ───────────────
//
// ROW breakdown (from calcOverviewLayout):
//   0          top frame border
//   1          header bar
//   graphY_(2) graph section separator
//   3..6       graph content (graphH_=4 rows)
//   7          table section separator  (graphY_+1+graphH_)
//   8          table header row
//   9          table header underline separator
//   10         min 1 data row           (tableH_min=3 → header+sep+1row)
//   11         log section separator    (tableY_+1+tableH_min)
//   12..13     log content min 2 rows
//   14         bottom frame border
//
static constexpr int LAYOUT_GRAPH_Y     = 2;  // graphY_ in calcOverviewLayout
static constexpr int LAYOUT_GRAPH_H     = 4;  // graphH_ in calcOverviewLayout
static constexpr int LAYOUT_TABLE_H_MIN = 3;  // header(1) + sep(1) + 1 data row
static constexpr int LAYOUT_LOG_H_MIN   = 2;  // minimum visible log rows

static constexpr int MIN_ROWS =
    1                           // top frame border
  + 1                           // header bar (row 1)
  + 1 + LAYOUT_GRAPH_H          // graph sep + graph content
  + 1 + LAYOUT_TABLE_H_MIN      // table sep + table (hdr+undersep+1data)
  + 1 + LAYOUT_LOG_H_MIN        // log sep + min log rows
  + 1;                          // bottom frame border
// = 1+1+1+4+1+3+1+2+1 = 15

// COL breakdown (from renderTable):
//   innerW = cols-2 (left/right frame borders)
//   cHost_min = 6   (min(16, innerW/6), but cBar formula needs innerW/6 >= 6 → innerW>=36)
//   cBar_min  = 6   (clamped floor in renderTable)
//   cPct      = 5   (fixed)
//   cStatus   = 8   (fixed)
//   3 metric cols, each: 1(gap) + cBar_min + cPct
//   Plus load col (~6), gap(1), right edge
//   innerW_min = cHost_min + 1 + (cBar_min+cPct+1)*3 + 6 + 1 + cStatus
//
static constexpr int LAYOUT_CHOST_MIN = 6;
static constexpr int LAYOUT_CBAR_MIN  = 6;
static constexpr int LAYOUT_CPCT      = 5;
static constexpr int LAYOUT_CSTATUS   = 8;
static constexpr int LAYOUT_CLOAD     = 6;  // load avg display width

static constexpr int MIN_COLS =
    2                                        // frame borders (left + right)
  + LAYOUT_CHOST_MIN + 1                     // host col + gap
  + (LAYOUT_CBAR_MIN + LAYOUT_CPCT + 1) * 3 // 3× metric (bar+pct+gap)
  + LAYOUT_CLOAD + 1                         // load avg + gap
  + LAYOUT_CSTATUS;                          // status col
// = 2 + 7 + 36 + 7 + 8 = 60

static std::string fmtTime(time_t t) {
  char buf[16]; struct tm *tm_=localtime(&t);
  strftime(buf, sizeof(buf), "%H:%M:%S", tm_); return buf;
}
static std::string trunc(const std::string &s, int w) {
  if (w<=0) return "";
  if ((int)s.size()<=w) { std::string r=s; while((int)r.size()<w) r+=' '; return r; }
  return s.substr(0,w);
}
static int pctColor(float pct, const std::string &host, const Thresholds &th, char metric) {
  float alTh = metric=='c' ? th.getCPU(host) : metric=='r' ? th.getRAM(host) : th.getDisk(host);
  if (pct >= alTh)           return C_RED;
  if (pct >= alTh * 0.80f)   return C_WARN_ORANGE;
  return C_OK_DIM;
}

static const char *SPARK_CHARS[]={"▁","▂","▃","▄","▅","▆","▇","█"};
static const char *BLOCK_FULL="█", *BLOCK_EMPTY="░";
static const char *BOX_TL="╔",*BOX_TR="╗",*BOX_BL="╚",*BOX_BR="╝";
static const char *BOX_H="═",*BOX_V="║",*BOX_LT="╠",*BOX_RT="╣";
static const char *SYM_ONLINE="●",*SYM_WARN="◐",*SYM_DIAMOND="◈",*LINE_H="─";

// ── Dashboard ─────────────────────────────────────────────────────────────────
class Dashboard {
public:
  Dashboard() = default;
  ~Dashboard() { teardown(); }

  void init() {
    setlocale(LC_ALL,"");
    initscr(); noecho(); cbreak(); curs_set(0);
    keypad(stdscr, TRUE); halfdelay(2);
    initColors(uiMode_);
    getmaxyx(stdscr, rows_, cols_);
  }

  void teardown() { if (!isendwin()) endwin(); }

  // ── Too-small screen ───────────────────────────────────────────────────────
  void renderTooSmall() {
    erase();
    int r, c; getmaxyx(stdscr, r, c);

    // Draw a simple box if even that fits
    if (r >= 6 && c >= 36) {
      attron(COLOR_PAIR(C_BOX)|A_BOLD);
      mvaddstr(0,0,BOX_TL); for(int i=1;i<c-1;i++) addstr(BOX_H); addstr(BOX_TR);
      mvaddstr(r-1,0,BOX_BL); for(int i=1;i<c-1;i++) addstr(BOX_H); addstr(BOX_BR);
      for(int i=1;i<r-1;i++){ mvaddstr(i,0,BOX_V); mvaddstr(i,c-1,BOX_V); }
      attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    }

    int my = r/2 - 2, mx;
    if (my < 1) my = 0;

    // Line 1: warning title
    {
      const char *title = " Terminal size too small ";
      mx = (c - (int)strlen(title)) / 2; if(mx<1) mx=1;
      attron(COLOR_PAIR(C_RED)|A_BOLD);
      mvaddstr(my, mx, title);
      attroff(COLOR_PAIR(C_RED)|A_BOLD);
    }
    my++;

    // Line 2: current size
    if (my < r-1) {
      char cur[48]; snprintf(cur, sizeof(cur), " Width = %d  Height = %d ", c, r);
      mx = (c - (int)strlen(cur)) / 2; if(mx<1) mx=1;
      attron(COLOR_PAIR(C_YELLOW)|A_BOLD);
      mvaddstr(my, mx, cur);
      attroff(COLOR_PAIR(C_YELLOW)|A_BOLD);
    }
    my += 2;

    // Line 3: required size header
    if (my < r-1) {
      const char *need = " Needed for current config: ";
      mx = (c - (int)strlen(need)) / 2; if(mx<1) mx=1;
      attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
      mvaddstr(my, mx, need);
      attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
    }
    my++;

    // Line 4: required dimensions
    if (my < r-1) {
      char req[48]; snprintf(req, sizeof(req), " Width = %d  Height = %d ", MIN_COLS, MIN_ROWS);
      mx = (c - (int)strlen(req)) / 2; if(mx<1) mx=1;
      attron(COLOR_PAIR(C_GREEN)|A_BOLD);
      mvaddstr(my, mx, req);
      attroff(COLOR_PAIR(C_GREEN)|A_BOLD);
    }

    refresh();
  }

  void render(const std::vector<HostState> &hosts,
              const std::vector<LogEvent> &log,
              const Thresholds &thresh) {
    getmaxyx(stdscr, rows_, cols_);

    // Guard: terminal too small
    if (rows_ < MIN_ROWS || cols_ < MIN_COLS) {
      // Still handle key input so the user can quit
      int ch = getch(); if(ch=='q'||ch=='Q') { teardown(); exit(0); }
      renderTooSmall();
      return;
    }

    sortedHosts_ = hosts;
    // Sort: ALERT < WARNING < STALE < ONLINE < OFFLINE (enum order) then by name
    std::stable_sort(sortedHosts_.begin(), sortedHosts_.end(),
              [](const HostState &a, const HostState &b){
                if (a.status != b.status)
                  return (int)a.status < (int)b.status;
                return a.name < b.name;
              });

    int ch = getch();
    handleKey(ch, log);

    erase();
    switch (viewMode_) {
    case ViewMode::OVERVIEW:
      renderOverview(log, thresh);
      if (cmdActive_)        renderCmdBar();
      else if (cmdErrorTimer_>0) { renderCmdError(); cmdErrorTimer_--; }
      break;
    case ViewMode::DETAIL:  renderDetailView(thresh); break;
    case ViewMode::HELP:    renderHelp(); break;
    case ViewMode::HISTORY: renderHistoryView(); break;
    }
    refresh();
  }

private:
  // ── State ──────────────────────────────────────────────────────────────────
  int rows_=24, cols_=80;
  int uiMode_=0;
  ViewMode viewMode_=ViewMode::OVERVIEW, prevMode_=ViewMode::OVERVIEW;
  std::vector<HostState> sortedHosts_;
  int selectedIdx_=0, logScroll_=0, histScroll_=0;
  std::string historyHost_;
  bool cmdActive_=false;
  std::string cmdBuf_, cmdError_;
  int cmdErrorTimer_=0;
  int hdrY_,graphY_,graphH_,tableY_,tableH_,logY_,logH_;

  // ── Key handling ──────────────────────────────────────────────────────────
  void handleKey(int ch, const std::vector<LogEvent> &log) {
    (void)log; // log not used directly in key handler
    if (cmdActive_) { handleCmdKey(ch); return; }
    switch (ch) {
    case 'q': case 'Q': teardown(); exit(0);
    case 'u': case 'U': case 21: // Ctrl+U
      uiMode_ = (uiMode_ + 1) % NUM_THEMES;
      initColors(uiMode_); break;
    case '/':
      if (viewMode_==ViewMode::OVERVIEW) { cmdActive_=true; cmdBuf_.clear(); }
      break;
    case 9: // Tab
      if (viewMode_==ViewMode::OVERVIEW) {
        if (!sortedHosts_.empty()) { selectedIdx_=0; histScroll_=0; viewMode_=ViewMode::DETAIL; }
      } else if (viewMode_==ViewMode::DETAIL) {
        selectedIdx_=(selectedIdx_+1)%(int)sortedHosts_.size(); histScroll_=0;
      }
      break;
    case KEY_BTAB:
      if (viewMode_==ViewMode::DETAIL) {
        selectedIdx_=(selectedIdx_+sortedHosts_.size()-1)%(int)sortedHosts_.size(); histScroll_=0;
      }
      break;
    case 27: case KEY_BACKSPACE: case 127:
      if (viewMode_!=ViewMode::OVERVIEW) { viewMode_=ViewMode::OVERVIEW; histScroll_=0; }
      break;
    case KEY_UP:   case 'k': logScroll_++; histScroll_++; break;
    case KEY_DOWN: case 'j': logScroll_=std::max(0,logScroll_-1); histScroll_=std::max(0,histScroll_-1); break;
    case KEY_PPAGE: logScroll_+=10; histScroll_+=10; break;
    case KEY_NPAGE: logScroll_=std::max(0,logScroll_-10); histScroll_=std::max(0,histScroll_-10); break;
    }
  }

  void handleCmdKey(int ch) {
    if (ch==27||ch==KEY_F(1)) { cmdActive_=false; cmdBuf_.clear(); return; }
    if (ch=='\n'||ch==KEY_ENTER) { execCmd(cmdBuf_); cmdActive_=false; cmdBuf_.clear(); return; }
    if ((ch==KEY_BACKSPACE||ch==127) && !cmdBuf_.empty()) { cmdBuf_.pop_back(); return; }
    if (ch>=32&&ch<127) cmdBuf_+=(char)ch;
  }

  void execCmd(const std::string &cmd) {
    if (cmd=="help"||cmd=="/help") { viewMode_=ViewMode::HELP; return; }
    if (cmd=="clear") { return; }
    auto sp = cmd.find(' ');
    std::string verb = (sp==std::string::npos) ? cmd : cmd.substr(0,sp);
    std::string target = (sp==std::string::npos) ? "" : cmd.substr(sp+1);
    if (verb=="viewer"||verb=="/viewer") {
      int idx=findHost(target);
      if (idx<0) { cmdError_="Host not found: "+target; cmdErrorTimer_=20; return; }
      selectedIdx_=idx; histScroll_=0; viewMode_=ViewMode::DETAIL;
    } else if (verb=="history"||verb=="/history") {
      int idx=findHost(target);
      if (idx<0) { cmdError_="Host not found: "+target; cmdErrorTimer_=20; return; }
      selectedIdx_=idx; historyHost_=target; prevMode_=viewMode_;
      viewMode_=ViewMode::HISTORY; histScroll_=0;
    } else {
      cmdError_="Unknown command: /"+verb+"  (try /help)"; cmdErrorTimer_=20;
    }
  }

  int findHost(const std::string &name) {
    for (int i=0;i<(int)sortedHosts_.size();i++) {
      if (sortedHosts_[i].name==name) return i;
      if (sortedHosts_[i].name.find(name)!=std::string::npos) return i;
    }
    return -1;
  }

  // ── Overview ───────────────────────────────────────────────────────────────
  void renderOverview(const std::vector<LogEvent> &log, const Thresholds &thresh) {
    calcOverviewLayout();
    drawOuterFrame();
    renderHeader(("[ U ] " + std::string(THEME_NAMES[uiMode_]) + "  [Tab] Detail  [/] Cmd  [Q] Quit").c_str());
    renderGraphs(thresh);
    renderTable(thresh);
    renderLog(log, thresh);
  }

  void calcOverviewLayout() {
    hdrY_=0; graphY_=2; graphH_=4;
    int graphEnd=graphY_+1+graphH_;
    tableY_=graphEnd;
    int hostCount=std::max(1,(int)sortedHosts_.size());
    tableH_=2+hostCount;
    if (tableH_>rows_/3) tableH_=rows_/3;
    if (tableH_<3)       tableH_=3;
    int tableEnd=tableY_+1+tableH_;
    logY_=tableEnd; logH_=rows_-logY_-1;
    if (logH_<2) logH_=2;
  }

  void drawOuterFrame() {
    attron(COLOR_PAIR(C_BOX)|A_BOLD);
    mvaddstr(0,0,BOX_TL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_TR);
    mvaddstr(rows_-1,0,BOX_BL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_BR);
    for(int r=1;r<rows_-1;r++){ mvaddstr(r,0,BOX_V); mvaddstr(r,cols_-1,BOX_V); }
    drawHSep(graphY_); drawHSep(tableY_);
    // Table header underline — use ═ consistent with other separators
    int ths=tableY_+2;
    if (ths<rows_-1) {
      attron(COLOR_PAIR(C_GRAY)|A_DIM);
      mvaddstr(ths,0,BOX_LT);
      for(int i=1;i<cols_-1;i++) addstr(BOX_H);
      mvaddstr(ths,cols_-1,BOX_RT);
      attroff(COLOR_PAIR(C_GRAY)|A_DIM);
    }
    drawHSep(logY_);
    attroff(COLOR_PAIR(C_BOX)|A_BOLD);
  }

  void drawHSep(int y) {
    if (y<=0||y>=rows_-1) return;
    mvaddstr(y,0,BOX_LT); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_RT);
  }

  void renderHeader(const char *hint) {
    int y=1;
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    for(int i=1;i<cols_-1;i++) mvaddch(y,i,' ');
    // Time left, title center, hint right — all same color
    std::string ts=fmtTime(time(nullptr));
    mvaddstr(y,2,ts.c_str());
    std::string title=" ◈  DISTRIBUTED SYSTEM MONITOR  ◈ ";
    int tx=(cols_-(int)title.size())/2;
    if (tx>10) mvaddstr(y,tx,title.c_str());
    int hintLen=(int)strlen(hint);
    if(cols_-hintLen-3>0) mvaddstr(y,cols_-hintLen-2,hint);
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
  }

  void renderGraphs(const Thresholds &th) {
    (void)th; // th used only in sub-lambdas via pctColor through drawSparkline
    int innerW=cols_-2, panelW=innerW/3, panelWLast=innerW-panelW*2;
    int startY=graphY_+1;
    int online=0,offline=0,warn=0,alert=0,stale=0;
    float sumCpu=0,sumRam=0,maxCpu=-1,maxRam=-1,maxDisk=-1;
    std::string hotCpu="-",hotRam="-",hotDisk="-";
    float sumLoad=0; int sumProc=0, netOnline=0;

    for (const auto &h : sortedHosts_) {
      if (h.status==HostStatus::OFFLINE) { offline++; continue; }
      if (h.status==HostStatus::STALE)   { stale++; continue; }
      online++; netOnline++;
      sumCpu+=h.cpu; sumRam+=h.ram;
      sumLoad+=h.loadAvg; sumProc+=h.procCount;
      if (h.status==HostStatus::WARNING) warn++;
      if (h.status==HostStatus::ALERT)   alert++;
      if (h.cpu>maxCpu)  { maxCpu=h.cpu;   hotCpu=h.name; }
      if (h.ram>maxRam)  { maxRam=h.ram;   hotRam=h.name; }
      if (h.disk>maxDisk){ maxDisk=h.disk; hotDisk=h.name;}
    }
    float avgCpu=netOnline?sumCpu/netOnline:0;
    float avgRam=netOnline?sumRam/netOnline:0;
    float avgLoad=netOnline?sumLoad/netOnline:0;
    int threat=std::min(100, alert*30+warn*10+(int)(avgCpu*0.2f));

    auto drawPanel=[&](int x0, int pw, const char *title, int titleColor) {
      if (x0>1) {
        attron(COLOR_PAIR(C_BOX)|A_BOLD);
        for(int r=startY;r<startY+graphH_;r++) mvaddstr(r,x0,BOX_V);
        attroff(COLOR_PAIR(C_BOX)|A_BOLD);
      }
      attron(COLOR_PAIR(C_BOX)|A_BOLD); mvaddstr(startY,x0+(x0>1?1:0),LINE_H); addstr(" "); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
      attron(COLOR_PAIR(titleColor)|A_BOLD); addstr(title); attroff(COLOR_PAIR(titleColor)|A_BOLD);
      attron(COLOR_PAIR(C_BOX)|A_BOLD); addstr(" ");
      int cur=getcurx(stdscr); for(int i=cur;i<x0+pw;i++) addstr(LINE_H);
      attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    };

    // Panel 1: Threat score with mini bar
    drawPanel(1, panelW, "TACTICAL RISK", C_RED);
    {
      int cRisk=threat>=70?C_RED:threat>=35?C_YELLOW:C_OK_DIM;
      int barW=std::min(panelW-14, 16); if(barW<4) barW=4;
      int filled=std::clamp((int)(threat*barW/100), 0, barW);
      attron(COLOR_PAIR(cRisk)|A_BOLD);
      mvprintw(startY+1,3,"RISK %3d%% ",threat);
      for(int b=0;b<filled;b++) addstr(BLOCK_FULL);
      attroff(COLOR_PAIR(cRisk)|A_BOLD);
      attron(COLOR_PAIR(C_GRAY)|A_DIM);
      for(int b=filled;b<barW;b++) addstr(BLOCK_EMPTY);
      attroff(COLOR_PAIR(C_GRAY)|A_DIM);
      attron(COLOR_PAIR(C_CYAN));
      mvprintw(startY+2,3,"● %-2d alert  ◐ %-2d warn",alert,warn);
      mvprintw(startY+3,3,"▶ %-2d on  ▪ %-2d stale  ○ %-2d off",online,stale,offline);
      attroff(COLOR_PAIR(C_CYAN));
    }

    // Panel 2: Hot targets
    int x2=1+panelW;
    drawPanel(x2, panelW, "HOT TARGETS", C_MAGENTA);
    {
      int co1=maxCpu>=0?C_RED:C_GRAY;
      int co2=maxRam>=0?C_WARN_ORANGE:C_GRAY;
      int co3=maxDisk>=0?C_CYAN:C_GRAY;
      attron(COLOR_PAIR(co1)|A_BOLD);
      mvprintw(startY+1,x2+2,"⬆ CPU  %-10s %5.1f%%",trunc(hotCpu,10).c_str(),std::max(0.f,maxCpu));
      attroff(COLOR_PAIR(co1)|A_BOLD);
      attron(COLOR_PAIR(co2)|A_BOLD);
      mvprintw(startY+2,x2+2,"⬆ RAM  %-10s %5.1f%%",trunc(hotRam,10).c_str(),std::max(0.f,maxRam));
      attroff(COLOR_PAIR(co2)|A_BOLD);
      attron(COLOR_PAIR(co3)|A_BOLD);
      mvprintw(startY+3,x2+2,"⬆ DISK %-10s %5.1f%%",trunc(hotDisk,10).c_str(),std::max(0.f,maxDisk));
      attroff(COLOR_PAIR(co3)|A_BOLD);
    }

    // Panel 3: Fleet health
    int x3=1+panelW*2;
    drawPanel(x3, panelWLast, "FLEET HEALTH", C_CYAN);
    {
      int cCpu=avgCpu>=80?C_RED:avgCpu>=60?C_WARN_ORANGE:C_OK_DIM;
      int cRam=avgRam>=85?C_RED:avgRam>=65?C_WARN_ORANGE:C_OK_DIM;
      attron(COLOR_PAIR(cCpu)|A_BOLD); mvprintw(startY+1,x3+2,"CPU avg : %5.1f%%",avgCpu); attroff(COLOR_PAIR(cCpu)|A_BOLD);
      attron(COLOR_PAIR(cRam)|A_BOLD); mvprintw(startY+2,x3+2,"RAM avg : %5.1f%%",avgRam); attroff(COLOR_PAIR(cRam)|A_BOLD);
      attron(COLOR_PAIR(C_FLEET)|A_BOLD);
      mvprintw(startY+3,x3+2,"LOAD/1m : %5.2f  PROCS:%-5d",avgLoad,sumProc);
      attroff(COLOR_PAIR(C_FLEET)|A_BOLD);
    }
  }

  void drawSparkline(int y, int x, int w, int rows,
                     const std::vector<float> &data, char metric,
                     const Thresholds &th) {
    if (w<1||rows<1) return;
    std::vector<float> vals=data;
    while((int)vals.size()<w) vals.insert(vals.begin(),0.0f);
    if((int)vals.size()>w) vals.erase(vals.begin(),vals.begin()+(int)vals.size()-w);
    for(int row=0;row<rows;row++) {
      int ry=y+row; move(ry,x);
      for(int i=0;i<w;i++) {
        float pct=std::clamp(vals[i],0.0f,100.0f), eff;
        if(rows==1) eff=pct;
        else if(row==0) eff=(pct>50.0f)?(pct-50.0f)*2.0f:0.0f;
        else eff=std::min(pct,50.0f)*2.0f;
        int level=std::clamp((int)(eff/100.0f*8.0f),0,7);
        int co=pctColor(pct,"",th,metric);
        attron(COLOR_PAIR(co));
        if(eff>0.5f) addstr(SPARK_CHARS[level]); else addch(' ');
        attroff(COLOR_PAIR(co));
      }
    }
  }

  void drawScrollbar(int x, int y0, int y1, int scroll, int visible, int total) {
    int trackH=y1-y0+1;
    if(trackH<2||total<=visible) return;
    int thumbH=std::max(1,trackH*visible/total);
    int thumbPos=(total-visible>0)?(trackH-thumbH)*scroll/(total-visible):0;
    thumbPos=std::clamp(thumbPos,0,trackH-thumbH);
    for(int i=0;i<trackH;i++) {
      bool isThumb=(i>=thumbPos&&i<thumbPos+thumbH);
      if(isThumb) { attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(y0+i,x,"┃"); attroff(COLOR_PAIR(C_CYAN)|A_BOLD); }
      else        { attron(COLOR_PAIR(C_GRAY)|A_DIM);  mvaddstr(y0+i,x,"│"); attroff(COLOR_PAIR(C_GRAY)|A_DIM); }
    }
  }

  // ── Host Table ─────────────────────────────────────────────────────────────
  void renderTable(const Thresholds &th) {
    int innerW=cols_-2;
    attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(tableY_,3," HOST TABLE "); attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
    int cHost=std::min(16,innerW/6), cBar=std::min(16,(innerW-cHost-24)/3);
    if(cBar<6) cBar=6;
    int cPct=5, cStatus=8;
    int hy=tableY_+1, x=2;
    // Table column headers — slightly dim to not compete with data
    attron(COLOR_PAIR(C_CYAN)|A_BOLD);
    mvprintw(hy,x,"%-*s",cHost,"HOST"); x+=cHost+1;
    mvprintw(hy,x,"%-*s",cBar+cPct,"CPU");  x+=cBar+cPct+1;
    mvprintw(hy,x,"%-*s",cBar+cPct,"RAM");  x+=cBar+cPct+1;
    mvprintw(hy,x,"%-*s",cBar+cPct,"DISK");
    mvprintw(hy,x+cBar+cPct+2,"LOAD");
    mvprintw(hy,innerW-cStatus,"STATUS");
    attroff(COLOR_PAIR(C_CYAN)|A_BOLD);

    int dataY=tableY_+3, maxRows=logY_-dataY;
    if(maxRows<0) maxRows=0;
    int row=0;
    for(int idx=0;idx<(int)sortedHosts_.size()&&row<maxRows;idx++) {
      auto &h=sortedHosts_[idx]; int ry=dataY+row; x=2;
      bool off=(h.status==HostStatus::OFFLINE);
      bool staleH=(h.status==HostStatus::STALE);
      int namePairId = off ? C_GRAY : staleH ? C_STALE : C_WHITE_BD;
      int nameAttr   = (off||staleH) ? A_DIM : A_BOLD;
      attron(COLOR_PAIR(namePairId)|nameAttr);
      mvprintw(ry,x,"%-*s",cHost,trunc(h.name,cHost).c_str());
      attroff(COLOR_PAIR(namePairId)|nameAttr);
      x+=cHost+1;
      if(off) {
        attron(COLOR_PAIR(C_GRAY)|A_DIM);
        // Show how long ago it was last seen
        time_t age = time(nullptr) - h.lastSeen;
        char offMsg[64];
        if(h.lastSeen>0) snprintf(offMsg,sizeof(offMsg),"OFFLINE  (%ldm ago)",(long)(age/60));
        else             snprintf(offMsg,sizeof(offMsg),"OFFLINE");
        mvprintw(ry,x,"%-*s",innerW-x-cStatus-2,offMsg);
        mvprintw(ry,innerW-cStatus,"OFFLINE");
        attroff(COLOR_PAIR(C_GRAY)|A_DIM);
        row++; continue;
      }
      if(staleH) {
        attron(COLOR_PAIR(C_STALE)|A_DIM);
        time_t age = time(nullptr) - h.lastSeen;
        char staleMsg[64];
        if(h.lastSeen>0) snprintf(staleMsg,sizeof(staleMsg),"STALE    (%lds ago)",(long)age);
        else             snprintf(staleMsg,sizeof(staleMsg),"STALE");
        mvprintw(ry,x,"%-*s",innerW-x-cStatus-2,staleMsg);
        mvprintw(ry,innerW-cStatus,"◌ STALE");
        attroff(COLOR_PAIR(C_STALE)|A_DIM);
        row++; continue;
      }
      auto drawBar=[&](float val,char m,int col){
        int co=pctColor(val,h.name,th,m);
        int filled=std::clamp((int)(val/100.0f*cBar),0,cBar);
        move(ry,col);
        attron(COLOR_PAIR(co)|A_BOLD);
        for(int i=0;i<filled;i++) addstr(BLOCK_FULL);
        attroff(COLOR_PAIR(co)|A_BOLD);
        attron(COLOR_PAIR(C_GRAY)|A_DIM);
        for(int i=filled;i<cBar;i++) addstr(BLOCK_EMPTY);
        attroff(COLOR_PAIR(C_GRAY)|A_DIM);
        attron(COLOR_PAIR(co)|A_BOLD); printw("%3.0f%% ",val); attroff(COLOR_PAIR(co)|A_BOLD);
      };
      int bx=2+cHost+1;
      drawBar(h.cpu,'c',bx);
      drawBar(h.ram,'r',bx+cBar+cPct+1);
      drawBar(h.disk,'d',bx+(cBar+cPct+1)*2);
      // Load avg
      int loadX=bx+(cBar+cPct+1)*3;
      if(loadX+8<innerW-cStatus) {
        int lc=h.loadAvg>=4?C_RED:h.loadAvg>=2?C_YELLOW:C_OK_DIM;
        attron(COLOR_PAIR(lc));
        mvprintw(ry,loadX,"%4.2f",h.loadAvg);
        attroff(COLOR_PAIR(lc));
      }
      const char *sym,*label; int sc;
      switch(h.status) {
      case HostStatus::ALERT:   sc=C_RED;    sym=SYM_ONLINE; label=" ALERT"; break;
      case HostStatus::WARNING: sc=C_YELLOW; sym=SYM_WARN;   label=" WARN"; break;
      case HostStatus::STALE:   sc=C_STALE;  sym="◌";        label=" STALE"; break;
      default:                  sc=C_GREEN;  sym=SYM_ONLINE; label=" OK"; break;
      }
      attron(COLOR_PAIR(sc)|A_BOLD);
      mvaddstr(ry,innerW-cStatus,sym); addstr(label);
      attroff(COLOR_PAIR(sc)|A_BOLD);
      row++;
    }
  }

  // ── Log ────────────────────────────────────────────────────────────────────
  void renderLog(const std::vector<LogEvent> &log, const Thresholds &th) {
    std::string logTitle=" EVENT LOG  [↑↓ / PgUp PgDn to scroll] ";
    int ltx=(cols_-(int)logTitle.size())/2; if(ltx<3) ltx=3;
    attron(COLOR_PAIR(C_MAGENTA)|A_BOLD); mvaddstr(logY_,ltx,logTitle.c_str()); attroff(COLOR_PAIR(C_MAGENTA)|A_BOLD);
    int contentY=logY_+1, innerH=logH_; if(innerH<=0) return;
    int maxScroll=std::max(0,(int)log.size()-innerH);
    logScroll_=std::clamp(logScroll_,0,maxScroll);
    int startIdx=(int)log.size()-innerH-logScroll_;
    if(startIdx<0) startIdx=0;
    for(int i=0;i<innerH;i++) {
      int idx=startIdx+i; if(idx<0||idx>=(int)log.size()) continue;
      const auto &ev=log[idx]; int ry=contentY+i; if(ry>=rows_-1) break;
      int cx=2, ec; const char *es, *badge;
      switch(ev.type) {
      case LogEventType::CONNECT:    ec=C_OK_DIM;      es="CONN   "; badge="▶"; break;
      case LogEventType::METRIC:     ec=C_GRAY;        es="METRIC "; badge="·"; break;
      case LogEventType::ALERT:      ec=C_RED;         es="ALERT  "; badge="●"; break;
      case LogEventType::DISCONNECT: ec=C_WARN_ORANGE; es="DISC   "; badge="◀"; break;
      case LogEventType::STALE:      ec=C_STALE;       es="STALE  "; badge="◌"; break;
      default:                       ec=C_NORMAL;      es="INFO   "; badge="·"; break;
      }
      // Row: badge | time | host | ip | event type | metric values
      attron(COLOR_PAIR(ec)|A_BOLD); mvaddstr(ry,cx,badge); cx+=2; attroff(COLOR_PAIR(ec)|A_BOLD);
      attron(COLOR_PAIR(C_GRAY)|A_DIM); mvaddstr(ry,cx,fmtTime(ev.ts).c_str()); cx+=10; attroff(COLOR_PAIR(C_GRAY)|A_DIM);
      attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(ry,cx,"%-14s",trunc(ev.host,14).c_str()); cx+=15; attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
      attron(COLOR_PAIR(C_GRAY)|A_DIM); mvprintw(ry,cx,"%-14s",trunc(ev.ip,14).c_str()); cx+=15; attroff(COLOR_PAIR(C_GRAY)|A_DIM);
      attron(COLOR_PAIR(ec)|A_BOLD); mvaddstr(ry,cx,es); cx+=7; attroff(COLOR_PAIR(ec)|A_BOLD);
      if((ev.type==LogEventType::METRIC||ev.type==LogEventType::ALERT)&&cx+24<cols_-1) {
        auto pm=[&](float val,char m,const char *lbl){
          int co=pctColor(val,ev.host,th,m);
          attron(COLOR_PAIR(co));
          char mb[16]; snprintf(mb,sizeof(mb),"%s%3.0f%%",lbl,val);
          mvaddstr(ry,cx,mb); cx+=7;
          attroff(COLOR_PAIR(co));
        };
        pm(ev.cpu,'c',"C:"); pm(ev.ram,'r',"R:"); pm(ev.disk,'d',"D:");
        if(!ev.detail.empty()&&cx+3<cols_-1) {
          attron(COLOR_PAIR(C_RED)|A_BOLD);
          mvaddstr(ry,cx,(" "+ev.detail.substr(0,cols_-cx-3)).c_str());
          attroff(COLOR_PAIR(C_RED)|A_BOLD);
        }
      } else if(!ev.detail.empty()&&cx+3<cols_-1) {
        attron(COLOR_PAIR(C_GRAY)|A_DIM);
        mvaddstr(ry,cx,ev.detail.substr(0,cols_-cx-2).c_str());
        attroff(COLOR_PAIR(C_GRAY)|A_DIM);
      }
    }
    char si[32]; snprintf(si,sizeof(si)," %d/%d ",(int)log.size(),MAX_LOG_ENTRIES);
    attron(COLOR_PAIR(C_GRAY)|A_DIM); mvaddstr(rows_-1,cols_-(int)strlen(si)-2,si); attroff(COLOR_PAIR(C_GRAY)|A_DIM);
    if((int)log.size()>innerH)
      drawScrollbar(cols_-2,contentY,contentY+innerH-1,logScroll_,innerH,(int)log.size());
  }

  // ── Detail View ────────────────────────────────────────────────────────────
  void renderDetailView(const Thresholds &th) {
    if(selectedIdx_>=(int)sortedHosts_.size()) return;
    const auto &host=sortedHosts_[selectedIdx_];
    int hostCount=(int)sortedHosts_.size();
    attron(COLOR_PAIR(C_BOX)|A_BOLD);
    mvaddstr(0,0,BOX_TL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_TR);
    mvaddstr(rows_-1,0,BOX_BL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_BR);
    for(int r=1;r<rows_-1;r++){ mvaddstr(r,0,BOX_V); mvaddstr(r,cols_-1,BOX_V); }
    attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    int y=1;
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    for(int i=1;i<cols_-1;i++) mvaddch(y,i,' ');
    attron(COLOR_PAIR(C_TIME)); mvaddstr(y,2,fmtTime(time(nullptr)).c_str()); attroff(COLOR_PAIR(C_TIME));
    std::string dtitle=std::string(SYM_DIAMOND)+" HOST DETAIL: "+host.name+" "+SYM_DIAMOND;
    int dtx=(cols_-(int)dtitle.size())/2;
    if(dtx>12) mvaddstr(y,dtx,dtitle.c_str());
    const char *dhint="[Tab]Next [U]Theme [Esc]Back [Q]Quit";
    int dhLen=(int)strlen(dhint);
    if(cols_-dhLen-3>0) mvaddstr(y,cols_-dhLen-2,dhint);
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(2); attroff(COLOR_PAIR(C_BOX)|A_BOLD);

    if(host.status==HostStatus::OFFLINE||host.status==HostStatus::STALE) {
      int sc=(host.status==HostStatus::STALE)?C_STALE:C_GRAY;
      attron(COLOR_PAIR(sc)|A_DIM);
      std::string statLbl=(host.status==HostStatus::STALE)?"STALE":"OFFLINE";
      std::string offMsg="--- "+host.name+" is "+statLbl;
      if(host.lastSeen>0) {
        time_t age=time(nullptr)-host.lastSeen;
        char ageBuf[32]; snprintf(ageBuf,sizeof(ageBuf)," (last seen %lds ago) ---",(long)age);
        offMsg+=ageBuf;
      } else offMsg+=" ---";
      mvaddstr(rows_/2,(cols_-(int)offMsg.size())/2,offMsg.c_str());
      attroff(COLOR_PAIR(sc)|A_DIM);
      renderDetailFooter(hostCount); return;
    }
    int curY=3, graphW=cols_-4; if(graphW<10) graphW=10;

    // CPU Sparkline
    {
      std::vector<float> cpuH; for(auto &s:host.history) cpuH.push_back(s.cpu);
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD); curY++;
      if(curY<rows_-2) { drawMetricTitle(curY,"CPU",host.cpu,host.name,th,'c'); curY++; }
      if(curY<rows_-2) { drawSparkline(curY,2,graphW,1,cpuH,'c',th); curY++; }
    }
    // Per-core
    if(curY<rows_-2&&!host.cores.empty()) {
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
      char coreTitle[48]; snprintf(coreTitle,sizeof(coreTitle)," PROCESSORS (%d cores)  LOAD:%.2f  PROCS:%d ",
                                   (int)host.cores.size(),host.loadAvg,host.procCount);
      attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(curY,3,coreTitle); attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
      curY++;
      int barMaxW=cols_-24; if(barMaxW<10) barMaxW=10;
      int reserveBelow=12, availCoreRows=rows_-curY-reserveBelow-1;
      if(availCoreRows<2) availCoreRows=2;
      int totalCores=(int)host.cores.size();
      int maxCoreScroll=std::max(0,totalCores-availCoreRows);
      histScroll_=std::clamp(histScroll_,0,maxCoreScroll);
      int startCore=histScroll_, showCores=std::min(totalCores-startCore,availCoreRows);
      for(int ci=0;ci<showCores;ci++) {
        int coreIdx=startCore+ci; if(curY>=rows_-reserveBelow) break;
        float val=host.cores[coreIdx];
        int co=pctColor(val,host.name,th,'c');
        attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        char lbl[24]; snprintf(lbl,sizeof(lbl)," core%2d ",coreIdx);
        mvaddstr(curY,2,lbl); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        int barStart=10, barW=barMaxW;
        int filled=std::clamp((int)(val/100.0f*barW),0,barW);
        attron(COLOR_PAIR(co)|A_BOLD); move(curY,barStart);
        for(int b=0;b<filled;b++) addstr(BLOCK_FULL);
        attroff(COLOR_PAIR(co)|A_BOLD);
        attron(COLOR_PAIR(C_GRAY)|A_DIM);
        for(int b=filled;b<barW;b++) addstr(BLOCK_EMPTY);
        attroff(COLOR_PAIR(C_GRAY)|A_DIM);
        attron(COLOR_PAIR(co)|A_BOLD);
        char pctBuf[12]; snprintf(pctBuf,sizeof(pctBuf)," %5.1f%%",val); addstr(pctBuf);
        attroff(COLOR_PAIR(co)|A_BOLD);
        curY++;
      }
      if(totalCores>availCoreRows) drawScrollbar(cols_-2,curY-showCores,curY-1,histScroll_,availCoreRows,totalCores);
    }
    // Network panel
    if(curY<rows_-7) {
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
      attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(curY,3," NETWORK "); attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
      curY++;
      if(curY<rows_-6) {
        attron(COLOR_PAIR(C_GREEN)|A_BOLD);
        mvprintw(curY,4,"▲ TX: %7.1f KB/s",host.netTxKB);
        attron(COLOR_PAIR(C_CYAN)|A_BOLD);
        mvprintw(curY,26,"▼ RX: %7.1f KB/s",host.netRxKB);
        attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
        curY++;
      }
    }
    // RAM
    if(curY<rows_-5) {
      std::vector<float> ramH; for(auto &s:host.history) ramH.push_back(s.ram);
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD); curY++;
      if(curY<rows_-4) { drawMetricTitle(curY,"RAM",host.ram,host.name,th,'r'); curY++; }
      if(curY<rows_-3) { drawSparkline(curY,2,graphW,1,ramH,'r',th); curY++; }
    }
    // DISK
    if(curY<rows_-4) {
      std::vector<float> diskH; for(auto &s:host.history) diskH.push_back(s.disk);
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD); curY++;
      if(curY<rows_-3) { drawMetricTitle(curY,"DISK",host.disk,host.name,th,'d'); curY++; }
      if(curY<rows_-2) { drawSparkline(curY,2,graphW,1,diskH,'d',th); curY++; }
    }
    // Host info
    if(curY<rows_-2) {
      attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(curY); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
      attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(curY,3," HOST INFO "); attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
      curY++;
      int lc=2, rc=cols_/2;
      if(curY<rows_-1) {
        attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(curY,lc,"Name: "); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        attron(COLOR_PAIR(C_CYAN)); addstr(host.name.c_str()); attroff(COLOR_PAIR(C_CYAN));
        const char *sym,*slbl; int sc;
        switch(host.status) {
        case HostStatus::ALERT:   sc=C_RED;    sym=SYM_ONLINE; slbl=" ALERT"; break;
        case HostStatus::WARNING: sc=C_YELLOW; sym=SYM_WARN;   slbl=" WARN"; break;
        case HostStatus::STALE:   sc=C_STALE;  sym="◌";        slbl=" STALE"; break;
        default:                  sc=C_GREEN;  sym=SYM_ONLINE; slbl=" OK"; break;
        }
        attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(curY,rc,"Status: "); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        attron(COLOR_PAIR(sc)|A_BOLD); addstr(sym); addstr(slbl); attroff(COLOR_PAIR(sc)|A_BOLD);
        curY++;
      }
      if(curY<rows_-1) {
        attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(curY,lc,"IP:   "); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        attron(COLOR_PAIR(C_GRAY)); addstr(host.ip.c_str()); attroff(COLOR_PAIR(C_GRAY));
        attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(curY,rc,"Cores: "); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
        attron(COLOR_PAIR(C_CYAN)); printw("%d",host.coreCount); attroff(COLOR_PAIR(C_CYAN));
        int tsCol=rc+16;
        if(tsCol+20<cols_) {
          attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD); mvprintw(curY,tsCol,"Last: "); attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
          attron(COLOR_PAIR(C_CYAN)); addstr(fmtTime(host.lastSeen).c_str()); attroff(COLOR_PAIR(C_CYAN));
        }
      }
    }
    renderDetailFooter(hostCount);
  }

  void drawMetricTitle(int y, const char *label, float val,
                       const std::string &hostname, const Thresholds &th, char metric) {
    int titleColor=metric=='c'?C_CYAN:metric=='r'?C_GREEN:C_MAGENTA;
    attron(COLOR_PAIR(C_BOX)|A_BOLD); mvaddstr(y,2,LINE_H); addstr(" "); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    attron(COLOR_PAIR(titleColor)|A_BOLD); addstr(label); attroff(COLOR_PAIR(titleColor)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); addstr(" ");
    int cur=getcurx(stdscr);
    char pctBuf[16]; snprintf(pctBuf,sizeof(pctBuf)," %5.1f%% ",val);
    int pctPos=cols_-(int)strlen(pctBuf)-3;
    for(int i=cur;i<pctPos;i++) addstr(LINE_H);
    attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    int co=pctColor(val,hostname,th,metric);
    attron(COLOR_PAIR(co)|A_BOLD); addstr(pctBuf); attroff(COLOR_PAIR(co)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); addstr(LINE_H); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
  }

  void renderDetailFooter(int hostCount) {
    char fi[32]; snprintf(fi,sizeof(fi)," %d/%d ",selectedIdx_+1,hostCount);
    attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(rows_-1,cols_-(int)strlen(fi)-2,fi); attroff(COLOR_PAIR(C_CYAN)|A_BOLD);
  }

  void renderCmdBar() {
    int y=rows_-1;
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    for(int i=0;i<cols_;i++) mvaddch(y,i,' ');
    mvaddstr(y,1,"> /"); addstr(cmdBuf_.c_str()); addstr("_");
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
  }

  void renderCmdError() {
    int y=rows_-1, len=(int)cmdError_.size()+4;
    int x=(cols_-len)/2; if(x<0) x=0;
    attron(COLOR_PAIR(C_RED)|A_BOLD|A_BLINK);
    mvaddstr(y,x,(" "+cmdError_+" ").c_str());
    attroff(COLOR_PAIR(C_RED)|A_BOLD|A_BLINK);
  }

  // ── Help View ──────────────────────────────────────────────────────────────
  void renderHelp() {
    attron(COLOR_PAIR(C_BOX)|A_BOLD);
    mvaddstr(0,0,BOX_TL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_TR);
    mvaddstr(rows_-1,0,BOX_BL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_BR);
    for(int r=1;r<rows_-1;r++){mvaddstr(r,0,BOX_V);mvaddstr(r,cols_-1,BOX_V);}
    attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    int y=1;
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    for(int i=1;i<cols_-1;i++) mvaddch(y,i,' ');
    std::string htitle=std::string(SYM_DIAMOND)+" HELP "+SYM_DIAMOND;
    mvaddstr(y,(cols_-(int)htitle.size())/2,htitle.c_str());
    mvaddstr(y,cols_-24,"[U] Theme  [Esc] Close");
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(2); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    y=4; int lx=4, rx=28;
    attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(y++,lx,"NAVIGATION"); attroff(COLOR_PAIR(C_CYAN)|A_BOLD); y++;
    struct HL { const char *key, *desc; };
    HL nav[]={{"Tab","Enter detail view / next host"},{"Shift+Tab","Previous host"},
               {"U / Ctrl+U","Cycle UI theme (6 themes)"},{"Esc","Back to overview"},
               {"↑ ↓","Scroll"},{"PgUp/PgDn","Scroll fast"},{"Q","Quit"},{"/","Open command bar"}};
    for(auto &h:nav){
      if(y>=rows_-6) break;
      attron(COLOR_PAIR(C_GREEN)|A_BOLD); mvprintw(y,lx,"%-16s",h.key); attroff(COLOR_PAIR(C_GREEN)|A_BOLD);
      attron(COLOR_PAIR(C_WHITE_BD)); mvaddstr(y,rx,h.desc); attroff(COLOR_PAIR(C_WHITE_BD));
      y++;
    }
    y+=2;
    attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(y++,lx,"THEMES"); attroff(COLOR_PAIR(C_CYAN)|A_BOLD); y++;
    for(int i=0;i<NUM_THEMES;i++) {
      if(y>=rows_-2) break;
      bool cur=(i==uiMode_);
      attron(COLOR_PAIR(cur?C_YELLOW:C_WHITE_BD)|(cur?A_BOLD:0));
      mvprintw(y++,lx,"%d. %s%s",i+1,THEME_NAMES[i],cur?" (active)":"");
      attroff(COLOR_PAIR(cur?C_YELLOW:C_WHITE_BD));
    }
    y+=2;
    attron(COLOR_PAIR(C_CYAN)|A_BOLD); mvaddstr(y++,lx,"COMMANDS"); attroff(COLOR_PAIR(C_CYAN)|A_BOLD); y++;
    HL cmds[]={{"/help","Show this help"},{"/viewer <host>","Jump to host detail"},{"/history <host>","Show host history"}};
    for(auto &h:cmds){
      if(y>=rows_-2) break;
      attron(COLOR_PAIR(C_YELLOW)|A_BOLD); mvprintw(y,lx,"%-22s",h.key); attroff(COLOR_PAIR(C_YELLOW)|A_BOLD);
      attron(COLOR_PAIR(C_WHITE_BD)); mvaddstr(y,rx,h.desc); attroff(COLOR_PAIR(C_WHITE_BD));
      y++;
    }
  }

  // ── History View ────────────────────────────────────────────────────────────
  void renderHistoryView() {
    if(selectedIdx_>=(int)sortedHosts_.size()) return;
    const auto &host=sortedHosts_[selectedIdx_];
    attron(COLOR_PAIR(C_BOX)|A_BOLD);
    mvaddstr(0,0,BOX_TL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_TR);
    mvaddstr(rows_-1,0,BOX_BL); for(int i=1;i<cols_-1;i++) addstr(BOX_H); addstr(BOX_BR);
    for(int r=1;r<rows_-1;r++){mvaddstr(r,0,BOX_V);mvaddstr(r,cols_-1,BOX_V);}
    attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    int y=1;
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    for(int i=1;i<cols_-1;i++) mvaddch(y,i,' ');
    attron(COLOR_PAIR(C_TIME)); mvaddstr(y,2,fmtTime(time(nullptr)).c_str()); attroff(COLOR_PAIR(C_TIME));
    std::string htitle=std::string(SYM_DIAMOND)+" HISTORY: "+host.name+" "+SYM_DIAMOND;
    mvaddstr(y,(cols_-(int)htitle.size())/2,htitle.c_str());
    mvaddstr(y,cols_-14,"[Esc] Back");
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(2); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    if(host.status==HostStatus::OFFLINE||host.status==HostStatus::STALE) {
      int sc=(host.status==HostStatus::STALE)?C_STALE:C_GRAY;
      attron(COLOR_PAIR(sc)|A_DIM);
      std::string msg="--- "+host.name+" is "+(host.status==HostStatus::STALE?"STALE":"OFFLINE")+" ---";
      mvaddstr(rows_/2,(cols_-(int)msg.size())/2,msg.c_str());
      attroff(COLOR_PAIR(sc)|A_DIM);
      if(!host.history.empty()) {
        // Still show history even if stale
      } else return;
    }
    y=3;
    int cTime=12,cCpu=10,cRam=10,cDisk=10,cLoad=10;
    attron(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
    mvprintw(y,4,"%-*s",cTime,"TIME");
    mvprintw(y,4+cTime,"%-*s",cCpu,"CPU%");
    mvprintw(y,4+cTime+cCpu,"%-*s",cRam,"RAM%");
    mvprintw(y,4+cTime+cCpu+cRam,"%-*s",cDisk,"DISK%");
    mvprintw(y,4+cTime+cCpu+cRam+cDisk,"%-*s",cLoad,"LOAD");
    if(cols_>80) mvprintw(y,4+cTime+cCpu+cRam+cDisk+cLoad,"NET RX/TX KB/s");
    attroff(COLOR_PAIR(C_WHITE_BD)|A_BOLD);
    attron(COLOR_PAIR(C_BOX)|A_BOLD); drawHSep(4); attroff(COLOR_PAIR(C_BOX)|A_BOLD);
    int dataY=5, innerH=rows_-dataY-1; if(innerH<1) return;
    int total=(int)host.history.size();
    int maxScroll=std::max(0,total-innerH);
    histScroll_=std::clamp(histScroll_,0,maxScroll);
    for(int i=0;i<innerH&&i<total;i++) {
      int idx=total-1-i-histScroll_; if(idx<0) break;
      const auto &s=host.history[idx]; int ry=dataY+i; if(ry>=rows_-1) break;
      attron(COLOR_PAIR(C_CYAN)); mvprintw(ry,4,"%-*s",cTime,fmtTime(s.ts).c_str()); attroff(COLOR_PAIR(C_CYAN));
      auto dP=[&](float val,char m,int col){
        int co=pctColor(val,host.name,Thresholds{},m);
        attron(COLOR_PAIR(co)|A_BOLD); mvprintw(ry,col,"%6.1f%%",val); attroff(COLOR_PAIR(co)|A_BOLD);
      };
      dP(s.cpu,'c',4+cTime); dP(s.ram,'r',4+cTime+cCpu); dP(s.disk,'d',4+cTime+cCpu+cRam);
      int lc=s.loadAvg>=4?C_RED:s.loadAvg>=2?C_YELLOW:C_OK_DIM;
      attron(COLOR_PAIR(lc)); mvprintw(ry,4+cTime+cCpu+cRam+cDisk,"%.2f",s.loadAvg); attroff(COLOR_PAIR(lc));
      if(cols_>80) {
        attron(COLOR_PAIR(C_GREEN)); mvprintw(ry,4+cTime+cCpu+cRam+cDisk+cLoad,"▲%-6.1f",s.netTxKB); attroff(COLOR_PAIR(C_GREEN));
        attron(COLOR_PAIR(C_CYAN)); mvprintw(ry,4+cTime+cCpu+cRam+cDisk+cLoad+8,"▼%-6.1f",s.netRxKB); attroff(COLOR_PAIR(C_CYAN));
      }
    }
    if(total>innerH) drawScrollbar(cols_-2,dataY,dataY+innerH-1,histScroll_,innerH,total);
    char cnt[32]; snprintf(cnt,sizeof(cnt)," %d samples ",total);
    attron(COLOR_PAIR(C_GRAY)|A_DIM); mvaddstr(rows_-1,cols_-(int)strlen(cnt)-2,cnt); attroff(COLOR_PAIR(C_GRAY)|A_DIM);
  }
};

} // namespace monitor::ui
