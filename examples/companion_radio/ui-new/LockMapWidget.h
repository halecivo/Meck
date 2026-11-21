#pragma once
// =============================================================================
// Lock screen map (Settings > Experimental, docs/lock-map.md): with GPS on and
// a fresh fix, the map takes all the room the lock screen has for widgets (no
// other widget shows) and is centred on the deck, at the best zoom the SD
// card has. It is drawn by MapScreen, as the map screen draws: same tiles,
// same decoder, same own-position marker.
//
// The main loop (meckLockMapTick) decides whether there is a position and
// tiles under it, and hands the position in; this widget only draws.
// =============================================================================

#if defined(MECK_UI_V2) && HAS_GPS

#include "MapScreen.h"
#include "ui2/Ui2Canvas.h"
#include "ui2/Ui2LockWidgets.h"

struct LockMapWidget : Ui2LockWidget {
  MapScreen* map = nullptr;
  void (*boost)() = nullptr;   // the PNG decode wants the CPU at full speed
  bool visible = false;        // a fresh fix, and tiles under it
  double lat = 0, lon = 0;

  uint8_t priority() const override { return 255; }
  bool fillsLock() const override { return true; }

  // Whatever it is given; the layout hands it the whole room (fillsLock).
  // Too little room and nothing shrinks it: it is left out.
  int16_t height(int16_t, uint32_t) override { return visible && map ? 120 : 0; }

  void draw(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, uint32_t now) override {
    drawAt(c, x, top, w, height(w, now), now);
  }

  void drawAt(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, int16_t h, uint32_t) override {
    if (!visible || !map || h < 8 || w < 8) return;
    if (boost) boost();
    // 1 px frame, the map inside it
    map->renderInRect(c.raw(), x + 1, top + 1, w - 2, h - 2, lat, lon);
    c.rect(x, top, w, h);
  }
};

#endif
