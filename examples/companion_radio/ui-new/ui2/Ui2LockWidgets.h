#pragma once
// =============================================================================
// UI v2 -- what the lock screen shows besides the clock (docs/lock-screen.md)
//
// Widgets (agenda, departures, ...) are small blocks that stack upwards from
// the status line at the bottom. When they do not all fit next to the clock,
// the date and the notifications, the lowest priority ones are left out: the
// clock never shrinks. The weather group sits left of the clock.
//
// Providers own their widget and data; they register once with
// UITask::addLockWidget() / setLockWeather() and call
// UITask::lockContentChanged() when what they show changes.
// =============================================================================

#include <stdint.h>
#include "Ui2Assets.h"

class Ui2Canvas;

struct Ui2LockWidget {
  virtual ~Ui2LockWidget() {}
  // Higher stays longer when space runs out
  virtual uint8_t priority() const = 0;
  // Height it needs at width w now; 0 = nothing to show (no block, no gap)
  virtual int16_t height(int16_t w, uint32_t now) = 0;
  // Draw in [x, x + w) from `top`, with its 1 px top rule at `top`
  virtual void draw(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, uint32_t now) = 0;
  // Next time (RTC seconds) its content changes by itself, e.g. an event
  // ends; 0 = none. The lock screen redraws then even off the minute tick.
  virtual uint32_t nextChange(uint32_t now) { (void)now; return 0; }
};

// The weather group left of the clock; valid = false draws the clock alone
struct Ui2LockWeather {
  enum Rain : uint8_t { RAIN_NONE, RAIN_STARTS, RAIN_ENDS };
  bool valid = false;
  Ui2Icon icon = UI2_IC_CLOUD;   // drawn at 32 px
  int8_t tempC = 0;
  Rain rain = RAIN_NONE;
  uint16_t rainMin = 0;          // local minute of the day it starts / ends
};

static const int UI2_LOCK_MAX_WIDGETS = 4;
