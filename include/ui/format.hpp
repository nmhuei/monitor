#pragma once
#include <string>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include "../protocol.hpp"

namespace monitor::ui {

inline std::string formatAge(time_t lastSeen, HostStatus status) {
  if (lastSeen == 0) return "never";
  time_t diff = time(nullptr) - lastSeen;
  if (diff < 0) diff = 0;
  if (status == HostStatus::OFFLINE) return "offline";
  if (status == HostStatus::STALE) return "stale " + std::to_string(diff) + "s";
  if (diff < 60) return std::to_string(diff) + "s";
  time_t mins = diff / 60;
  if (mins < 60) return std::to_string(mins) + "m";
  time_t hrs = mins / 60;
  return std::to_string(hrs) + "h";
}

inline const char* getBlockGlyph(float pct) {
  static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  int idx = std::clamp(static_cast<int>((pct / 100.f) * 8), 0, 8);
  return blocks[idx];
}

inline std::string drawBar(float pct, int width) {
  int filled = static_cast<int>((pct / 100.f) * width);
  filled = std::clamp(filled, 0, width);
  std::string out;
  for (int i = 0; i < filled; ++i) out += "█";
  for (int i = filled; i < width; ++i) out += "░";
  return out;
}

inline std::string formatNet(float kb) {
  if (kb < 0.1f) return "0B";
  if (kb < 1024.f) {
    char b[16];
    snprintf(b, sizeof(b), "%.0fK", kb);
    return b;
  }
  char b[16];
  snprintf(b, sizeof(b), "%.1fM", kb / 1024.f);
  return b;
}

inline std::string formatUptime(time_t seconds) {
  if (seconds < 60) return std::to_string(seconds) + "s";
  time_t mins = seconds / 60;
  time_t secs = seconds % 60;
  if (mins < 60) return std::to_string(mins) + "m " + std::to_string(secs) + "s";
  time_t hrs = mins / 60;
  mins = mins % 60;
  return std::to_string(hrs) + "h " + std::to_string(mins) + "m";
}

inline std::string padRight(const std::string &s, size_t n) {
  if (s.size() >= n) return s.substr(0, n);
  return s + std::string(n - s.size(), ' ');
}

inline std::string padLeft(const std::string &s, size_t n) {
  if (s.size() >= n) return s.substr(0, n);
  return std::string(n - s.size(), ' ') + s;
}

inline std::string makeBrailleChar(int h1, int h2) {
  static const int mask_left[] = {0, 0x40, 0x44, 0x46, 0x47};
  static const int mask_right[] = {0, 0x80, 0x20 | 0x80, 0x10 | 0x20 | 0x80, 0x08 | 0x10 | 0x20 | 0x80};
  int val = mask_left[std::clamp(h1, 0, 4)] + mask_right[std::clamp(h2, 0, 4)];
  int cp = 0x2800 + val;
  std::string utf8;
  utf8 += static_cast<char>(0xE0 | (cp >> 12));
  utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  utf8 += static_cast<char>(0x80 | (cp & 0x3F));
  return utf8;
}

} // namespace monitor::ui
