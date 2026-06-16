#pragma once
#include <vector>
#include <unordered_map>
#include <ctime>
#include "../ui_types.hpp"
#include "../../metrics_store.hpp"
#include "../../thresholds.hpp"

namespace monitor::ui {

class IScreen {
public:
  virtual void render(const Layout& layout,
                      const std::vector<HostState>& hosts,
                      const std::vector<LogEvent>& log,
                      DashboardState& state,
                      const Thresholds& thresholds,
                      const std::unordered_map<std::string, time_t>& firstSeen,
                      std::vector<HostState>& outFilteredSorted) = 0;

  virtual void handleInput(int key,
                           DashboardState& state,
                           const std::vector<HostState>& hosts,
                           const std::vector<LogEvent>& log) = 0;

  virtual ~IScreen() = default;
};

} // namespace monitor::ui
