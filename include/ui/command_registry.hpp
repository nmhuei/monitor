#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include "ui_types.hpp"
#include "theme.hpp"

namespace monitor::ui {

struct CommandResult {
  bool ok = false;
  std::string message;
};

inline std::vector<std::string> splitTokens(const std::string &s) {
  std::vector<std::string> tokens;
  std::stringstream ss(s);
  std::string token;
  while (ss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

class CommandRegistry {
public:
  static CommandResult execute(const std::string &line, DashboardState &state, const std::vector<HostState> &hosts) {
    if (line.empty() || line[0] != '/') {
      return {false, "Invalid command. Must start with '/'"};
    }
    
    auto tokens = splitTokens(line);
    if (tokens.empty()) return {false, "Empty command"};
    
    std::string cmd = tokens[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    
    if (cmd == "/help") {
      state.screen = Screen::Help;
      return {true, "Switched to Help"};
    }
    else if (cmd == "/events") {
      state.screen = Screen::Events;
      return {true, "Switched to Events"};
    }
    else if (cmd == "/log" && tokens.size() >= 2 && tokens[1] == "clear") {
      state.eventScroll = 0;
      return {true, "Logs scrolled to bottom"};
    }
    else if (cmd == "/host" && tokens.size() >= 2) {
      std::string target = tokens[1];
      for (size_t i = 0; i < hosts.size(); ++i) {
        if (hosts[i].name == target) {
          state.selectedHost = i;
          state.screen = Screen::HostDetail;
          state.activeTab = 0;
          return {true, "Host selected: " + target};
        }
      }
      for (size_t i = 0; i < hosts.size(); ++i) {
        if (hosts[i].name.find(target) != std::string::npos) {
          state.selectedHost = i;
          state.screen = Screen::HostDetail;
          state.activeTab = 0;
          return {true, "Host matched: " + hosts[i].name};
        }
      }
      return {false, "Host not found: " + target};
    }
    else if (cmd == "/history" && tokens.size() >= 2) {
      std::string target = tokens[1];
      for (size_t i = 0; i < hosts.size(); ++i) {
        if (hosts[i].name == target || hosts[i].name.find(target) != std::string::npos) {
          state.selectedHost = i;
          state.screen = Screen::HostDetail;
          state.activeTab = 2;
          return {true, "Opened history for: " + hosts[i].name};
        }
      }
      return {false, "Host not found: " + target};
    }
    else if (cmd == "/theme" && tokens.size() >= 2) {
      std::string tName = tokens[1];
      std::transform(tName.begin(), tName.end(), tName.begin(), ::toupper);
      for (int i = 0; i < NUM_THEMES; ++i) {
        if (std::string(THEME_NAMES[i]) == tName) {
          state.themeId = i;
          initColors(i);
          return {true, "Theme changed to " + tName};
        }
      }
      return {false, "Theme not found: " + tokens[1]};
    }
    else if (cmd == "/layout" && tokens.size() >= 2) {
      std::string lKind = tokens[1];
      std::transform(lKind.begin(), lKind.end(), lKind.begin(), ::tolower);
      if (lKind == "compact") {
        return {true, "Compact layout requested"};
      }
      return {false, "Unknown layout: " + tokens[1]};
    }
    else if (cmd == "/sort" && tokens.size() >= 2) {
      std::string key = tokens[1];
      std::transform(key.begin(), key.end(), key.begin(), ::tolower);
      
      bool desc = true;
      if (tokens.size() >= 3) {
        std::string dir = tokens[2];
        std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
        if (dir == "asc") desc = false;
      }
      
      state.sortDesc = desc;
      if (key == "status") { state.sortKey = SortKey::Status; return {true, "Sorted by status"}; }
      if (key == "host" || key == "name") { state.sortKey = SortKey::Host; return {true, "Sorted by host name"}; }
      if (key == "cpu") { state.sortKey = SortKey::Cpu; return {true, "Sorted by CPU"}; }
      if (key == "ram") { state.sortKey = SortKey::Ram; return {true, "Sorted by RAM"}; }
      if (key == "disk") { state.sortKey = SortKey::Disk; return {true, "Sorted by Disk"}; }
      if (key == "load") { state.sortKey = SortKey::Load; return {true, "Sorted by LoadAvg"}; }
      if (key == "age" || key == "seen") { state.sortKey = SortKey::Age; return {true, "Sorted by last seen"}; }
      
      return {false, "Invalid sort key: " + tokens[1]};
    }
    else if (cmd == "/filter" && tokens.size() >= 2) {
      std::string filterArg = tokens[1];
      if (filterArg == "clear") {
        state.filter.clear();
        return {true, "Filters cleared"};
      }
      
      auto eq = filterArg.find('=');
      if (eq == std::string::npos) {
        state.filter.search = filterArg;
        return {true, "Search filter set to: " + filterArg};
      }
      
      std::string key = filterArg.substr(0, eq);
      std::string val = filterArg.substr(eq + 1);
      std::transform(key.begin(), key.end(), key.begin(), ::tolower);
      std::transform(val.begin(), val.end(), val.begin(), ::tolower);
      
      if (key == "status") { state.filter.status = val; return {true, "Filter status=" + val}; }
      if (key == "role") { state.filter.role = val; return {true, "Filter role=" + val}; }
      if (key == "env") { state.filter.env = val; return {true, "Filter env=" + val}; }
      if (key == "cluster") { state.filter.cluster = val; return {true, "Filter cluster=" + val}; }
      
      return {false, "Unknown filter field: " + key};
    }
    
    return {false, "Unknown command: " + tokens[0]};
  }

  static std::vector<std::string> suggest(const std::string &partial, const std::vector<HostState> &hosts) {
    std::vector<std::string> suggestions;
    static const std::vector<std::string> rootCommands = {
      "/help", "/events", "/host ", "/history ", "/filter ", "/sort ", "/theme ", "/log clear"
    };
    
    if (partial.empty()) return rootCommands;
    
    if (partial[0] == '/') {
      auto space = partial.find(' ');
      if (space == std::string::npos) {
        for (const auto &cmd : rootCommands) {
          if (cmd.compare(0, partial.size(), partial) == 0) {
            suggestions.push_back(cmd);
          }
        }
        return suggestions;
      }
      
      std::string cmd = partial.substr(0, space);
      std::string arg = partial.substr(space + 1);
      std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
      
      if (cmd == "/host" || cmd == "/history") {
        for (const auto &h : hosts) {
          if (h.name.compare(0, arg.size(), arg) == 0) {
            suggestions.push_back(cmd + " " + h.name);
          }
        }
      }
      else if (cmd == "/theme") {
        for (int i = 0; i < NUM_THEMES; ++i) {
          std::string tName = THEME_NAMES[i];
          std::transform(tName.begin(), tName.end(), tName.begin(), ::tolower);
          if (tName.compare(0, arg.size(), arg) == 0) {
            suggestions.push_back(cmd + " " + tName);
          }
        }
      }
      else if (cmd == "/sort") {
        static const std::vector<std::string> sortKeys = {
          "status", "host", "cpu", "ram", "disk", "load", "age"
        };
        for (const auto &key : sortKeys) {
          if (key.compare(0, arg.size(), arg) == 0) {
            suggestions.push_back(cmd + " " + key);
          }
        }
      }
      else if (cmd == "/filter") {
        static const std::vector<std::string> filters = {
          "clear", "status=online", "status=warning", "status=alert", "status=stale", "status=offline",
          "role=web", "role=db", "role=api", "env=prod", "env=test", "cluster=default"
        };
        for (const auto &filt : filters) {
          if (filt.compare(0, arg.size(), arg) == 0) {
            suggestions.push_back(cmd + " " + filt);
          }
        }
      }
    }
    
    return suggestions;
  }
};

} // namespace monitor::ui
