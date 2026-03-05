#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace monitor {

// Wire protocol: [4-byte length (network order)][JSON payload]
static constexpr uint16_t DEFAULT_PORT = 8784;
static constexpr int RECONNECT_INTERVAL_SEC = 5;
static constexpr int MAX_HISTORY = 60; // graph samples
static constexpr int MAX_LOG_ENTRIES = 500;

struct MetricPayload {
  std::string host;
  float cpu = 0.0f;
  float ram = 0.0f;
  float disk = 0.0f;
  time_t timestamp = 0;
  std::string ip;
  std::vector<float> cores; // per-core CPU %
};

enum class HostStatus { ONLINE, WARNING, ALERT, OFFLINE };

inline const char *statusStr(HostStatus s) {
  switch (s) {
  case HostStatus::ONLINE:
    return "OK";
  case HostStatus::WARNING:
    return "WARN";
  case HostStatus::ALERT:
    return "ALERT";
  case HostStatus::OFFLINE:
    return "OFFLINE";
  }
  return "UNKNOWN";
}

} // namespace monitor
