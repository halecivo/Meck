#pragma once
// =============================================================================
// UI v2 -- message text with emoji: tokenising, word wrap and drawing
//
// Message and compose buffers hold UTF-8 text plus single-byte emoji escapes
// (EmojiSprites.h, drawn as 12 px sprites) and pad bytes (ignored). Words
// wrap at spaces; a word longer than a line (a URL) is hard-broken.
// =============================================================================

#include "Ui2Canvas.h"
#include "../EmojiSprites.h"

struct Ui2RtLine { int16_t a, b, w; };   // bytes [a, b) and their width

// A mention as the MeshCore app writes it, "@[name]", at pos in body text:
// returns its byte length (0 when there is none). It is drawn bold as
// "@name" and wraps as one unit.
static inline int ui2RtMention(const char* s, int pos, int len, Ui2Font font) {
  if (font != UI2_F_BODY || s[pos] != '@' || pos + 2 >= len || s[pos + 1] != '[') return 0;
  for (int i = pos + 2; i < len && i < pos + 2 + 32; i++) {
    if (s[i] == ']') return i > pos + 2 ? i - pos + 1 : 0;
  }
  return 0;
}

// Byte length of the token at pos and its width in `font`
static inline int ui2RtToken(const char* s, int pos, int len, Ui2Font font, int16_t* w) {
  uint8_t b = (uint8_t)s[pos];
  if (b == EMOJI_PAD_BYTE) { *w = 0; return 1; }
  int ml = ui2RtMention(s, pos, len, font);
  if (ml) {
    *w = Ui2Canvas::textWidth("@", UI2_F_LABEL) + Ui2Canvas::textWidth(s + pos + 2, UI2_F_LABEL, ml - 3);
    return ml;
  }
  bool utf8 = (b >= 0xC2 && pos + 1 < len && ((uint8_t)s[pos + 1] & 0xC0) == 0x80);
  if (!utf8 && isEmojiEscape(b)) { *w = EMOJI_LG_W + 1; return 1; }
  int cl = utf8 ? ((b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4) : 1;
  if (pos + cl > len) cl = len - pos;
  *w = Ui2Canvas::textWidth(s + pos, font, cl);
  return cl;
}

// Wrap s[0, len) into at most maxLines lines of maxW px; *more tells whether
// text remains after the last line. Returns the number of lines.
static inline int ui2RtWrap(const char* s, int len, int16_t maxW, Ui2Font font,
                            Ui2RtLine* out, int maxLines, bool* more) {
  int pos = 0, n = 0;
  if (more) *more = false;
  while (pos < len && n < maxLines) {
    while (pos < len && s[pos] == ' ') pos++;
    if (pos >= len) break;
    int start = pos, lastSpace = -1, i = pos;
    int16_t w = 0, wAtSpace = 0;
    while (i < len) {
      int16_t tw;
      int cl = ui2RtToken(s, i, len, font, &tw);
      if (w + tw > maxW) break;
      if (s[i] == ' ') { lastSpace = i; wAtSpace = w; }
      w += tw;
      i += cl;
    }
    Ui2RtLine& L = out[n++];
    if (i >= len) { L.a = start; L.b = len; L.w = w; pos = len; }
    else if (lastSpace > start) { L.a = start; L.b = lastSpace; L.w = wAtSpace; pos = lastSpace + 1; }
    else {
      if (i == start) { int16_t tw; i += ui2RtToken(s, i, len, font, &tw); w = tw; }
      L.a = start; L.b = i; L.w = w; pos = i;
    }
  }
  while (pos < len && s[pos] == ' ') pos++;
  if (more) *more = pos < len;
  return n;
}

// Draw bytes [a, b): text runs in `font`, emoji as sprites. Returns the pen x.
static inline int16_t ui2RtDraw(Ui2Canvas& c, int16_t x, int16_t baseline, const char* s, int a, int b,
                                Ui2Font font, bool ink = true) {
  int i = a, run = a;
  while (i < b) {
    int16_t tw;
    int cl = ui2RtToken(s, i, b, font, &tw);
    uint8_t ch = (uint8_t)s[i];
    if (cl > 3 && ui2RtMention(s, i, b, font) == cl) {
      // "@[name]" -> bold "@name"
      if (i > run) x = c.text(x, baseline, s + run, font, ink, i - run);
      x = c.text(x, baseline, "@", UI2_F_LABEL, ink);
      x = c.text(x, baseline, s + i + 2, UI2_F_LABEL, ink, cl - 3);
      i += cl;
      run = i;
      continue;
    }
    bool emoji = (cl == 1 && ch != EMOJI_PAD_BYTE && tw == EMOJI_LG_W + 1 && isEmojiEscape(ch));
    if (emoji || ch == EMOJI_PAD_BYTE) {
      if (i > run) x = c.text(x, baseline, s + run, font, ink, i - run);
      if (emoji) {
        const uint8_t* sprite = getEmojiSpriteLg(ch);
        if (sprite) c.raw().drawXbmRaw(x, baseline - 10, sprite, EMOJI_LG_W, EMOJI_LG_H, c.raw().rawColor(ink));
        x += EMOJI_LG_W + 1;
      }
      run = i + cl;
    }
    i += cl;
  }
  if (b > run) x = c.text(x, baseline, s + run, font, ink, b - run);
  return x;
}
