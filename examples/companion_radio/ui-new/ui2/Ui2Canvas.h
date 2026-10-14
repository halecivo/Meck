#pragma once
// =============================================================================
// UI v2 -- native-pixel drawing helpers of the e-ink design system
//
// Wraps GxEPDDisplay's raw primitives with the design system's text rules
// (ellipsis, wrapping with hard breaks for long words) and its widgets
// (keycaps, chips, badges, toggles, segmented bars, page dots, battery,
// section headers). All coordinates are physical pixels, 240x320.
//
// "ink" is the foreground colour (black, or white in dark mode).
// =============================================================================

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/GxEPDDisplay.h>
#include "Ui2Theme.h"
#include "Ui2Assets.h"

enum Ui2Align : uint8_t { UI2_LEFT, UI2_CENTER, UI2_RIGHT };

static const Ui2Icon UI2_NO_ICON = UI2_IC_COUNT;

class Ui2Canvas {
public:
  explicit Ui2Canvas(DisplayDriver& d) : _d(static_cast<GxEPDDisplay&>(d)) {}
  GxEPDDisplay& raw() { return _d; }

  // --- Primitives ----------------------------------------------------------
  void fill(int16_t x, int16_t y, int16_t w, int16_t h, bool ink = true) { _d.fillRectRaw(x, y, w, h, ink); }
  void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink = true) { _d.drawRectRaw(x, y, w, h, ink); }
  void hline(int16_t x, int16_t y, int16_t w, bool ink = true) { _d.hLineRaw(x, y, w, ink); }
  void vline(int16_t x, int16_t y, int16_t h, bool ink = true) { _d.vLineRaw(x, y, h, ink); }
  void roundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool ink = true) { _d.drawRoundRectRaw(x, y, w, h, r, ink); }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool ink = true) { _d.fillRoundRectRaw(x, y, w, h, r, ink); }
  void dashedRect(int16_t x, int16_t y, int16_t w, int16_t h, bool ink = true) { _d.drawDashedRectRaw(x, y, w, h, 2, ink); }
  void hatch(int16_t x, int16_t y, int16_t w, int16_t h, bool ink = true) { _d.hatchRectRaw(x, y, w, h, 4, ink); }
  // Icon at 12/16/24/32 px with its top-left corner at (x, y)
  void icon(int16_t x, int16_t y, Ui2Icon id, uint8_t size, bool ink = true);

  // --- Text ----------------------------------------------------------------
  static int16_t textWidth(const char* s, Ui2Font f, int maxBytes = -1);
  // Height of a capital letter, for vertically centring text in a box
  static int16_t capHeight(Ui2Font f);
  // Baseline of a single line of text in a line box (CSS-like centring)
  static int16_t lineBaseline(Ui2Font f, int16_t top, int16_t lineH);
  // Baseline that centres capitals in a box [top, top + h)
  static int16_t boxBaseline(Ui2Font f, int16_t top, int16_t h) { return top + (h + capHeight(f)) / 2; }
  // Number of leading bytes of s (whole characters) whose width fits maxW
  static int fitBytes(const char* s, Ui2Font f, int16_t maxW, int maxBytes = -1);

  // Draw text at (x, baseline); returns the pen x after it
  int16_t text(int16_t x, int16_t baseline, const char* s, Ui2Font f, bool ink = true, int maxBytes = -1);
  // Letter-spaced text (section headers)
  int16_t textTracked(int16_t x, int16_t baseline, const char* s, Ui2Font f, int16_t tracking, bool ink = true);
  // One line, never wider than maxW: ellipsized with "..." when too long and
  // aligned within [x, x + maxW]. Returns the drawn width.
  int16_t textFit(int16_t x, int16_t baseline, int16_t maxW, const char* s, Ui2Font f,
                  bool ink = true, Ui2Align align = UI2_LEFT, int maxBytes = -1);
  // Word-wrapped text: at most maxLines lines of lineH, the last one
  // ellipsized if text remains. Words longer than a line are hard-broken
  // (URLs). '\n' forces a break. With draw = false only measures.
  // Returns the number of lines; *truncated tells whether text was cut.
  int wrap(int16_t x, int16_t firstBaseline, int16_t maxW, int16_t lineH, int maxLines,
           const char* s, Ui2Font f, bool ink = true, bool draw = true, bool* truncated = nullptr);

  // --- Widgets -------------------------------------------------------------
  // Keycap (15 px tall, raised bottom edge) showing a key name or an icon.
  // Returns its width. active = inverted (e.g. "Mute" on).
  int16_t keycap(int16_t x, int16_t top, const char* key, Ui2Icon ic = UI2_NO_ICON, bool active = false, bool ink = true);
  static int16_t keycapWidth(const char* key, Ui2Icon ic = UI2_NO_ICON);
  // Pill chip (14 px tall) with optional 12 px icon. Returns its width.
  int16_t chip(int16_t x, int16_t top, const char* label, Ui2Icon ic = UI2_NO_ICON, bool active = false, bool ink = true);
  static int16_t chipWidth(const char* label, Ui2Icon ic = UI2_NO_ICON);
  // Count badge pill: h = 18 (tiles, Montserrat 10) or 14 (rows, Montserrat 9).
  // `ink` is the pill colour; the number uses the opposite. Returns its width.
  int16_t badge(int16_t x, int16_t top, int16_t h, const char* text, bool ink = true);
  static int16_t badgeWidth(int16_t h, const char* text);
  // 26 x 14 toggle
  void toggle(int16_t x, int16_t top, bool on, bool ink = true);
  // Segmented bar: `total` segments in w px, the first `filled` ones solid
  void segBar(int16_t x, int16_t top, int16_t w, int16_t h, int filled, int total, int16_t gap = 1, bool ink = true);
  // Page dots centred on cx (6 px dots, 6 px gap)
  void pageDots(int16_t cx, int16_t top, int count, int active, bool ink = true);
  // Battery: 5 segments in an 18 x 10 outline plus nub; returns width (20)
  int16_t battery(int16_t x, int16_t top, int pct, bool ink = true);
  // List section header (16 px): small caps label and a rule to the right
  void sectionHeader(int16_t y, const char* label, bool ink = true);

private:
  GxEPDDisplay& _d;
  // textFit, optionally forcing the ellipsis even if the text fits
  int16_t fitLine(int16_t x, int16_t baseline, int16_t maxW, const char* s, Ui2Font f,
                  bool ink, Ui2Align align, int maxBytes, bool forceEllipsis);
};
