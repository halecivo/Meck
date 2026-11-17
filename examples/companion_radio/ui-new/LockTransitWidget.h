#pragma once
// =============================================================================
// Lock screen departures (docs/transit-widget.md): the rows the Neujede
// server sent (MeckTransit), drawn as in "Lock screen v3", departures:
//
//   ŘÍČANY, NÁDRAŽÍ                         then        <- header, 14 px
//   [S9] Praha hl.n.           [1]  7:41    7:45 8:15.  <- rows, 18 px each
//   [381] Háje            [+2] [B]  7:28    7:28 7:43
//
// A trip: its rides, one per row with the stop to board at, or its
// alternatives, one per row with the lines and the arrival:
//
//   [17] 8»B»S9           [+1]      7 m     7:12 7:20  »7:30
//
// Sizes S / M / L = 2 / 3 / 4 rows (Settings). When the lock screen runs out
// of room, rows go first (down to one), then the widget. The time column
// shows minutes to go ("6 m") or, by a setting, clock times; the minutes
// ride on the lock screen's own redraw each minute for the clock.
// =============================================================================

#if defined(MECK_TRANSIT) && MECK_TRANSIT && defined(MECK_UI_V2)

#include "MeckTransit.h"
#include "ui2/Ui2Canvas.h"
#include "ui2/Ui2LockWidgets.h"

struct LockTransitWidget : Ui2LockWidget {
  static constexpr int16_t HEAD_H = 14;
  static constexpr int16_t ROW_H = 18;
  static constexpr int16_t GAP = 4;
  static constexpr int16_t THEN_W = 58;     // "then": 4 px pad and two times
  static constexpr int16_t PLAT_W = 14;
  static constexpr int16_t CHIP_MIN_W = 24;

  const uint8_t* enabled = nullptr;   // NodePrefs::lock_transit
  const uint8_t* size = nullptr;      // NodePrefs::lock_transit_rows (2-4)
  const uint8_t* countdown = nullptr; // NodePrefs::lock_transit_countdown
  const int8_t* utcHours = nullptr;   // NodePrefs::utc_offset_hours
  int fitRows = 0;                    // rows the last layout gave it (0 = not drawn yet)

  int32_t off() const { return utcHours ? (int32_t)*utcHours * 3600 : 0; }
  int sizeRows() const {
    int n = size ? *size : 3;
    return n < TR_MIN_ROWS ? TR_MIN_ROWS : n > TR_MAX_ROWS ? TR_MAX_ROWS : n;
  }
  // What the server is asked for: more rows than drawn, kept in RAM, so a
  // board moves on by itself when a run leaves (trBoardAt)
  int requestRows() const { return TR_KEEP_ROWS; }

  // The board as it stands this minute, worked out once per minute or reply
  mutable TrBoard shownBoard = {};
  mutable uint32_t shownMin = 0xFFFFFFFFu, shownVer = 0;
  const TrBoard& shown(uint32_t now) const {
    const uint32_t min = now / 60;
    if (min != shownMin || meckTransit.version() != shownVer) {
      trBoardAt(meckTransit.board(), now, off(), shownBoard);
      shownMin = min;
      shownVer = meckTransit.version();
    }
    return shownBoard;
  }

  static int16_t heightFor(int rows) { return 1 + HEAD_H + rows * ROW_H; }

  TrView view(uint32_t now) const {
    TrView v = {};
    const bool on = enabled && *enabled && meckTransit.configured();
    if (!on && !meckTransit.demoActive()) { v.state = TR_HIDDEN; return v; }
    return trViewAt(shown(now), meckTransit.have(), meckTransit.lastOk(), now, off());
  }

  int wantRows(uint32_t now) const {
    TrView v = view(now);
    if (v.state == TR_HIDDEN) return 0;
    if (v.state == TR_EMPTY) return 1;
    int n = shown(now).nRows;
    return n < sizeRows() ? n : sizeRows();
  }

  uint8_t priority() const override { return 15; }   // below the agenda

  int16_t height(int16_t, uint32_t now) override {
    int n = wantRows(now);
    return n ? heightFor(n) : 0;
  }

  int16_t shrink(int16_t, uint32_t now, int16_t h) override {
    if (view(now).state != TR_ROWS) return 0;
    int n = (h - 1 - HEAD_H) / ROW_H;
    return n > 1 ? heightFor(n - 1) : 0;
  }

  void draw(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, uint32_t now) override {
    drawAt(c, x, top, w, height(w, now), now);
  }

  void drawAt(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, int16_t h, uint32_t now) override {
    const TrView v = view(now);
    if (v.state == TR_HIDDEN || h <= 0) return;
    const TrBoard& b = shown(now);
    int rows = (h - 1 - HEAD_H) / ROW_H;
    if (v.state == TR_ROWS) {
      if (rows > b.nRows) rows = b.nRows;
      fitRows = rows;
    }
    c.hline(x, top, w);

    // Columns shared by all rows: the "then" column only when a row has
    // one, the platform column only when a row has a platform
    // and the arrival column only for a trip's alternatives
    bool anyThen = false, anyPlat = false;
    int16_t arrW = 0;
    for (int i = 0; v.state == TR_ROWS && i < rows; i++) {
      anyThen |= b.rows[i].then[0].hm[0] != 0;
      anyPlat |= b.rows[i].plat[0] != 0 && !b.rows[i].arr[0];
      int16_t aw = b.rows[i].arr[0] ? Ui2Canvas::textWidth(b.rows[i].arr, UI2_F_NODE) + GAP : 0;
      if (aw > arrW) arrW = aw;
    }
    const int16_t right = x + w;
    const int16_t thenX = right - arrW - (anyThen ? THEN_W : 0);

    // Header: title in caps, "then" (or the stale mark) in the right slot
    const int16_t hb = top + 1 + HEAD_H - 3;   // baseline on the bottom of the box
    int16_t slotX = right - THEN_W;
    if (v.stale) {
      // A reply that is out of date or did not come: refresh icon and the
      // time of the last good reply (warn icon when there never was one)
      int16_t ix = slotX;
      if (v.staleAt[0]) {
        int16_t tw = Ui2Canvas::textWidth(v.staleAt, UI2_F_META);
        ix = right - tw - 14;
        c.icon(ix, top + 2, UI2_IC_REFRESH, 12);
        c.text(ix + 14, hb, v.staleAt, UI2_F_META);
      } else {
        ix = right - 12;
        c.icon(ix, top + 2, UI2_IC_WARN, 12);
      }
      slotX = ix;
    } else if (v.state == TR_ROWS && anyThen) {
      c.textFit(thenX, hb, THEN_W, b.thenLabel, UI2_F_META);
    }
    {
      // Without a board (never a reply, or another server's) the widget's name
      const char* title = meckTransit.have() && b.title[0] ? b.title : "ODJEZDY";
      int16_t tw = (v.stale || anyThen ? slotX : right) - GAP - x;
      if (tw > 0) c.textFit(x, hb, tw, title, UI2_F_CHIP);
    }

    int16_t y = top + 1 + HEAD_H;
    if (v.state == TR_EMPTY) {
      const char* msg = b.empty[0] && meckTransit.have() ? b.empty : "Nic k zobrazen\xC3\xAD";
      c.textFit(x, Ui2Canvas::lineBaseline(UI2_F_SMALL, y, ROW_H), w, msg, UI2_F_SMALL);
      return;
    }
    // The time column is as wide as the widest time on the board
    char tm[TR_MAX_ROWS][8];
    int16_t timeW = 0;
    for (int i = 0; i < rows; i++) {
      trRowTime(b, i, countdown && *countdown, now, off(), tm[i], sizeof(tm[i]));
      int16_t tw = Ui2Canvas::textWidth(tm[i], UI2_F_NODE);
      if (tw > timeW) timeW = tw;
    }
    for (int i = 0; i < rows; i++, y += ROW_H)
      drawRow(c, x, y, thenX, right, timeW, anyPlat, b.rows[i], tm[i]);
  }

private:
  // A 2 px dot at the top right of a time: the run is >= 5 min late
  static void lateDot(Ui2Canvas& c, int16_t xEnd, int16_t baseline, Ui2Font f) {
    c.fill(xEnd + 1, baseline - Ui2Canvas::capHeight(f) - 1, 2, 2);
  }

  void drawRow(Ui2Canvas& c, int16_t x, int16_t y, int16_t thenX, int16_t right, int16_t timeW, bool anyPlat, const TrRow& r, const char* tm) {
    int16_t lx = x;
    // Line chip, inverted
    if (r.line[0]) {
      int16_t cw = Ui2Canvas::textWidth(r.line, UI2_F_NUM10) + 6;
      if (cw < CHIP_MIN_W) cw = CHIP_MIN_W;
      c.fillRoundRect(lx, y + 2, cw, 14, 2);
      c.textFit(lx, Ui2Canvas::boxBaseline(UI2_F_NUM10, y + 2, 14), cw, r.line, UI2_F_NUM10, false, UI2_CENTER);
      lx += cw + GAP;
    }
    // From the right: then, time (+ room for its dot), platform, delay chip
    int16_t rx = thenX;
    if (r.then[0].hm[0]) {
      int16_t tx = thenX + 4;
      const int16_t base = Ui2Canvas::lineBaseline(UI2_F_META, y, ROW_H);
      for (int k = 0; k < TR_THEN && r.then[k].hm[0]; k++) {
        int16_t e = c.text(tx, base, r.then[k].hm, UI2_F_META);
        if (r.then[k].late) lateDot(c, e, base, UI2_F_META);
        tx = e + 6;
      }
    }
    // The arrival of an alternative, bold at the right edge
    if (r.arr[0])
      c.text(right - Ui2Canvas::textWidth(r.arr, UI2_F_NODE), Ui2Canvas::lineBaseline(UI2_F_NODE, y, ROW_H), r.arr, UI2_F_NODE);
    rx -= 3;   // the dot of a late time
    const int16_t tb = Ui2Canvas::lineBaseline(UI2_F_NODE, y, ROW_H);
    const int16_t tl = rx - timeW;
    if (tm[0]) {
      int16_t tw = Ui2Canvas::textWidth(tm, UI2_F_NODE);
      int16_t e = c.text(rx - tw, tb, tm, UI2_F_NODE);
      if (r.time.late) lateDot(c, e, tb, UI2_F_NODE);
    }
    rx = tl - GAP;
    if (anyPlat) {
      if (r.plat[0] && !r.arr[0]) {
        const int16_t py = y + 2;
        c.roundRect(rx - PLAT_W, py, PLAT_W, PLAT_W, 2);
        if (r.platChanged) c.roundRect(rx - PLAT_W + 1, py + 1, PLAT_W - 2, PLAT_W - 2, 1);
        c.textFit(rx - PLAT_W, Ui2Canvas::boxBaseline(UI2_F_CHIP, py, PLAT_W), PLAT_W, r.plat, UI2_F_CHIP, true, UI2_CENTER);
      }
      rx -= PLAT_W + GAP;
    }
    if (r.badge[0]) {
      int16_t bw = Ui2Canvas::textWidth(r.badge, UI2_F_KEY) + 6;
      const int16_t by = y + 3;
      c.roundRect(rx - bw, by, bw, 12, 2);
      c.text(rx - bw + 3, Ui2Canvas::boxBaseline(UI2_F_KEY, by, 12), r.badge, UI2_F_KEY);
      rx -= bw + GAP;
    }
    if (r.text[0] && rx > lx)
      c.textFit(lx, Ui2Canvas::lineBaseline(UI2_F_SMALL, y, ROW_H), rx - lx, r.text, UI2_F_SMALL);
  }
};

#endif
