#pragma once
#include "ui_types.hpp"

namespace monitor::ui {

class LayoutEngine {
public:
  static Layout compute(Size size, const DashboardState& state) {
    Layout l;
    l.topBar = {0, 0, 1, size.cols};
    l.statusBar = {size.rows - 1, 0, 1, size.cols};
    
    int contentY = 1;
    int contentH = size.rows - 2;
    
    int eventHeight = 0;
    if (size.rows >= 30 && state.showEventPreview && state.screen == Screen::Overview) {
      eventHeight = static_cast<int>(size.rows * 0.20);
      l.eventPane = {size.rows - 1 - eventHeight, 0, eventHeight, size.cols};
      l.showEvents = true;
      contentH -= eventHeight;
    }
    
    if (size.cols < 100) {
      l.kind = LayoutKind::Compact;
      l.showLeft = false;
      l.showRight = false;
      l.mainPane = {contentY, 0, contentH, size.cols};
    } 
    else if (size.cols < 140) {
      l.kind = LayoutKind::Standard;
      l.showLeft = true;
      l.showRight = true;
      int listWidth = static_cast<int>(size.cols * 0.35);
      l.leftPane = {contentY, 0, contentH, listWidth};
      l.mainPane = {contentY, listWidth, contentH, size.cols - listWidth};
    } 
    else {
      l.kind = LayoutKind::Wide;
      l.showLeft = true;
      l.showRight = true;
      int groupWidth = static_cast<int>(size.cols * 0.18);
      int tableWidth = static_cast<int>(size.cols * 0.52);
      int rightWidth = size.cols - groupWidth - tableWidth;
      l.leftPane = {contentY, 0, contentH, groupWidth};
      l.mainPane = {contentY, groupWidth, contentH, tableWidth};
      l.rightPane = {contentY, groupWidth + tableWidth, contentH, rightWidth};
    }
    
    return l;
  }
};

} // namespace monitor::ui
