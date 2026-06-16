#pragma once
/*
 * dashboard.hpp — Ops Console TUI
 * Completely remade UX/UI with C++20 and modular widget architecture.
 * Implements: Screen Router + Focus Stack + Widget-based rendering.
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
#include <memory>

// 1. Core definitions & logic
#include "ui/ui_types.hpp"
#include "ui/theme.hpp"
#include "ui/format.hpp"
#include "ui/layout.hpp"
#include "ui/command_registry.hpp"

// 2. Pure widgets
#include "ui/widgets/panel.hpp"
#include "ui/widgets/sparkline.hpp"
#include "ui/widgets/top_bar.hpp"
#include "ui/widgets/status_bar.hpp"
#include "ui/widgets/host_table.hpp"
#include "ui/widgets/event_list.hpp"
#include "ui/widgets/metric_card.hpp"
#include "ui/widgets/help_view.hpp"

// 3. Polymorphic Screen Base and Layout Assemblies
#include "ui/screens/iscreen.hpp"
#include "ui/screens/overview_screen.hpp"
#include "ui/screens/host_detail_screen.hpp"
#include "ui/screens/events_screen.hpp"
#include "ui/screens/help_screen.hpp"

namespace monitor::ui {

// ============================================================
// SCREEN ROUTER (btop-style core engine)
// ============================================================
class ScreenRouter {
public:
  ScreenRouter() {
    overview_   = std::make_unique<OverviewScreen>();
    hostDetail_ = std::make_unique<HostDetailScreen>();
    events_     = std::make_unique<EventsScreen>();
    help_       = std::make_unique<HelpScreen>();
  }

  void render(const Layout& layout,
              const std::vector<HostState>& hosts,
              const std::vector<LogEvent>& log,
              DashboardState& state,
              const Thresholds& thresholds,
              const std::unordered_map<std::string, time_t>& firstSeen,
              std::vector<HostState>& outFilteredSorted) {

    switch (state.screen) {
      case Screen::Overview:
        overview_->render(layout, hosts, log, state, thresholds, firstSeen, outFilteredSorted);
        break;

      case Screen::HostDetail:
        hostDetail_->render(layout, hosts, log, state, thresholds, firstSeen, outFilteredSorted);
        break;

      case Screen::Events:
        events_->render(layout, hosts, log, state, thresholds, firstSeen, outFilteredSorted);
        break;

      case Screen::Help:
        help_->render(layout, hosts, log, state, thresholds, firstSeen, outFilteredSorted);
        break;
    }
  }

  void handleInput(int key,
                   DashboardState& state,
                   const std::vector<HostState>& hosts,
                   const std::vector<LogEvent>& log) {

    switch (state.screen) {
      case Screen::Overview:
        overview_->handleInput(key, state, hosts, log);
        break;

      case Screen::HostDetail:
        hostDetail_->handleInput(key, state, hosts, log);
        break;

      case Screen::Events:
        events_->handleInput(key, state, hosts, log);
        break;

      case Screen::Help:
        help_->handleInput(key, state, hosts, log);
        break;
    }
  }

private:
  std::unique_ptr<IScreen> overview_;
  std::unique_ptr<IScreen> hostDetail_;
  std::unique_ptr<IScreen> events_;
  std::unique_ptr<IScreen> help_;
};

// ============================================================
// INPUT ROUTER (btop-style keyboard system)
// ============================================================
class InputRouter {
public:
  void handle(int key,
              DashboardState& state,
              ScreenRouter& router,
              const std::vector<HostState>& hosts,
              const std::vector<LogEvent>& log) {

    // 1. Command mode active
    if (state.commandActive) {
      handleCmdKey(key, state, hosts);
      return;
    }

    // 2. Global hotkeys
    if (key == 'q' || key == 'Q') {
      if (!isendwin()) endwin();
      exit(0);
    }
    
    if (key == '/') {
      state.commandActive = true;
      state.commandBuffer = "/";
      return;
    }
    
    if (key == 'f' || key == 'F') {
      state.commandActive = true;
      state.commandBuffer = "/filter ";
      return;
    }

    if (key == 'u' || key == 'U') {
      state.themeId = (state.themeId + 1) % NUM_THEMES;
      initColors(state.themeId);
      return;
    }

    // 3. Delegate specific navigation to screen router
    router.handleInput(key, state, hosts, log);
  }

private:
  void handleCmdKey(int ch, DashboardState& state, const std::vector<HostState>& hosts) {
    if (ch == 27) { // Esc
      state.commandActive = false;
      state.commandBuffer.clear();
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      if (!state.commandBuffer.empty()) {
        auto res = CommandRegistry::execute(state.commandBuffer, state, hosts);
        if (!res.ok) {
          state.commandError = res.message;
          state.commandErrorTimer = 3;
        } else {
          state.commandError.clear();
          state.commandErrorTimer = 0;
        }
      }
      state.commandActive = false;
      state.commandBuffer.clear();
      return;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (!state.commandBuffer.empty()) {
        state.commandBuffer.pop_back();
      }
      return;
    }
    if (ch == 9) { // Tab autocomplete
      auto suggestions = CommandRegistry::suggest(state.commandBuffer, hosts);
      if (!suggestions.empty()) {
        state.commandBuffer = suggestions[0];
      }
      return;
    }
    if (ch >= 32 && ch < 127) {
      state.commandBuffer += static_cast<char>(ch);
    }
  }
};

// ============================================================
// DASHBOARD APP (FINAL ENTRY POINT)
// ============================================================
class Dashboard {
public:
  Dashboard() {
    startTime_ = time(nullptr);
  }

  ~Dashboard() {
    teardown();
  }

  void init() {
    setlocale(LC_ALL, "");
    initscr(); noecho(); cbreak(); curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    initColors(state_.themeId);
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

    // Drain and route all pending input
    int ch;
    while ((ch = getch()) != ERR) {
      inputRouter_.handle(ch, state_, screenRouter_, hosts, log);
    }

    // Process errors timers
    if (state_.commandErrorTimer > 0) {
      state_.commandErrorTimer--;
    }

    erase();

    // Compute layout
    Size size = {rows_, cols_};
    Layout layout = LayoutEngine::compute(size, state_);

    // Draw Top Bar
    TopBar::draw(layout.topBar, hosts, state_);

    // Render screen
    std::vector<HostState> filteredHosts;
    if (state_.screen == Screen::HostDetail) {
      for (const auto& h : hosts) {
        if (matchesFilter(h, state_.filter)) {
          filteredHosts.push_back(h);
        }
      }
      HostComparator comp{state_.sortKey, state_.sortDesc};
      std::stable_sort(filteredHosts.begin(), filteredHosts.end(), comp);
      
      screenRouter_.render(layout, filteredHosts, log, state_, thresh, firstSeen_, filteredHosts);
    } else {
      screenRouter_.render(layout, hosts, log, state_, thresh, firstSeen_, filteredHosts);
    }

    // Draw Status Bar
    StatusBar::draw(layout.statusBar, state_, hosts);

    refresh();
  }

private:
  void renderTooSmall() {
    erase();
    attron(COLOR_PAIR(CP_ALERT) | A_BOLD);
    mvaddstr(rows_ / 2, (cols_ - 30) / 2, "Terminal size too small!");
    attroff(COLOR_PAIR(CP_ALERT) | A_BOLD);
    
    attron(COLOR_PAIR(CP_NORMAL));
    mvprintw(rows_ / 2 + 1, (cols_ - 36) / 2, "Current: %dx%d. Required: >=80x24.", cols_, rows_);
    mvaddstr(rows_ / 2 + 2, (cols_ - 18) / 2, "Press [Q] to quit.");
    attroff(COLOR_PAIR(CP_NORMAL));
    refresh();
  }

private:
  int rows_ = 0;
  int cols_ = 0;
  time_t startTime_;
  DashboardState state_;
  std::unordered_map<std::string, time_t> firstSeen_;
  
  ScreenRouter screenRouter_;
  InputRouter inputRouter_;
};

} // namespace monitor::ui
