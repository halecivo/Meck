#pragma once
// =============================================================================
// Where the deck is, from several sources, each with how far off it may be
// (docs/position.md). Plain C++ (no Arduino), so test/position runs it on a PC.
//
// A fix carries a radius in metres. Apps ask for an accuracy class, not for
// a source: the map and anything that shares the position want FINE, the
// weather is happy with AREA. The best fix that is good enough wins; a
// position from the cell tower is never FINE, so it can feed the weather but
// never the map, an advert or an SOS.
//
//   MeckFix f;
//   if (meckPos.best(POS_AREA, now, f)) ... f.lat, f.lon, f.radiusM
// =============================================================================

#include <stdint.h>
#include <string.h>

// Accuracy classes, best first; a fix belongs to the first whose limit its
// radius fits in
enum PosClass : uint8_t {
  POS_FINE,     // <= 100 m: GPS. Maps, sharing the position, adverts, SOS
  POS_LOCAL,    // <= 2 km: a small cell in town. Nearby things (stops, POIs)
  POS_AREA,     // <= 50 km: a cell, or the cell's area. Weather
  POS_REGION,   // <= 500 km: the cell's tracking area at worst. Country, time zone
  POS_CLASS_COUNT
};

static const uint32_t POS_CLASS_LIMIT_M[POS_CLASS_COUNT] = { 100, 2000, 50000, 500000 };

enum PosSource : uint8_t {
  POS_SRC_GPS,      // a live fix of the deck's GPS
  POS_SRC_CELL,     // the serving cell, looked up in the SD card's cell database
  POS_SRC_STORED,   // the advert position (set by hand or the last GPS fix); has no time
  POS_SRC_COUNT
};

struct MeckFix {
  double lat, lon;
  uint32_t radiusM;   // how far off it may be, at the time it was taken
  uint32_t at;        // seconds it was taken (0 for a stored position)
  PosSource src;
  uint32_t holdS;     // it stays this good for so long after `at` (a cell the
                      // modem keeps confirming), then ages
};

static inline PosClass posClassOf(uint32_t radiusM) {
  for (int c = 0; c < POS_CLASS_COUNT; c++)
    if (radiusM <= POS_CLASS_LIMIT_M[c]) return (PosClass)c;
  return POS_CLASS_COUNT;   // worse than any class
}

static inline const char* posClassName(uint8_t c) {
  static const char* const n[] = { "fine", "local", "area", "region", "none" };
  return n[c < POS_CLASS_COUNT ? c : POS_CLASS_COUNT];
}

static inline const char* posSourceName(uint8_t s) {
  static const char* const n[] = { "gps", "cell", "stored", "?" };
  return n[s < POS_SRC_COUNT ? s : POS_SRC_COUNT];
}

class MeckPosition {
public:
  // A fix gets worse as it ages: the deck may have moved since. 15 m/s is
  // a car in town; a GPS fix is FINE for a few seconds only by this, so the
  // GPS feeds a new fix every loop while it has one.
  static const uint32_t DRIFT_M_PER_S = 15;
  // A stored position counts as this good: it is where the user wants the
  // deck to be (or where its GPS last was), as today
  static const uint32_t STORED_RADIUS_M = 100;

  void set(const MeckFix& f) {
    if (f.src >= POS_SRC_COUNT) return;
    _fix[f.src] = f;
    _have[f.src] = true;
  }
  void clear(PosSource s) { if (s < POS_SRC_COUNT) _have[s] = false; }
  bool has(PosSource s) const { return s < POS_SRC_COUNT && _have[s]; }
  const MeckFix& fix(PosSource s) const { return _fix[s]; }

  // The fix's radius now: what it was plus how far the deck may have gone
  uint32_t radiusNow(const MeckFix& f, uint32_t now) const {
    if (f.src == POS_SRC_STORED || !f.at) return f.radiusM;
    uint32_t age = now > f.at ? now - f.at : 0;
    age = age > f.holdS ? age - f.holdS : 0;
    uint64_t r = (uint64_t)f.radiusM + (uint64_t)age * DRIFT_M_PER_S;
    return r > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)r;
  }

  // The best fix of at least class `need` now: a live one (GPS, cell) with
  // the smallest radius, else the stored position when allowed. out.radiusM
  // is the radius now.
  bool best(PosClass need, uint32_t now, MeckFix& out, bool allowStored = true) const {
    if (need >= POS_CLASS_COUNT) return false;
    const uint32_t limit = POS_CLASS_LIMIT_M[need];
    bool found = false;
    uint32_t bestR = 0;
    for (int s = 0; s < POS_SRC_COUNT; s++) {
      if (!_have[s] || s == POS_SRC_STORED) continue;
      uint32_t r = radiusNow(_fix[s], now);
      if (r > limit || (found && r >= bestR)) continue;
      out = _fix[s];
      out.radiusM = r;
      bestR = r;
      found = true;
    }
    if (!found && allowStored && _have[POS_SRC_STORED] && _fix[POS_SRC_STORED].radiusM <= limit) {
      out = _fix[POS_SRC_STORED];
      found = true;
    }
    return found;
  }

private:
  MeckFix _fix[POS_SRC_COUNT] = {};
  bool _have[POS_SRC_COUNT] = {};
};
