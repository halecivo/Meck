#pragma once
// =============================================================================
// Lock screen agenda (docs/lock-screen.md): today and tomorrow side by side,
// from the synced calendars (MeckAgenda). Design: "Lock screen v3", V2 / V5j.
//
// The block is 40, 52 or 70 px tall, as today has 0-2, 3 or 4+ events left;
// that gives 2, 3 or 4 rows per day. A day with more events than rows shows
// one row less and "+n more" in the last. Shown only when switched on
// (Settings > Experimental) and the agenda covers today.
// =============================================================================

#if defined(MECK_DAV) && MECK_DAV && defined(MECK_UI_V2)

#include "MeckAgenda.h"
#include "MeckLang.h"
#include "ui2/Ui2Canvas.h"
#include "ui2/Ui2LockWidgets.h"

struct LockAgendaWidget : Ui2LockWidget {
  static constexpr int16_t COL_GAP = 8;
  static constexpr int16_t LINE_H = 12;
  static constexpr int16_t PAD_X = 3;

  const uint8_t* enabled = nullptr;   // NodePrefs::lock_agenda
  const int8_t* utcHours = nullptr;   // NodePrefs::utc_offset_hours

  int32_t off() const { return utcHours ? (int32_t)*utcHours * 3600 : 0; }

  uint8_t priority() const override { return 20; }

  int16_t height(int16_t, uint32_t now) override {
    if (!enabled || !*enabled || !meckAgenda.valid(now, off())) return 0;
    int left = meckAgenda.dayEvents(0, now, off(), nullptr, 0);
    return left <= 2 ? 40 : left == 3 ? 52 : 70;
  }

  uint32_t nextChange(uint32_t now) override { return meckAgenda.nextChange(now, off()); }

  void draw(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, uint32_t now) override {
    int16_t h = height(w, now);
    if (!h) return;
    const int rows = (h - 2 - LINE_H) / LINE_H;
    const int16_t colW = (w - COL_GAP) / 2;
    for (int day = 0; day < 2; day++)
      drawDay(c, x + day * (colW + COL_GAP), top, colW, rows, day, now);
  }

private:
  void drawDay(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, int rows, int day, uint32_t now) {
    c.hline(x, top, w);
    // Header: "Today Fri 13" inverted, "Tomorrow Sat 14" plain
    const int16_t hy = top + 2;
    const bool inv = day == 0;
    if (inv) c.fillRoundRect(x, hy, w, LINE_H, 2);
    const int16_t base = Ui2Canvas::lineBaseline(UI2_F_NODE, hy, LINE_H);
    const char* label = day == 0 ? MECK_TR("Today", "Auj.") : MECK_TR("Tomorrow", "Demain");
    int16_t px = c.text(x + PAD_X, base, label, UI2_F_NODE, !inv);
    char date[16];
    dayLabel(now + (uint32_t)day * 86400u, date, sizeof(date));
    c.textFit(px + 3, base, x + w - PAD_X - (px + 3), date, UI2_F_META, !inv);

    const DavEvent* ev[4];
    const int total = meckAgenda.dayEvents(day, now, off(), ev, 4);
    const int shown = total > rows ? rows - 1 : total;
    int16_t y = hy + LINE_H;
    const int16_t tw = w - 2 * PAD_X;
    if (total == 0) {
      c.textFit(x + PAD_X, Ui2Canvas::lineBaseline(UI2_F_META, y, LINE_H), tw,
                MECK_TR("No events", "Rien"), UI2_F_META);
      return;
    }
    const uint32_t d0 = MeckAgenda::localMidnight(now, off()) + (uint32_t)day * 86400u;
    for (int i = 0; i < shown; i++, y += LINE_H) {
      const DavEvent& e = *ev[i];
      const int16_t b = Ui2Canvas::lineBaseline(UI2_F_META, y, LINE_H);
      int16_t tx = x + PAD_X;
      if (!e.allDay && e.start >= d0) {   // timed, starts this day: its time first
        char hm[8];
        int32_t l = (int32_t)(e.start + off());
        snprintf(hm, sizeof(hm), "%d:%02d", (int)(l / 3600 % 24), (int)(l / 60 % 60));
        tx = c.text(tx, b, hm, UI2_F_CHIP) + 3;
      }
      const char* title = e.title[0] ? e.title : MECK_TR("(no title)", "(sans titre)");
      if (tx < x + w - PAD_X) c.textFit(tx, b, x + w - PAD_X - tx, title, UI2_F_META);
    }
    if (total > shown) {
      char more[20];
      snprintf(more, sizeof(more), MECK_TR("+%d more", "+%d autres"), total - shown);
      c.textFit(x + PAD_X, Ui2Canvas::lineBaseline(UI2_F_META, y, LINE_H), tw, more, UI2_F_META);
    }
  }

  // "Fri 13" of the local day `t` is in
  void dayLabel(uint32_t t, char* out, size_t n) const {
    static const char* const en[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    static const char* const fr[] = {"jeu", "ven", "sam", "dim", "lun", "mar", "mer"};
    int32_t days = (int32_t)(((int64_t)t + off()) / 86400);
    int wd = (int)(days % 7);
    char s[20];
    davIcalUtcStamp((uint32_t)days * 86400u, s, sizeof(s));   // "YYYYMMDD..."
    int d = (s[6] - '0') * 10 + (s[7] - '0');
    snprintf(out, n, "%s %d", MECK_TR(en[wd], fr[wd]), d);
  }
};

#endif
