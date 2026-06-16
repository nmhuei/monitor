#pragma once
#include <string>
#include <vector>
#include "../protocol.hpp"

namespace monitor::ui {

enum class Screen {
  Overview,
  HostDetail,
  Events,
  Help
};

enum class FocusPane {
  Groups,
  HostTable,
  Detail,
  Events,
  Command
};

enum class SortKey {
  Status,
  Host,
  Cpu,
  Ram,
  Disk,
  Load,
  Age
};

struct HostFilter {
  std::string search;
  std::string status;
  std::string role;
  std::string env;
  std::string cluster;
  
  void clear() {
    search.clear();
    status.clear();
    role.clear();
    env.clear();
    cluster.clear();
  }
};

struct DashboardState {
  Screen screen = Screen::Overview;
  FocusPane focus = FocusPane::HostTable;

  int selectedHost = 0;
  int hostScroll = 0;
  int eventScroll = 0;
  int helpScroll = 0;

  SortKey sortKey = SortKey::Status;
  bool sortDesc = true;

  HostFilter filter;

  bool commandActive = false;
  std::string commandBuffer;
  std::string commandError;
  int commandErrorTimer = 0;

  int themeId = 0;
  bool showEventPreview = true;
  int activeTab = 0; // 0: Metrics, 1: Cores, 2: History, 3: Events
};

struct Rect {
  int y = 0;
  int x = 0;
  int h = 0;
  int w = 0;
};

struct Size {
  int rows = 0;
  int cols = 0;
};

enum class LayoutKind {
  Compact,
  Standard,
  Wide
};

struct Layout {
  LayoutKind kind = LayoutKind::Standard;
  Rect topBar;
  Rect statusBar;
  Rect leftPane;
  Rect mainPane;
  Rect rightPane;
  Rect eventPane;
  bool showLeft = false;
  bool showRight = false;
  bool showEvents = false;
};

} // namespace monitor::ui
