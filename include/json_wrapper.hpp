#pragma once
#include "third_party/json.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace monitor::json {

struct Value {
  std::string str;
  double num = 0.0;
  bool is_str = false;
  bool is_arr = false;
  std::vector<double> arr;
};

using Object = std::unordered_map<std::string, Value>;

inline std::string encode(const std::string &host, float cpu, float ram,
                          float disk, time_t ts,
                          const std::vector<float> &cores = {},
                          float netRxKB = 0, float netTxKB = 0,
                          float loadAvg = 0, int procCount = 0) {
  nlohmann::json j;
  j["host"] = host;
  j["cpu"] = cpu;
  j["ram"] = ram;
  j["disk"] = disk;
  j["timestamp"] = static_cast<long long>(ts);
  j["net_rx"] = netRxKB;
  j["net_tx"] = netTxKB;
  j["load_avg"] = loadAvg;
  j["proc_count"] = procCount;
  if (!cores.empty()) {
    j["cores"] = cores;
  }
  return j.dump();
}

inline Object decode(const std::string &jsonStr) {
  Object obj;
  try {
    auto j = nlohmann::json::parse(jsonStr);
    if (!j.is_object()) {
      return obj;
    }
    for (auto& [key, val] : j.items()) {
      Value v;
      if (val.is_string()) {
        v.is_str = true;
        v.str = val.get<std::string>();
      } else if (val.is_number()) {
        v.num = val.get<double>();
      } else if (val.is_boolean()) {
        v.num = val.get<bool>() ? 1.0 : 0.0;
      } else if (val.is_array()) {
        v.is_arr = true;
        for (auto& item : val) {
          if (item.is_number()) {
            v.arr.push_back(item.get<double>());
          }
        }
      }
      obj[key] = v;
    }
  } catch (...) {
    // Return empty or partial
  }
  return obj;
}

} // namespace monitor::json
