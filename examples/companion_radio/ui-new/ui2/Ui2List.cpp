#ifdef MECK_UI_V2

#include "Ui2List.h"
#include <stdio.h>

using namespace ui2;

namespace Ui2List {

void drawRow(Ui2Canvas& c, int16_t y, const Ui2Row& r) {
  const int16_t h = r.height;
  const bool sel = r.selected;
  const bool fg = !sel;              // text colour: ink, or background on an inverted row

  if (sel) {
    c.fill(0, y, SCREEN_W, h, true);
    c.fill(2, y + 4, 2, h - 8, false);            // 2 px bar inside on the left
  }
  if (r.pending || r.disabled) c.dashedRect(3, y + 3, SCREEN_W - 6, h - 6, fg);
  if (!r.noRule && !sel) c.hline(0, y + h - 1, SCREEN_W, true);

  // A long right value drops to the subtitle line
  const char* sub = r.sub;
  const char* right = r.right;
  if (!sub && right && Ui2Canvas::textWidth(right, UI2_F_SMALL) > 72) { sub = right; right = nullptr; }

  // ---- right side, laid out from the right edge ----
  int16_t xr = SCREEN_W - 8;
  if (r.chevron) { xr -= 12; c.icon(xr, y + (h - 12) / 2, UI2_IC_CHEV, 12, fg); xr -= 7; }
  if (r.check)   { xr -= 16; c.icon(xr, y + (h - 16) / 2, UI2_IC_CHECK, 16, fg); xr -= 7; }
  if (r.toggle >= 0) { xr -= 26; c.toggle(xr, y + (h - 14) / 2, r.toggle == 1, fg); xr -= 7; }

  if (right || r.right2 || r.badge) {
    int16_t w1 = right ? Ui2Canvas::textWidth(right, UI2_F_SMALL) : 0;
    int16_t w2 = r.right2 ? Ui2Canvas::textWidth(r.right2, UI2_F_META) : 0;
    int16_t wb = r.badge ? Ui2Canvas::badgeWidth(14, r.badge) : 0;
    int16_t cw = w1 > w2 ? w1 : w2;
    if (wb > cw) cw = wb;
    if (cw > r.rightMax) cw = r.rightMax;
    int16_t colH = (right ? 14 : 0) + (r.right2 ? 12 : 0) + (r.badge ? 14 : 0);
    int items = (right ? 1 : 0) + (r.right2 ? 1 : 0) + (r.badge ? 1 : 0);
    colH += items > 1 ? items - 1 : 0;
    int16_t ty = y + (h - colH) / 2;
    if (right) {
      c.textFit(xr - cw, Ui2Canvas::lineBaseline(UI2_F_SMALL, ty, 14), cw, right, UI2_F_SMALL, fg, UI2_RIGHT);
      ty += 15;
    }
    if (r.right2) {
      c.textFit(xr - cw, Ui2Canvas::lineBaseline(UI2_F_META, ty, 12), cw, r.right2, UI2_F_META, fg, UI2_RIGHT);
      ty += 13;
    }
    if (r.badge) c.badge(xr - wb, ty, 14, r.badge, fg);
    xr -= cw + 7;
  }
  if (r.sig != UI2_NO_ICON) { xr -= 12; c.icon(xr, y + (h - 12) / 2, r.sig, 12, fg); xr -= 7; }
  if (r.star) { xr -= 12; c.icon(xr, y + (h - 12) / 2, UI2_IC_STARF, 12, fg); xr -= 7; }

  // ---- left side ----
  int16_t x = 9;
  if (r.dot) { c.fillRoundRect(x, y + (h - 6) / 2, 6, 6, 3, fg); x += 6 + 5; }
  if (r.icon != UI2_NO_ICON) {
    int16_t iy = y + (h - 16) / 2;
    if (r.disabled) c.hatch(x, iy, 16, 16, fg);
    c.icon(x, iy, r.icon, 16, fg);
    x += 16 + 7;
  }

  // ---- text column ----
  int16_t tw = xr - x;
  if (tw < 8) return;
  if (sub) {
    int16_t top = y + (h - 30) / 2;
    c.textFit(x, Ui2Canvas::lineBaseline(UI2_F_LABEL, top, 15), tw, r.title ? r.title : "", UI2_F_LABEL, fg);
    c.textFit(x, Ui2Canvas::lineBaseline(UI2_F_BODY, top + 15, 15), tw, sub, UI2_F_BODY, fg);
  } else {
    int16_t top = y + (h - 15) / 2;
    c.textFit(x, Ui2Canvas::lineBaseline(UI2_F_LABEL, top, 15), tw, r.title ? r.title : "", UI2_F_LABEL, fg);
  }
}

void formatAge(char* buf, size_t len, int32_t secs) {
  if (secs < 0) snprintf(buf, len, "-");
  else if (secs < 60) snprintf(buf, len, "%lds", (long)secs);
  else if (secs < 3600) snprintf(buf, len, "%ldm", (long)(secs / 60));
  else if (secs < 86400) snprintf(buf, len, "%ldh", (long)(secs / 3600));
  else snprintf(buf, len, "%ldd", (long)(secs / 86400));
}

void kvRow(Ui2Canvas& c, int16_t y, const char* label, const char* value) {
  int16_t vw = value ? Ui2Canvas::textWidth(value, UI2_F_LABEL) : 0;
  const int16_t maxV = SCREEN_W - 2 * PAD - 40;     // keep room for the label
  if (vw > maxV) vw = maxV;
  if (value) c.textFit(SCREEN_W - PAD - vw, Ui2Canvas::lineBaseline(UI2_F_LABEL, y + 2, 15), vw, value, UI2_F_LABEL);
  int16_t lw = SCREEN_W - 2 * PAD - vw - 8;
  if (label && lw > 0) c.textFit(PAD, Ui2Canvas::lineBaseline(UI2_F_BODY, y + 2, 15), lw, label, UI2_F_BODY);
}

void pageLabel(char* buf, size_t len, int index, int count, int perPage) {
  int pages = pageCount(count, perPage);
  if (pages <= 1) { if (len) buf[0] = 0; return; }
  snprintf(buf, len, "%d / %d", pageOf(index, perPage) + 1, pages);
}

void toast(Ui2Canvas& c, const char* text, Ui2Icon icon) {
  if (!text) text = "";
  // A '\n' splits a two-line toast: bold first line, regular second line
  const char* nl = strchr(text, '\n');
  const int16_t x = PAD, w = TOAST_W, h = nl ? 40 : 24, y = FOOTER_Y - 4 - h;
  c.fillRoundRect(x, y, w, h, 3, true);
  int16_t tx = x + 8;
  if (icon != UI2_NO_ICON) { c.icon(tx, y + (nl ? 6 : (h - 12) / 2), icon, 12, false); tx += 12 + 6; }
  int16_t tw = x + w - 8 - tx;
  if (nl) {
    c.textFit(tx, Ui2Canvas::lineBaseline(UI2_F_LABEL, y + 5, 15), tw, text, UI2_F_LABEL, false, UI2_LEFT, nl - text);
    c.textFit(tx, Ui2Canvas::lineBaseline(UI2_F_BODY, y + 20, 15), tw, nl + 1, UI2_F_BODY, false);
  } else {
    c.textFit(tx, Ui2Canvas::boxBaseline(UI2_F_LABEL, y, h), tw, text, UI2_F_LABEL, false);
  }
}

void dialog(Ui2Canvas& c, Ui2Icon icon, const char* title, const char* body,
            const char* cancelLabel, const char* confirmLabel) {
  const int16_t x = DLG_X, w = DLG_W, pad = 10, gap = 8, btnH = 32;
  const int16_t innerW = w - 2 * pad;
  const int16_t titleX = x + pad + (icon != UI2_NO_ICON ? 24 + 6 : 0);
  const int16_t titleW = x + w - pad - titleX;

  // Measure first so the box can be centred on the content area
  int titleLines = c.wrap(titleX, 0, titleW, 18, 2, title ? title : "", UI2_F_TITLE, true, false);
  int16_t titleH = titleLines * 18;
  if (icon != UI2_NO_ICON && titleH < 24) titleH = 24;
  int bodyLines = (body && *body) ? c.wrap(x + pad, 0, innerW, 15, 4, body, UI2_F_BODY, true, false) : 0;
  int16_t h = 2 + pad + titleH + (bodyLines ? gap + bodyLines * 15 : 0) + gap + btnH + pad + 2;
  int16_t y = BODY_Y + (FOOTER_Y - BODY_Y - h) / 2 - 8;
  if (y < HDR_H + 4) y = HDR_H + 4;

  c.fill(x + 3, y + 3, w, h, true);                 // offset shadow
  c.fill(x, y, w, h, false);                        // clear the box
  c.roundRect(x, y, w, h, 3, true);
  c.roundRect(x + 1, y + 1, w - 2, h - 2, 2, true); // 2 px border

  int16_t cy = y + 2 + pad;
  if (icon != UI2_NO_ICON) c.icon(x + pad, cy, icon, 24, true);
  int16_t ty = cy + (icon != UI2_NO_ICON && titleLines == 1 ? 3 : 0);
  c.wrap(titleX, Ui2Canvas::lineBaseline(UI2_F_TITLE, ty, 18), titleW, 18, 2, title ? title : "", UI2_F_TITLE, true);
  cy += titleH;
  if (bodyLines) {
    cy += gap;
    c.wrap(x + pad, Ui2Canvas::lineBaseline(UI2_F_BODY, cy, 15), innerW, 15, 4, body, UI2_F_BODY, true);
    cy += bodyLines * 15;
  }
  cy += gap;

  // Buttons: secondary (Q) left, primary (Enter) right
  const int16_t bw = (innerW - 6) / 2;
  const int16_t bx1 = x + pad, bx2 = x + pad + bw + 6;
  c.roundRect(bx1, cy, bw, btnH, 3, true);
  {
    const char* lab = cancelLabel ? cancelLabel : "";
    int16_t kw = Ui2Canvas::keycapWidth("Q");
    int16_t lw = Ui2Canvas::textWidth(lab, UI2_F_LABEL);
    int16_t tot = kw + 4 + lw;
    if (tot > bw - 8) { lw = bw - 8 - kw - 4; tot = bw - 8; }
    int16_t sx = bx1 + (bw - tot) / 2;
    c.keycap(sx, cy + (btnH - 15) / 2, "Q");
    c.textFit(sx + kw + 4, Ui2Canvas::boxBaseline(UI2_F_LABEL, cy, btnH), lw, lab, UI2_F_LABEL);
  }
  c.fillRoundRect(bx2, cy, bw, btnH, 3, true);
  {
    const char* lab = confirmLabel ? confirmLabel : "";
    int16_t lw = Ui2Canvas::textWidth(lab, UI2_F_LABEL);
    int16_t tot = 12 + 4 + lw;
    if (tot > bw - 8) { lw = bw - 8 - 16; tot = bw - 8; }
    int16_t sx = bx2 + (bw - tot) / 2;
    c.icon(sx, cy + (btnH - 12) / 2, UI2_IC_ENTER, 12, false);
    c.textFit(sx + 16, Ui2Canvas::boxBaseline(UI2_F_LABEL, cy, btnH), lw, lab, UI2_F_LABEL, false);
  }
}

void emptyState(Ui2Canvas& c, int16_t top, int16_t bottom, Ui2Icon icon,
                const char* line, const char* detail,
                const char* actionKey, const char* actionLabel) {
  const int16_t w = SCREEN_W - 2 * 12;
  int detailLines = (detail && *detail) ? c.wrap(12, 0, w, 15, 2, detail, UI2_F_BODY, true, false) : 0;
  bool action = actionLabel && *actionLabel;
  int16_t h = 32 + 8 + 18 + (detailLines ? 6 + detailLines * 15 : 0) + (action ? 10 + 20 : 0);
  int16_t y = top + (bottom - top - h) / 2;

  c.icon((SCREEN_W - 32) / 2, y, icon, 32, true);
  y += 32 + 8;
  c.textFit(12, Ui2Canvas::lineBaseline(UI2_F_TITLE, y, 18), w, line ? line : "", UI2_F_TITLE, true, UI2_CENTER);
  y += 18;
  if (detailLines) {
    y += 6;
    c.wrap(12, Ui2Canvas::lineBaseline(UI2_F_BODY, y, 15), w, 15, 2, detail, UI2_F_BODY, true,
           true, nullptr, UI2_CENTER);
    y += detailLines * 15;
  }
  if (action) {
    y += 10;
    int16_t kw = actionKey ? Ui2Canvas::keycapWidth(actionKey) : 0;
    int16_t lw = Ui2Canvas::textWidth(actionLabel, UI2_F_LABEL);
    int16_t pw = 10 + (kw ? kw + 5 : 0) + lw + 10;
    int16_t px = (SCREEN_W - pw) / 2;
    c.fillRoundRect(px, y, pw, 20, 10, true);
    int16_t ax = px + 10;
    if (kw) {
      // keycap drawn inverted on the filled pill
      c.keycap(ax, y + 2, actionKey, UI2_NO_ICON, false, false);
      ax += kw + 5;
    }
    c.text(ax, Ui2Canvas::boxBaseline(UI2_F_LABEL, y, 20), actionLabel, UI2_F_LABEL, false);
  }
}

}  // namespace Ui2List

#endif  // MECK_UI_V2
