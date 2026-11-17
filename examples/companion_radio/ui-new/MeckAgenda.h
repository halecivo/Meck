#pragma once
// =============================================================================
// The calendar events of the next days, for the lock screen agenda
// (docs/calendar-account.md, docs/lock-screen.md)
//
// MeckDav fills it from the account's calendars (read only); the lock screen
// widget reads it on every redraw, so everything here works on RAM only. A
// copy goes to /cal/agenda.bin on the SD card so the agenda is there after a
// reboot, before WiFi.
//
// Times are RTC seconds (UTC). The deck's local time is UTC plus a whole hour
// offset (NodePrefs::utc_offset_hours), as everywhere else on the deck.
// =============================================================================

#if defined(MECK_DAV) && MECK_DAV

#include <stdint.h>
#include "MeckDavUtil.h"

class MeckAgenda {
public:
  static constexpr int MAX_EVENTS = 128;
  static constexpr int WINDOW_DAYS = 7;     // what a sync asks for, from local today 00:00

  void load();                               // setup(): the SD copy, if any
  void clear();                              // forget the events and the SD copy

  // A sync's result: events of [winStart, winStart + WINDOW_DAYS days), any
  // order; the earliest MAX_EVENTS are kept. Written to the SD card.
  void replace(const DavEvent* ev, int n, uint32_t winStart, bool persist = true);
  // Debug CLI: made-up events for today and tomorrow, RAM only (the next
  // sync or a reboot brings the real ones back)
  void demo(int today, int tomorrow, uint32_t now, int32_t offSec);

  // Whether the events cover today and tomorrow (a sync worked, not too long ago)
  bool valid(uint32_t now, int32_t offSec) const;
  int count() const { return _n; }
  const DavEvent& event(int i) const { return _ev[i]; }
  uint32_t windowStart() const { return _winStart; }

  // Events of local day `day` (0 today, 1 tomorrow): all-day ones and ones
  // going on from the day before first, then by start. Today leaves out what
  // has ended. Up to max into out; returns how many there are in all.
  int dayEvents(int day, uint32_t now, int32_t offSec, const DavEvent** out, int max) const;
  // Next time the agenda changes by itself: an event of today ends, or midnight
  uint32_t nextChange(uint32_t now, int32_t offSec) const;

  static uint32_t localMidnight(uint32_t now, int32_t offSec) {
    int64_t l = (int64_t)now + offSec;
    return (uint32_t)((l - l % 86400) - offSec);
  }

private:
  DavEvent* _ev = nullptr;   // PSRAM, sorted by start
  int _n = 0;
  uint32_t _winStart = 0;    // 0 = no events known
  bool alloc();
  void save() const;
};

MeckAgenda& meckAgendaGet();
#define meckAgenda (meckAgendaGet())

// main.cpp: the agenda changed, redraw the lock screen if it shows it
void meckAgendaChanged();

#endif
