#pragma once
/*
 * Minimal JSON encoder/decoder for monitor metrics.
 * No external dependencies required.
 */
#include <string>
#include <unordered_map>
#include <sstream>
#include <stdexcept>

namespace monitor::json {

// --- Encoder ----------------------------------------------------------------
inline std::string encode(const std::string& host, float cpu, float ram,
                          float disk, time_t ts) {
    std::ostringstream o;
    o << "{\"host\":\"" << host << "\","
      << "\"cpu\":"  << cpu  << ","
      << "\"ram\":"  << ram  << ","
      << "\"disk\":" << disk << ","
      << "\"timestamp\":" << static_cast<long long>(ts) << "}";
    return o.str();
}

// --- Decoder (very small hand-rolled parser) ---------------------------------
struct Value {
    std::string str;
    double      num = 0.0;
    bool        is_str = false;
};

using Object = std::unordered_map<std::string, Value>;

inline std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

inline std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

inline Object decode(const std::string& json) {
    Object obj;
    std::string s = trim(json);
    if (s.empty() || s[0] != '{') throw std::runtime_error("Not a JSON object");
    s = s.substr(1, s.size() - 2); // strip { }

    // Split on commas not inside strings
    std::vector<std::string> pairs;
    int depth = 0; bool inStr = false;
    std::string cur;
    for (char c : s) {
        if (c == '"') inStr = !inStr;
        if (!inStr) {
            if (c == '{' || c == '[') depth++;
            if (c == '}' || c == ']') depth--;
            if (c == ',' && depth == 0) { pairs.push_back(cur); cur.clear(); continue; }
        }
        cur += c;
    }
    if (!cur.empty()) pairs.push_back(cur);

    for (auto& p : pairs) {
        auto colon = p.find(':');
        if (colon == std::string::npos) continue;
        std::string key = unquote(trim(p.substr(0, colon)));
        std::string val = trim(p.substr(colon + 1));
        Value v;
        if (!val.empty() && val[0] == '"') {
            v.is_str = true;
            v.str = unquote(val);
        } else {
            v.num = std::stod(val);
        }
        obj[key] = v;
    }
    return obj;
}

} // namespace monitor::json
