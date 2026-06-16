#pragma once
#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif
#include <vector>
#include <algorithm>
#include "../theme.hpp"
#include "../format.hpp"

namespace monitor::ui {

inline void drawBrailleSparkline(int start_y, int start_x, int W, int H,
                                 const std::vector<float>& values, float thresh) {
  std::vector<float> data(2 * W, 0.0f);
  int offset = static_cast<int>(2 * W) - static_cast<int>(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    int target_idx = static_cast<int>(i) + offset;
    if (target_idx >= 0 && target_idx < 2 * W) {
      data[target_idx] = values[i];
    }
  }

  for (int row_idx = H - 1; row_idx >= 0; --row_idx) {
    int screen_y = start_y + (H - 1 - row_idx);
    move(screen_y, start_x);
    for (int char_x = 0; char_x < W; ++char_x) {
      int col1 = 2 * char_x;
      int col2 = 2 * char_x + 1;
      
      float v1 = data[col1];
      float v2 = data[col2];
      
      int height1 = std::clamp(static_cast<int>((v1 / 100.f) * (4 * H)), 0, 4 * H);
      int height2 = std::clamp(static_cast<int>((v2 / 100.f) * (4 * H)), 0, 4 * H);
      
      int h1 = std::clamp(height1 - 4 * row_idx, 0, 4);
      int h2 = std::clamp(height2 - 4 * row_idx, 0, 4);
      
      float avg = (v1 + v2) / 2.0f;
      int cp = CP_GRAPH_LOW;
      if (avg >= thresh) cp = CP_GRAPH_HIGH;
      else if (avg >= thresh * 0.8f) cp = CP_GRAPH_MID;
      
      attron(COLOR_PAIR(cp));
      if (h1 > 0 || h2 > 0) {
        addstr(makeBrailleChar(h1, h2).c_str());
      } else {
        addch(' ');
      }
      attroff(COLOR_PAIR(cp));
    }
  }
}

inline void drawBlockSparkline(int start_y, int start_x, int W, int H,
                               const std::vector<float>& values, float thresh) {
  std::vector<float> data(W, 0.0f);
  int offset = W - static_cast<int>(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    int target_idx = static_cast<int>(i) + offset;
    if (target_idx >= 0 && target_idx < W) {
      data[target_idx] = values[i];
    }
  }

  static const char* blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

  for (int row_idx = H - 1; row_idx >= 0; --row_idx) {
    int screen_y = start_y + (H - 1 - row_idx);
    move(screen_y, start_x);
    for (int char_x = 0; char_x < W; ++char_x) {
      float v = data[char_x];
      int total_levels = H * 8;
      int height = std::clamp(static_cast<int>((v / 100.f) * total_levels), 0, total_levels);
      int h = std::clamp(height - 8 * row_idx, 0, 8);
      
      int cp = CP_GRAPH_LOW;
      if (v >= thresh) cp = CP_GRAPH_HIGH;
      else if (v >= thresh * 0.8f) cp = CP_GRAPH_MID;
      
      attron(COLOR_PAIR(cp));
      addstr(blocks[h]);
      attroff(COLOR_PAIR(cp));
    }
  }
}

inline void drawSparkline(int start_y, int start_x, int W, int H,
                          const std::vector<float>& values, float thresh) {
#ifdef HAVE_NCURSESW
  drawBrailleSparkline(start_y, start_x, W, H, values, thresh);
#else
  drawBlockSparkline(start_y, start_x, W, H, values, thresh);
#endif
}

} // namespace monitor::ui
