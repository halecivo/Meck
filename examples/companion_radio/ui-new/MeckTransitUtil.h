#pragma once
// =============================================================================
// Lock screen departures (docs/transit-widget.md): the row model, reading the
// Neujede /compact/widget reply, the request URL and when to ask next.
// Plain C++ (no Arduino), so test/transit runs it on a PC.
//
// The server does the thinking (which stop or trip, which runs, names
// already shortened); the deck only draws ready rows:
//
//   [S9] Praha hl.n.      [+2] [1]  7:45   7:53 8:23
//   line text (ellipsis)  delay plat time  then (up to two)
//
// A trip is either its rides, one per row ("[17] Braník", where to board),
// or its alternatives, one per row with the arrival at the end:
//
//   [17] 8»B»S9           [+1]      7:04   7:12 7:20  »7:30
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int TR_MAX_ROWS = 4;
static const int TR_MIN_ROWS = 2;      // the server is asked for 2..4 rows
static const int TR_THEN = 2;

// A request no more often than this (WiFi), and over mobile data
static const uint32_t TR_MIN_INTERVAL_S = 120;
static const uint32_t TR_MIN_INTERVAL_4G_S = 300;
// A reply without validUntil is asked again after this; one with a very
// distant validUntil (night) after this at the latest
static const uint32_t TR_DEFAULT_VALID_S = 600;
static const uint32_t TR_MAX_VALID_S = 4UL * 3600;
// Back-off after failed requests: 2, 4, 8, then 15 min
static const uint32_t TR_BACKOFF_MAX_S = 15UL * 60;
// Rows older than this are not shown any more (the empty state with the
// stale mark instead): their departures are long gone
static const uint32_t TR_MAX_AGE_S = 2UL * 3600;

struct TrTime {
  char hm[8];   // "7:45" (no leading zero), "" = none
  bool late;    // the run is >= 5 min late: a 2 px dot after the time
};

struct TrRow {
  char line[8];      // inverted chip: "S9", "17"; "" = no chip
  char text[48];     // direction, or "Braník → Palackého" for a trip leg
  char badge[8];     // outlined chip: "+2", "+8 h", "72 %"; "" = none
  char plat[4];      // platform box: "1", "B"; "" = none
  bool platChanged;  // 2 px border
  int16_t delay;     // minutes late (0 = on time or not known): the time is the timetable's
  TrTime time;       // bold time column
  TrTime then[TR_THEN];
  char arr[8];       // a whole alternative in one row: its arrival, "»7:30"; "" = a ride or a run
};

struct TrBoard {
  uint8_t v;              // format version of the reply
  char title[48];         // header, upper-cased: "ŘÍČANY, NÁDRAŽÍ", "DO PRÁCE 6:55→7:30"
  char thenLabel[12];     // right column label, "then" by default
  char empty[40];         // what an empty board says (server text, else ours)
  char kind[8];           // "board", "trip", "live", "none" (not used for drawing)
  char key[16];           // changes exactly when the drawn content does
  uint8_t more;           // trip rides that did not fit into the rows
  uint8_t nRows;
  TrRow rows[TR_MAX_ROWS];
  uint32_t fetched;       // RTC seconds the reply came
  uint32_t validUntil;    // RTC seconds; 0 = not given
};

// --- JSON: tolerant, just enough for this reply --------------------------------

static inline const char* trWs(const char* p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// The end (one past) of the JSON value at p: string, object, array or a bare
// word (number, true, false, null)
static inline const char* trJsonEnd(const char* p, const char* end) {
  if (p >= end) return nullptr;
  if (*p == '"') {
    for (p++; p < end && *p != '"'; p += (*p == '\\') ? 2 : 1) {}
    return p < end ? p + 1 : nullptr;
  }
  if (*p == '{' || *p == '[') {
    int depth = 0;
    for (; p < end; p++) {
      if (*p == '"') {
        for (p++; p < end && *p != '"'; p += (*p == '\\') ? 2 : 1) {}
        if (p >= end) return nullptr;
      } else if (*p == '{' || *p == '[') {
        depth++;
      } else if (*p == '}' || *p == ']') {
        if (--depth == 0) return p + 1;
      }
    }
    return nullptr;
  }
  while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
  return p;
}

// The value of "key" directly in the object at [p, end) (not in an inner one)
static inline const char* trJsonGet(const char* p, const char* end, const char* key) {
  p = trWs(p, end);
  if (p >= end || *p != '{') return nullptr;
  const size_t kl = strlen(key);
  p++;
  while (true) {
    p = trWs(p, end);
    if (p >= end || *p == '}') return nullptr;
    if (*p == ',') { p++; continue; }
    if (*p != '"') return nullptr;
    const char* ks = p + 1;
    const char* ke = trJsonEnd(p, end);
    if (!ke) return nullptr;
    p = trWs(ke, end);
    if (p >= end || *p != ':') return nullptr;
    p = trWs(p + 1, end);
    if (p >= end) return nullptr;
    if ((size_t)(ke - 1 - ks) == kl && memcmp(ks, key, kl) == 0) return p;
    p = trJsonEnd(p, end);
    if (!p) return nullptr;
  }
}

// The first of several names a field may go by
static inline const char* trJsonGetAny(const char* p, const char* end, const char* const* keys) {
  for (; *keys; keys++) {
    const char* v = trJsonGet(p, end, *keys);
    if (v) return v;
  }
  return nullptr;
}

static inline size_t trPutUtf8(char* out, size_t k, size_t n, uint32_t cp) {
  char b[4];
  size_t l;
  if (cp < 0x80) { b[0] = (char)cp; l = 1; }
  else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3F)); l = 2; }
  else if (cp < 0x10000) {
    b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); l = 3;
  } else {
    b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); l = 4;
  }
  if (k + l >= n) return k;   // never half a character
  memcpy(out + k, b, l);
  return k + l;
}

static inline int trHex4(const char* p, const char* end) {
  if (end - p < 4) return -1;
  int v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return -1;
  }
  return v;
}

// The value at v as text: a string (escapes undone, \uXXXX to UTF-8, cut at
// a whole character), or a number / word as written. null and objects: "".
static inline bool trJsonText(const char* v, const char* end, char* out, size_t n) {
  if (!n) return false;
  out[0] = 0;
  if (!v || v >= end) return false;
  size_t k = 0;
  if (*v == '"') {
    const char* p = v + 1;
    while (p < end && *p != '"') {
      uint32_t cp;
      if (*p == '\\' && p + 1 < end) {
        char e = p[1];
        p += 2;
        switch (e) {
          case 'n': case 'r': case 't': cp = ' '; break;
          case 'b': case 'f': cp = ' '; break;
          case 'u': {
            int h = trHex4(p, end);
            if (h < 0) return k > 0;
            p += 4;
            cp = (uint32_t)h;
            if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
              int lo = trHex4(p + 2, end);
              if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t)(lo - 0xDC00); p += 6; }
            }
            break;
          }
          default: cp = (uint8_t)e; break;
        }
        size_t nk = trPutUtf8(out, k, n, cp);
        if (nk == k) break;
        k = nk;
        continue;
      }
      // A raw UTF-8 sequence is copied whole or not at all
      uint8_t b = (uint8_t)*p;
      size_t l = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
      if (p + l > end || k + l >= n) break;
      memcpy(out + k, p, l);
      k += l;
      p += l;
    }
    out[k] = 0;
    return true;
  }
  if (*v == '{' || *v == '[') return false;
  const char* e = trJsonEnd(v, end);
  if (!e || (e - v == 4 && memcmp(v, "null", 4) == 0)) return false;
  size_t l = (size_t)(e - v);
  if (l >= n) l = n - 1;
  memcpy(out, v, l);
  out[l] = 0;
  return l > 0;
}

// A number (or a string holding one); false when there is none
static inline bool trJsonLong(const char* v, const char* end, long& out) {
  char b[24];
  if (!trJsonText(v, end, b, sizeof(b)) || !b[0]) return false;
  char* e;
  long x = strtol(b, &e, 10);
  if (e == b) return false;
  out = x;
  return true;
}

// true / 1 / "1" / "true"
static inline bool trJsonFlag(const char* v, const char* end) {
  char b[8];
  if (!trJsonText(v, end, b, sizeof(b))) return false;
  return strcmp(b, "true") == 0 || (b[0] >= '1' && b[0] <= '9');
}

// Calls f(element, end) for each element of the array at v; returns the count
template <typename F>
static inline int trJsonEach(const char* v, const char* end, F f) {
  if (!v || v >= end || *v != '[') return 0;
  const char* ae = trJsonEnd(v, end);
  if (!ae) return 0;
  const char* p = v + 1;
  int n = 0;
  while (true) {
    p = trWs(p, ae - 1);
    if (p >= ae - 1) break;
    if (*p == ',') { p++; continue; }
    const char* e = trJsonEnd(p, ae - 1);
    if (!e || e == p) break;
    f(p, e);
    n++;
    p = e;
  }
  return n;
}

// --- Text helpers --------------------------------------------------------------

// "07:45" -> "7:45"; anything that is no HH:MM is kept as it is (cut to fit)
static inline void trClock(const char* s, char* out, size_t n) {
  if (!n) return;
  if (s[0] == '0' && s[1] >= '0' && s[1] <= '9' && s[2] == ':') s++;
  snprintf(out, n, "%s", s);
}

// Upper case for ASCII, Latin-1 and Latin Extended-A (the Czech letters);
// in place, UTF-8 lengths do not change
static inline void trUpper(char* s) {
  for (uint8_t* p = (uint8_t*)s; *p;) {
    if (*p < 0x80) {
      if (*p >= 'a' && *p <= 'z') *p -= 32;
      p++;
    } else if ((*p >> 5) == 6 && (p[1] & 0xC0) == 0x80) {
      uint32_t cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      uint32_t up = cp;
      if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) up = cp - 0x20;
      else if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) up = cp & ~1u;
      else if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) up = (cp & 1) ? cp : cp - 1;
      if (up != cp && up >= 0x80 && up < 0x800) {
        p[0] = (uint8_t)(0xC0 | up >> 6);
        p[1] = (uint8_t)(0x80 | (up & 0x3F));
      }
      p += 2;
    } else {
      p++;
      while ((*p & 0xC0) == 0x80) p++;
    }
  }
}

// The UI v2 fonts end at U+017F: an arrow (U+2192, "Braník→Anděl",
// "6:55→7:30") would draw as nothing. It becomes "»" (U+00BB), in place.
static inline void trArrows(char* s) {
  char* w = s;
  for (const char* r = s; *r;) {
    if ((uint8_t)r[0] == 0xE2 && (uint8_t)r[1] == 0x86 && (uint8_t)r[2] == 0x92) {
      *w++ = (char)0xC2;
      *w++ = (char)0xBB;
      r += 3;
    } else {
      *w++ = *r++;
    }
  }
  *w = 0;
}

// Delay in minutes as the chip shows it: "+2", "+59", from 60 min in hours
// ("+1 h", "+8 h"); on time or early: no chip
static inline void trDelayChip(long min, char* out, size_t n) {
  if (min <= 0) { if (n) out[0] = 0; return; }
  if (min < 60) snprintf(out, n, "+%ld", min);
  else snprintf(out, n, "+%ld h", min / 60);
}

// --- The reply -------------------------------------------------------------------

// A time field: "07:45", or {"t":"07:45","late":1}; `lateKey` is a flag on
// the row next to it (main time)
static inline void trReadTime(const char* v, const char* end, TrTime& t) {
  static const char* const kT[] = { "t", "time", "hm", "dep", nullptr };
  static const char* const kLate[] = { "late", "l", "lt", nullptr };
  t.hm[0] = 0;
  t.late = false;
  if (!v) return;
  char b[12];
  if (*v == '{') {
    if (trJsonText(trJsonGetAny(v, end, kT), end, b, sizeof(b))) trClock(b, t.hm, sizeof(t.hm));
    t.late = trJsonFlag(trJsonGetAny(v, end, kLate), end);
  } else if (trJsonText(v, end, b, sizeof(b))) {
    trClock(b, t.hm, sizeof(t.hm));
  }
}

static inline void trReadRow(const char* r, const char* re, TrRow& row) {
  static const char* const kLine[] = { "line", "ln", "l", "r", "chip", nullptr };
  static const char* const kText[] = { "text", "tx", "t", "hs", "dir", nullptr };
  static const char* const kBadge[] = { "badge", "b", "dc", nullptr };
  static const char* const kDelay[] = { "dly", "delay", "d", nullptr };
  static const char* const kProb[] = { "p", "prob", nullptr };
  static const char* const kPlat[] = { "pl", "plat", "platform", nullptr };
  static const char* const kPlatX[] = { "pc", "plc", "plx", "platChanged", nullptr };
  static const char* const kTime[] = { "time", "tm", "dep", "at", nullptr };
  static const char* const kLate[] = { "late", "lt", nullptr };
  static const char* const kThen[] = { "then", "nx", "next", nullptr };
  static const char* const kThenLate[] = { "thenLate", "nxl", nullptr };
  memset(&row, 0, sizeof(row));
  trJsonText(trJsonGetAny(r, re, kLine), re, row.line, sizeof(row.line));
  trJsonText(trJsonGetAny(r, re, kText), re, row.text, sizeof(row.text));
  // The chip: ready text, else the delay in minutes, else a probability
  const char* bv = trJsonGetAny(r, re, kBadge);
  long x;
  if (!bv || !trJsonText(bv, re, row.badge, sizeof(row.badge))) {
    const char* dv = trJsonGetAny(r, re, kDelay);
    if (dv && *dv == '"') trJsonText(dv, re, row.badge, sizeof(row.badge));
    else if (trJsonLong(dv, re, x)) {
      trDelayChip(x, row.badge, sizeof(row.badge));
      if (x > 0) row.delay = (int16_t)(x > 1440 ? 1440 : x);
    }
    else if (trJsonLong(trJsonGetAny(r, re, kProb), re, x)) snprintf(row.badge, sizeof(row.badge), "%ld %%", x);
  }
  if (strcmp(row.badge, "0") == 0 || strcmp(row.badge, "+0") == 0) row.badge[0] = 0;
  trJsonText(trJsonGetAny(r, re, kPlat), re, row.plat, sizeof(row.plat));
  row.platChanged = trJsonFlag(trJsonGetAny(r, re, kPlatX), re);
  trReadTime(trJsonGetAny(r, re, kTime), re, row.time);
  if (trJsonFlag(trJsonGetAny(r, re, kLate), re)) row.time.late = true;
  int i = 0;
  trJsonEach(trJsonGetAny(r, re, kThen), re, [&](const char* e, const char* ee) {
    if (i < TR_THEN) trReadTime(e, ee, row.then[i++]);
  });
  i = 0;
  trJsonEach(trJsonGetAny(r, re, kThenLate), re, [&](const char* e, const char* ee) {
    if (i < TR_THEN && trJsonFlag(e, ee)) row.then[i].late = true;
    i++;
  });
  // A whole alternative in one row has its arrival, in a column of its own
  char arr[8], hm[6];
  if (trJsonText(trJsonGet(r, re, "arr"), re, arr, sizeof(arr)) && arr[0]) {
    trClock(arr, hm, sizeof(hm));
    snprintf(row.arr, sizeof(row.arr), "\xC2\xBB%s", hm);
  }
  trArrows(row.text);
}

// The rows of a trip, as the lock screen can show them in its width:
// - Rides and whole alternatives are not mixed. When the server sent the
//   recommended trip ride by ride and another alternative after it, the
//   alternative is left out (it would read as one more ride).
// - A ride says only where to board ("Braník»Anděl" -> "Braník"): where to
//   get off is the next row's stop, and for the last ride the header.
//   "» Anděl" (the ride you are on: where to get off) stays.
static inline void trTripRows(TrBoard& b) {
  bool rides = false, alts = false;
  for (int i = 0; i < b.nRows; i++) (b.rows[i].arr[0] ? alts : rides) = true;
  if (rides && alts) {
    int n = 0;
    for (int i = 0; i < b.nRows; i++)
      if (!b.rows[i].arr[0]) b.rows[n++] = b.rows[i];
    for (int i = n; i < b.nRows; i++) memset(&b.rows[i], 0, sizeof(b.rows[i]));
    b.nRows = (uint8_t)n;
  }
  for (int i = 0; i < b.nRows; i++) {
    TrRow& r = b.rows[i];
    if (r.arr[0]) continue;
    char* a = strstr(r.text, "\xC2\xBB");
    if (!a || a == r.text) continue;
    while (a > r.text && a[-1] == ' ') a--;
    *a = 0;
  }
}

// "HH:MM" local -> RTC seconds of its next occurrence from `now` (within a
// day); 0 when it is no clock time
static inline uint32_t trNextLocal(const char* hm, uint32_t now, int32_t offSec) {
  int h, m;
  if (sscanf(hm, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return 0;
  int64_t local = (int64_t)now + offSec;
  int64_t day = local - ((local % 86400) + 86400) % 86400;
  int64_t t = day + h * 3600 + m * 60;
  if (t <= local) t += 86400;
  return (uint32_t)(t - offSec);
}

// Reads the reply into b. now = RTC seconds when it came, offSec = the local
// UTC offset (for a validUntil given as "HH:MM"). Unknown fields are skipped;
// a reply with an unknown major version, or that is no object, is refused.
static inline bool trParse(const char* json, size_t len, uint32_t now, int32_t offSec, TrBoard& b) {
  static const char* const kHdr[] = { "hdr", "header", "title", "h", nullptr };
  static const char* const kHdrT[] = { "t", "title", "text", "name", nullptr };
  static const char* const kHdrR[] = { "r", "right", "then", nullptr };
  static const char* const kRows[] = { "rows", "r", nullptr };
  static const char* const kValid[] = { "validUntil", "valid", "vu", nullptr };
  static const char* const kEmpty[] = { "empty", "msg", nullptr };
  const char* end = json + len;
  const char* p = trWs(json, end);
  if (p >= end || *p != '{') return false;
  memset(&b, 0, sizeof(b));
  long v = 1;
  trJsonLong(trJsonGet(p, end, "v"), end, v);
  if (v != 1) return false;   // "v" changes only when this parser would misread it
  b.v = (uint8_t)v;
  const char* rows = trJsonGetAny(p, end, kRows);
  if (!rows || *rows != '[') return false;

  const char* h = trJsonGetAny(p, end, kHdr);
  snprintf(b.thenLabel, sizeof(b.thenLabel), "then");
  if (h && *h == '{') {
    const char* he = trJsonEnd(h, end);
    trJsonText(trJsonGetAny(h, he, kHdrT), he, b.title, sizeof(b.title));
    trJsonText(trJsonGetAny(h, he, kHdrR), he, b.thenLabel, sizeof(b.thenLabel));
  } else if (h) {
    trJsonText(h, end, b.title, sizeof(b.title));
  }
  // A trip: "Do práce" plus its departure and arrival, "DO PRÁCE 10:10»10:30"
  char dep[8], arr[8], t1[8], t2[8];
  if (trJsonText(trJsonGet(p, end, "dep"), end, dep, sizeof(dep)) && dep[0] &&
      trJsonText(trJsonGet(p, end, "arr"), end, arr, sizeof(arr)) && arr[0]) {
    trClock(dep, t1, sizeof(t1));
    trClock(arr, t2, sizeof(t2));
    size_t tl = strlen(b.title);
    snprintf(b.title + tl, sizeof(b.title) - tl, "%s%s\xC2\xBB%s", tl ? " " : "", t1, t2);
  }
  trArrows(b.title);
  trUpper(b.title);
  trJsonText(trJsonGetAny(p, end, kEmpty), end, b.empty, sizeof(b.empty));
  trJsonText(trJsonGet(p, end, "kind"), end, b.kind, sizeof(b.kind));
  trJsonText(trJsonGet(p, end, "key"), end, b.key, sizeof(b.key));
  long more;
  if (trJsonLong(trJsonGet(p, end, "more"), end, more) && more > 0) b.more = (uint8_t)(more > 255 ? 255 : more);

  trJsonEach(rows, end, [&](const char* r, const char* re) {
    if (*r == '{' && b.nRows < TR_MAX_ROWS) trReadRow(r, re, b.rows[b.nRows++]);
  });

  if (b.kind[0] && strcmp(b.kind, "board") != 0) trTripRows(b);

  b.fetched = now;
  // validUntil: unix seconds (taken relative to the server's "ts", so a deck
  // clock that is a bit off still waits as long as meant), or "HH:MM"
  const char* vu = trJsonGetAny(p, end, kValid);
  long t, ts;
  char hm[8];
  if (vu && *vu == '"' && trJsonText(vu, end, hm, sizeof(hm)) && strchr(hm, ':')) {
    b.validUntil = trNextLocal(hm, now, offSec);
  } else if (trJsonLong(vu, end, t) && t > 0) {
    if (trJsonLong(trJsonGet(p, end, "ts"), end, ts) && ts > 1600000000L && t >= ts)
      b.validUntil = now + (uint32_t)(t - ts);
    else if (t > 1600000000L)
      b.validUntil = (uint32_t)t;
    else
      b.validUntil = now + (uint32_t)t;   // seconds from now
  }
  return true;
}

// --- The request -------------------------------------------------------------------

// <base>/compact/widget?rows=N[&lat=..&lon=..]; base is "https://host[:port][/path]"
// (no scheme: https). Position in 1/10000 degree, or none.
static inline int trUrl(char* out, size_t n, const char* base, int rows, bool pos, int32_t lat1e4, int32_t lon1e4) {
  const bool scheme = strstr(base, "://") != nullptr;
  size_t bl = strlen(base);
  while (bl && base[bl - 1] == '/') bl--;
  int k = snprintf(out, n, "%s%.*s/compact/widget?rows=%d", scheme ? "" : "https://", (int)bl, base, rows);
  if (pos && k > 0 && (size_t)k < n) {
    // Rounded to 4 decimals; written by hand so -0.5 stays "-0.5000"
    auto deg = [](char* o, size_t on, int32_t v) {
      unsigned long a = (unsigned long)(v < 0 ? -(long)v : v);
      return snprintf(o, on, "%s%lu.%04lu", v < 0 ? "-" : "", a / 10000, a % 10000);
    };
    k += snprintf(out + k, n - k, "&lat=");
    if ((size_t)k < n) k += deg(out + k, n - k, lat1e4);
    if ((size_t)k < n) k += snprintf(out + k, n - k, "&lon=");
    if ((size_t)k < n) k += deg(out + k, n - k, lon1e4);
  }
  return k;
}

static inline int32_t trDeg1e4(double d) { return (int32_t)(d >= 0 ? d * 10000 + 0.5 : d * 10000 - 0.5); }

// --- When to ask --------------------------------------------------------------------

// The next request after one that worked: at validUntil, but not sooner
// than the minimum interval, nor later than TR_MAX_VALID_S
static inline uint32_t trNextAfterOk(uint32_t now, uint32_t validUntil, bool cellular) {
  const uint32_t lo = now + (cellular ? TR_MIN_INTERVAL_4G_S : TR_MIN_INTERVAL_S);
  uint32_t t = validUntil ? validUntil : now + TR_DEFAULT_VALID_S;
  if (t < lo) t = lo;
  if (t > now + TR_MAX_VALID_S) t = now + TR_MAX_VALID_S;
  return t;
}

// The wait after the n-th failed request in a row (n >= 1): 2, 4, 8, 15 min
static inline uint32_t trBackoff(uint8_t fails) {
  uint32_t s = TR_MIN_INTERVAL_S;
  for (uint8_t i = 1; i < fails && s < TR_BACKOFF_MAX_S; i++) s *= 2;
  return s > TR_BACKOFF_MAX_S ? TR_BACKOFF_MAX_S : s;
}

// What the widget shows of a board at `now`
enum TrState : uint8_t {
  TR_HIDDEN,   // not configured, switched off: no block
  TR_ROWS,     // rows
  TR_EMPTY,    // "Nic k zobrazení"
};

struct TrView {
  TrState state;
  bool stale;        // the last request failed, or the rows are past validUntil
  char staleAt[6];   // local "7:24" of the last good reply, "" = never
};

// lastOk = the last request worked (or none has been made yet since the
// board came); have = there is a board at all
static inline TrView trViewAt(const TrBoard& b, bool have, bool lastOk, uint32_t now, int32_t offSec) {
  TrView v = {};
  v.state = TR_EMPTY;
  const bool fresh = have && b.fetched && now >= b.fetched && now - b.fetched < TR_MAX_AGE_S;
  v.stale = !lastOk || !have || (b.validUntil && now > b.validUntil + TR_MIN_INTERVAL_S);
  if (have && b.fetched) {
    const unsigned l = (unsigned)((((int64_t)b.fetched + offSec) % 86400 + 86400) % 86400);
    snprintf(v.staleAt, sizeof(v.staleAt), "%u:%02u", (l / 3600) % 24, (l / 60) % 60);
  }
  if (fresh && b.nRows) v.state = TR_ROWS;
  return v;
}

// --- The time column ----------------------------------------------------------------

// Minutes from `now` until the row really leaves (timetable time plus the
// delay); -1 when it has left, or the time is no clock time
static inline int trMinutesTo(const TrRow& r, uint32_t now, int32_t offSec) {
  int h, m;
  if (sscanf(r.time.hm, "%d:%d", &h, &m) != 2) return -1;
  const int nowMin = (int)(((((int64_t)now + offSec) % 86400 + 86400) % 86400) / 60);
  const int d = ((h * 60 + m + r.delay - nowMin) % 1440 + 1440) % 1440;
  return d < 720 ? d : -1;   // the other half of the day: it is behind us
}

// What the bold time column of row i shows. countdown = minutes to go
// ("6 m", "teď") instead of the clock time, for runs within the hour: every
// row of a stop board and every alternative of a trip (each is a way to
// leave now), but of a trip's rides only the first one (when to leave, or
// while riding when to get off); the later rides keep their clock times.
static inline void trRowTime(const TrBoard& b, int i, bool countdown, uint32_t now, int32_t offSec, char* out, size_t n) {
  const TrRow& r = b.rows[i];
  const bool board = !b.kind[0] || strcmp(b.kind, "board") == 0;
  int min = countdown && (board || i == 0 || r.arr[0]) ? trMinutesTo(r, now, offSec) : -1;
  if (min == 0) snprintf(out, n, "te\xC4\x8F");
  else if (min > 0 && min < 60) snprintf(out, n, "%d m", min);
  else snprintf(out, n, "%s", r.time.hm);
}

static inline bool trViewEqual(const TrView& a, const TrView& b) {
  return a.state == b.state && a.stale == b.stale && strcmp(a.staleAt, b.staleAt) == 0;
}
