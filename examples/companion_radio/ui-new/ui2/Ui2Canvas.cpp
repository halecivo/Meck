#ifdef MECK_UI_V2

#include "Ui2Canvas.h"

static const char* const UI2_ELLIPSIS = "\xE2\x80\xA6";   // U+2026, drawn as three dots

// Byte length of the UTF-8 character starting with lead byte b
static inline int utf8Len(uint8_t b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1;
}

static inline const GFXglyph* glyphOf(const GFXfont* f, uint16_t cp) {
  if (cp < f->first || cp > f->last) return nullptr;
  return &f->glyph[cp - f->first];
}

// ---------------------------------------------------------------- primitives

void Ui2Canvas::icon(int16_t x, int16_t y, Ui2Icon id, uint8_t size, bool ink) {
  const uint8_t* bits = ui2Icon(id, size);
  if (bits) _d.drawXbmRaw(x, y, bits, size, size, _d.rawColor(ink));
}

// ---------------------------------------------------------------------- text

int16_t Ui2Canvas::textWidth(const char* s, Ui2Font f, int maxBytes) {
  return GxEPDDisplay::measureTextFontRaw(s, ui2Font(f), maxBytes);
}

int16_t Ui2Canvas::capHeight(Ui2Font f) {
  const GFXfont* font = ui2Font(f);
  const GFXglyph* g = glyphOf(font, 'H');
  if (!g || g->height == 0) g = glyphOf(font, '0');
  return g ? g->height : 8;
}

int16_t Ui2Canvas::lineBaseline(Ui2Font f, int16_t top, int16_t lineH) {
  // Ascent is ~78.5 % of the font's line spacing for Noto Sans and Montserrat
  int16_t ya = ui2Font(f)->yAdvance;
  return top + (lineH - ya) / 2 + (ya * 785 + 500) / 1000;
}

int Ui2Canvas::fitBytes(const char* s, Ui2Font f, int16_t maxW, int maxBytes) {
  if (!s) return 0;
  const GFXfont* font = ui2Font(f);
  int len = strlen(s);
  if (maxBytes >= 0 && maxBytes < len) len = maxBytes;
  int pos = 0;
  int16_t w = 0;
  while (pos < len) {
    int cl = utf8Len((uint8_t)s[pos]);
    if (pos + cl > len) break;
    int16_t cw = GxEPDDisplay::measureTextFontRaw(s + pos, font, cl);
    if (w + cw > maxW) break;
    w += cw;
    pos += cl;
  }
  return pos;
}

int16_t Ui2Canvas::text(int16_t x, int16_t baseline, const char* s, Ui2Font f, bool ink, int maxBytes) {
  return _d.drawTextFontRaw(x, baseline, s, ui2Font(f), ink, maxBytes);
}

int16_t Ui2Canvas::textTracked(int16_t x, int16_t baseline, const char* s, Ui2Font f, int16_t tracking, bool ink) {
  const GFXfont* font = ui2Font(f);
  int len = strlen(s), pos = 0;
  while (pos < len) {
    int cl = utf8Len((uint8_t)s[pos]);
    x = _d.drawTextFontRaw(x, baseline, s + pos, font, ink, cl) + tracking;
    pos += cl;
  }
  return x;
}

int16_t Ui2Canvas::textFit(int16_t x, int16_t baseline, int16_t maxW, const char* s, Ui2Font f,
                           bool ink, Ui2Align align, int maxBytes) {
  return fitLine(x, baseline, maxW, s, f, ink, align, maxBytes, false);
}

int16_t Ui2Canvas::fitLine(int16_t x, int16_t baseline, int16_t maxW, const char* s, Ui2Font f,
                           bool ink, Ui2Align align, int maxBytes, bool forceEllipsis) {
  if (!s || maxW <= 0) return 0;
  int len = strlen(s);
  if (maxBytes >= 0 && maxBytes < len) len = maxBytes;
  int16_t full = textWidth(s, f, len);
  if (full <= maxW && !forceEllipsis) {
    int16_t dx = (align == UI2_CENTER) ? (maxW - full) / 2 : (align == UI2_RIGHT) ? (maxW - full) : 0;
    text(x + dx, baseline, s, f, ink, len);
    return full;
  }
  int16_t ellW = textWidth(UI2_ELLIPSIS, f);
  int n = fitBytes(s, f, maxW - ellW, len);
  while (n > 0 && s[n - 1] == ' ') n--;          // no space before the ellipsis
  int16_t w = textWidth(s, f, n) + ellW;
  int16_t dx = (align == UI2_CENTER) ? (maxW - w) / 2 : (align == UI2_RIGHT) ? (maxW - w) : 0;
  int16_t px = text(x + dx, baseline, s, f, ink, n);
  text(px, baseline, UI2_ELLIPSIS, f, ink);
  return w;
}

int Ui2Canvas::wrap(int16_t x, int16_t firstBaseline, int16_t maxW, int16_t lineH, int maxLines,
                    const char* s, Ui2Font f, bool ink, bool draw, bool* truncated, Ui2Align align) {
  if (truncated) *truncated = false;
  if (!s || maxLines <= 0) return 0;
  const GFXfont* font = ui2Font(f);
  int len = strlen(s);
  int pos = 0, line = 0;
  while (pos < len && line < maxLines) {
    if (line > 0) while (pos < len && s[pos] == ' ') pos++;   // no leading spaces on wrapped lines
    if (pos >= len) break;
    int start = pos, i = pos, lastSpace = -1;
    int16_t w = 0;
    while (i < len && s[i] != '\n') {
      int cl = utf8Len((uint8_t)s[i]);
      int16_t cw = GxEPDDisplay::measureTextFontRaw(s + i, font, cl);
      if (w + cw > maxW) break;
      if (s[i] == ' ') lastSpace = i;
      w += cw;
      i += cl;
    }
    int end, next;
    if (i >= len || s[i] == '\n') {          // rest of the paragraph fits
      end = i;
      next = (i < len) ? i + 1 : i;
    } else if (lastSpace > start) {          // break at the last space
      end = lastSpace;
      next = lastSpace + 1;
    } else {                                 // one long word: hard break
      end = (i > start) ? i : i + utf8Len((uint8_t)s[i]);
      next = end;
    }
    int16_t baseline = firstBaseline + line * lineH;
    bool last = (line == maxLines - 1);
    int rest = next;
    while (rest < len && (s[rest] == ' ' || s[rest] == '\n')) rest++;
    if (last && rest < len) {
      // Text remains: end this line with an ellipsis, filling it with as
      // much of the current paragraph as fits
      if (truncated) *truncated = true;
      int paraEnd = start;
      while (paraEnd < len && s[paraEnd] != '\n') paraEnd++;
      if (draw) fitLine(x, baseline, maxW, s + start, f, ink, align, paraEnd - start, true);
      line++;
      break;
    }
    if (draw) {
      int16_t lw = (align == UI2_LEFT) ? 0 : textWidth(s + start, f, end - start);
      int16_t dx = (align == UI2_CENTER) ? (maxW - lw) / 2 : (align == UI2_RIGHT) ? (maxW - lw) : 0;
      text(x + dx, baseline, s + start, f, ink, end - start);
    }
    line++;
    pos = next;
  }
  return line;
}

// ------------------------------------------------------------------- widgets

static const int16_t KEYCAP_H = 15;

int16_t Ui2Canvas::keycapWidth(const char* key, Ui2Icon ic) {
  int16_t cw = (ic != UI2_NO_ICON) ? 12 : textWidth(key ? key : "", UI2_F_KEY);
  int16_t w = cw + 8;                       // 3 px padding + 1 px border each side
  return w < 16 ? 16 : w;
}

int16_t Ui2Canvas::keycap(int16_t x, int16_t top, const char* key, Ui2Icon ic, bool active, bool ink) {
  int16_t w = keycapWidth(key, ic);
  if (active) {
    fillRoundRect(x, top, w, KEYCAP_H, 3, ink);
  } else {
    roundRect(x, top, w, KEYCAP_H, 3, ink);
    hline(x + 1, top + KEYCAP_H - 2, w - 2, ink);   // raised bottom edge (2 px)
  }
  bool fg = active ? !ink : ink;
  if (ic != UI2_NO_ICON) {
    icon(x + (w - 12) / 2, top + 1, ic, 12, fg);
  } else if (key) {
    int16_t tw = textWidth(key, UI2_F_KEY);
    text(x + (w - tw) / 2, boxBaseline(UI2_F_KEY, top + 1, KEYCAP_H - 3), key, UI2_F_KEY, fg);
  }
  return w;
}

int16_t Ui2Canvas::chipWidth(const char* label, Ui2Icon ic) {
  int16_t w = 12;                                   // 5 px padding + 1 px border each side
  if (ic != UI2_NO_ICON) w += 12 + ((label && *label) ? 3 : 0);
  if (label) w += textWidth(label, UI2_F_CHIP);
  return w;
}

int16_t Ui2Canvas::chip(int16_t x, int16_t top, const char* label, Ui2Icon ic, bool active, bool ink) {
  const int16_t h = 14;
  int16_t w = chipWidth(label, ic);
  if (active) fillRoundRect(x, top, w, h, 7, ink);
  else roundRect(x, top, w, h, 7, ink);
  bool fg = active ? !ink : ink;
  int16_t cx = x + 6;
  if (ic != UI2_NO_ICON) { icon(cx, top + 1, ic, 12, fg); cx += 15; }
  if (label) text(cx, boxBaseline(UI2_F_CHIP, top, h), label, UI2_F_CHIP, fg);
  return w;
}

int16_t Ui2Canvas::badgeWidth(int16_t h, const char* text) {
  Ui2Font f = (h >= 18) ? UI2_F_NUM10 : UI2_F_NUM9;
  int16_t w = textWidth(text, f) + 8;
  int16_t minW = (h >= 18) ? 18 : 16;
  return w < minW ? minW : w;
}

int16_t Ui2Canvas::badge(int16_t x, int16_t top, int16_t h, const char* s, bool ink) {
  Ui2Font f = (h >= 18) ? UI2_F_NUM10 : UI2_F_NUM9;
  int16_t w = badgeWidth(h, s);
  fillRoundRect(x, top, w, h, h / 2, ink);
  int16_t tw = textWidth(s, f);
  text(x + (w - tw) / 2, boxBaseline(f, top, h), s, f, !ink);
  return w;
}

void Ui2Canvas::toggle(int16_t x, int16_t top, bool on, bool ink) {
  const int16_t w = 26, h = 14;
  if (on) fillRoundRect(x, top, w, h, 7, ink);
  else roundRect(x, top, w, h, 7, ink);
  int16_t kx = on ? x + 15 : x + 3;
  fillRoundRect(kx, top + 3, 8, 8, 4, on ? !ink : ink);
}

void Ui2Canvas::segBar(int16_t x, int16_t top, int16_t w, int16_t h, int filled, int total, int16_t gap, bool ink) {
  if (total <= 0) return;
  int16_t avail = w - gap * (total - 1);
  int16_t px = x;
  for (int i = 0; i < total; i++) {
    // distribute the remainder so the bar keeps its exact width
    int16_t sw = avail * (i + 1) / total - avail * i / total;
    if (i < filled) fill(px, top, sw, h, ink);
    else rect(px, top, sw, h, ink);
    px += sw + gap;
  }
}

void Ui2Canvas::pageDots(int16_t cx, int16_t top, int count, int active, bool ink) {
  if (count <= 1) return;
  int16_t total = count * 6 + (count - 1) * 6;
  int16_t x = cx - total / 2;
  for (int i = 0; i < count; i++) {
    if (i == active) fillRoundRect(x, top, 6, 6, 3, ink);
    else roundRect(x, top, 6, 6, 3, ink);
    x += 12;
  }
}

int16_t Ui2Canvas::battery(int16_t x, int16_t top, int pct, bool ink) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  int lit = (pct + 19) / 20;                 // ceil(pct / 20), as the design
  rect(x, top, 18, 10, ink);
  for (int i = 0; i < 5; i++) {
    if (i < lit) fill(x + 2 + i * 3, top + 2, 2, 6, ink);
  }
  fill(x + 18, top + 3, 2, 4, ink);          // nub
  return 20;
}

void Ui2Canvas::sectionHeader(int16_t y, const char* label, bool ink) {
  int16_t base = y + ui2::SECTION_H - 3;
  int16_t ex = textTracked(ui2::PAD, base, label, UI2_F_CHIP, 1, ink);
  int16_t rx = ex + 5;
  if (rx < ui2::SCREEN_W - ui2::PAD) hline(rx, y + 10, ui2::SCREEN_W - ui2::PAD - rx, ink);
}

#endif  // MECK_UI_V2
