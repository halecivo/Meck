#pragma once
// =============================================================================
// UI v2 -- a DisplayDriver that records text instead of drawing it
//
// Lets a redesigned screen reuse the text that existing drawing code builds
// (labels, values, hints) without duplicating that code. Text printed at
// cursor x <= 0 goes to left(), text printed after a setCursor() with x > 0
// goes to right() (the classic layouts right-align hints that way).
// =============================================================================

#include <helpers/ui/DisplayDriver.h>
#include <string.h>

class Ui2TextCapture : public DisplayDriver {
public:
  Ui2TextCapture() : DisplayDriver(128, 128) { reset(); }

  void reset() { _left[0] = 0; _right[0] = 0; _toRight = false; }
  const char* left() const { return _left; }
  const char* right() const { return _right; }

  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(Color) override {}
  void setTextSize(int) override {}
  void setColor(Color) override {}
  void setCursor(int x, int) override { _toRight = x > 0; }
  void print(const char* str) override {
    char* dst = _toRight ? _right : _left;
    size_t used = strlen(dst);
    if (str && used < CAP - 1) {
      strncpy(dst + used, str, CAP - 1 - used);
      dst[CAP - 1] = 0;
    }
  }
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  // Small widths so classic code keeps texts on one line (no tickers)
  uint16_t getTextWidth(const char* str) override { return str ? strlen(str) : 0; }
  void endFrame() override {}

private:
  static const size_t CAP = 112;
  char _left[CAP];
  char _right[CAP];
  bool _toRight;
};
