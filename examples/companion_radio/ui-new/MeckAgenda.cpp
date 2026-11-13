// Calendar events for the lock screen agenda -- see MeckAgenda.h
#if defined(MECK_DAV) && MECK_DAV

#include "MeckAgenda.h"
#include <Arduino.h>
#include <SD.h>
#include <new>
#include <esp_heap_caps.h>

#define AGENDA_DIR   "/cal"
#define AGENDA_FILE  "/cal/agenda.bin"
#define AGENDA_TMP   "/cal/agenda.tmp"
#define AGENDA_MAGIC 0x31414741u   // "AGA1"

MeckAgenda& meckAgendaGet() {
  static MeckAgenda* inst = nullptr;
  if (!inst) {
    void* p = heap_caps_calloc(1, sizeof(MeckAgenda), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = calloc(1, sizeof(MeckAgenda));
    inst = new (p) MeckAgenda();
  }
  return *inst;
}

bool MeckAgenda::alloc() {
  if (_ev) return true;
  _ev = (DavEvent*)heap_caps_calloc(MAX_EVENTS, sizeof(DavEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return _ev != nullptr;
}

static int agendaCmp(const void* a, const void* b) {
  const DavEvent* x = (const DavEvent*)a;
  const DavEvent* y = (const DavEvent*)b;
  if (x->start != y->start) return x->start < y->start ? -1 : 1;
  if (x->allDay != y->allDay) return x->allDay ? -1 : 1;
  return strcmp(x->title, y->title);
}

void MeckAgenda::replace(const DavEvent* ev, int n, uint32_t winStart, bool persist) {
  if (!alloc()) return;
  // The caller's list may be larger: sort it there, keep the earliest
  DavEvent* src = (DavEvent*)ev;
  qsort(src, n, sizeof(DavEvent), agendaCmp);
  _n = n < MAX_EVENTS ? n : MAX_EVENTS;
  memcpy(_ev, src, _n * sizeof(DavEvent));
  _winStart = winStart;
  if (persist) save();
}

void MeckAgenda::demo(int today, int tomorrow, uint32_t now, int32_t offSec) {
  static const char* const titles[] = { "Standup", "PI planning, ZÃ¼rich call", "Bike service",
                                        "Pick up parcel at ÅÃ­Ä" "any post office",
                                        "Hams meetup", "Cinema" };
  DavEvent ev[12];
  int n = 0;
  uint32_t d0 = localMidnight(now, offSec);
  for (int day = 0; day < 2; day++) {
    int k = day == 0 ? today : tomorrow;
    for (int i = 0; i < k && n < 12; i++, n++) {
      // Today's from now on, so none has ended yet; tomorrow's from 9:00
      uint32_t base = day == 0 ? now + 600 : d0 + 86400u + 9 * 3600;
      if (day == 0 && base + i * 3600u >= d0 + 86400u) base = d0 + 86400u - 3600u * (k - i);
      memset(&ev[n], 0, sizeof(ev[n]));
      ev[n].start = base + (uint32_t)i * 3600u;
      ev[n].start -= ev[n].start % 300;
      ev[n].end = ev[n].start + 1800;
      snprintf(ev[n].title, sizeof(ev[n].title), "%s", titles[i % 6]);
      davTrimUtf8(ev[n].title);
    }
  }
  replace(ev, n, d0, false);
}

void MeckAgenda::clear() {
  _n = 0;
  _winStart = 0;
  SD.remove(AGENDA_FILE);
}

void MeckAgenda::save() const {
  if (!SD.exists(AGENDA_DIR)) SD.mkdir(AGENDA_DIR);
  File f = SD.open(AGENDA_TMP, FILE_WRITE);
  if (!f) { Serial.println("Agenda: cannot write the SD copy"); return; }
  uint32_t head[4] = { AGENDA_MAGIC, (uint32_t)sizeof(DavEvent), _winStart, (uint32_t)_n };
  bool ok = f.write((const uint8_t*)head, sizeof(head)) == sizeof(head);
  if (ok && _n) ok = f.write((const uint8_t*)_ev, _n * sizeof(DavEvent)) == _n * sizeof(DavEvent);
  f.close();
  if (!ok) { SD.remove(AGENDA_TMP); return; }
  SD.remove(AGENDA_FILE);
  SD.rename(AGENDA_TMP, AGENDA_FILE);
}

void MeckAgenda::load() {
  File f = SD.open(AGENDA_FILE, FILE_READ);
  if (!f) return;
  uint32_t head[4] = {};
  bool ok = f.read((uint8_t*)head, sizeof(head)) == sizeof(head) && head[0] == AGENDA_MAGIC &&
            head[1] == sizeof(DavEvent) && head[3] <= (uint32_t)MAX_EVENTS && alloc();
  if (ok && head[3]) ok = f.read((uint8_t*)_ev, head[3] * sizeof(DavEvent)) == (int)(head[3] * sizeof(DavEvent));
  f.close();
  if (!ok) { _n = 0; _winStart = 0; return; }
  _n = (int)head[3];
  _winStart = head[2];
  for (int i = 0; i < _n; i++) _ev[i].title[sizeof(_ev[i].title) - 1] = 0;
  Serial.printf("Agenda: %d event(s) from the SD copy\n", _n);
}

bool MeckAgenda::valid(uint32_t now, int32_t offSec) const {
  if (!_winStart || now < 1700000000u) return false;
  uint32_t today = localMidnight(now, offSec);
  uint32_t winEnd = _winStart + WINDOW_DAYS * 86400u;
  // An old copy says nothing about today: better no agenda than an empty one
  return today >= _winStart && today + 2 * 86400u <= winEnd;
}

// Over: end passed (an event without length lasts its minute)
static bool agendaEnded(const DavEvent& e, uint32_t now) {
  uint32_t end = e.end > e.start ? e.end : e.start + 60;
  return end <= now;
}

int MeckAgenda::dayEvents(int day, uint32_t now, int32_t offSec, const DavEvent** out, int max) const {
  uint32_t d0 = localMidnight(now, offSec) + (uint32_t)day * 86400u;
  uint32_t d1 = d0 + 86400u;
  int total = 0, o = 0;
  // Two passes: all-day (and going on from before) first, then the timed ones
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < _n; i++) {
      const DavEvent& e = _ev[i];
      bool in = e.end > e.start ? (e.start < d1 && e.end > d0) : (e.start >= d0 && e.start < d1);
      if (!in) continue;
      bool whole = e.allDay || e.start < d0;
      if ((pass == 0) != whole) continue;
      if (day == 0 && !e.allDay && agendaEnded(e, now)) continue;
      if (o < max && out) out[o++] = &e;
      total++;
    }
  }
  return total;
}

uint32_t MeckAgenda::nextChange(uint32_t now, int32_t offSec) const {
  uint32_t next = localMidnight(now, offSec) + 86400u;
  for (int i = 0; i < _n; i++) {
    const DavEvent& e = _ev[i];
    if (e.allDay) continue;
    uint32_t end = e.end > e.start ? e.end : e.start + 60;
    if (end > now && end < next) next = end;
  }
  return next;
}

#endif
