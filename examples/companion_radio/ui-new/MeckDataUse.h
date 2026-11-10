#pragma once
// =============================================================================
// Mobile data per app: which apps may use 4G, and what each used over it in
// the last 24 hours. Plain C++ (no Arduino), so test/datause runs it on a PC.
//
// Only traffic over the PPP link is counted: WiFi is free.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The apps that reach the internet. OTHER is not an app: the debug CLI, and
// traffic that cannot be told apart; it has no 4G switch of its own.
enum DataApp : uint8_t {
  DATA_APP_OTHER = 0,
  DATA_APP_BROWSER,
  DATA_APP_IRC,
  DATA_APP_HA,
  DATA_APP_COUNT
};
#define DATA_APP_NONE 0xFF   // no app tagged right now

static inline uint8_t dataAppBit(uint8_t app) { return (uint8_t)(1u << app); }

// Who the traffic belongs to: the app that tagged it (it is in a request
// right now), else the only app holding the link (that includes the link
// setup it caused), else IRC, the one connection that stays open between
// calls and gets data by itself. Anything else is OTHER.
static inline uint8_t dataUseOwner(uint8_t tag, uint8_t holders) {
  if (tag < DATA_APP_COUNT) return tag;
  uint8_t apps = holders & (uint8_t)~dataAppBit(DATA_APP_OTHER);
  if (apps && !(apps & (apps - 1))) {
    uint8_t a = 0;
    while (!(apps & 1)) { apps >>= 1; a++; }
    return a;
  }
  if (apps & dataAppBit(DATA_APP_IRC)) return DATA_APP_IRC;
  return DATA_APP_OTHER;
}

// Bytes per app in one-hour buckets; "the last 24 hours" is the current
// hour so far plus the 23 before it. Hours count from boot (a 64-bit timer),
// not the wall clock, which can be wrong until something sets it.
struct DataUsage {
  static const int HOURS = 24;
  uint32_t bytes[DATA_APP_COUNT][HOURS];
  uint32_t hour;   // the hour the newest bucket belongs to

  void clear() { memset(bytes, 0, sizeof(bytes)); hour = 0; }

  // Empty the buckets of the hours that went by since the newest one
  void roll(uint32_t now) {
    if (now <= hour) return;
    uint32_t gone = now - hour;
    if (gone >= (uint32_t)HOURS) {
      memset(bytes, 0, sizeof(bytes));
    } else {
      for (uint32_t h = hour + 1; h <= now; h++)
        for (int a = 0; a < DATA_APP_COUNT; a++) bytes[a][h % HOURS] = 0;
    }
    hour = now;
  }

  void add(uint8_t app, uint32_t n, uint32_t now) {
    if (app >= DATA_APP_COUNT) app = DATA_APP_OTHER;
    if (now < hour) now = hour;   // a caller that read the clock a moment earlier
    roll(now);
    uint32_t& b = bytes[app][now % HOURS];
    b = (b > UINT32_MAX - n) ? UINT32_MAX : b + n;
  }

  uint32_t last24h(uint8_t app, uint32_t now) {
    if (app >= DATA_APP_COUNT) return 0;
    roll(now);
    uint32_t sum = 0;
    for (int h = 0; h < HOURS; h++) sum += bytes[app][h];
    return sum;
  }
};

// Setting file: the master switch, then one '0'/'1' per app in DataApp
// order from BROWSER on ("1110" = on, browser and IRC allowed, HA not).
// An app missing from an older file is allowed: the master switch is what
// keeps 4G off until the user wants it.
static inline void dataConfigParse(const char* s, bool& master, uint8_t& appMask) {
  master = s && s[0] == '1';
  appMask = 0;
  for (uint8_t a = DATA_APP_BROWSER; a < DATA_APP_COUNT; a++) {
    char c = (s && strlen(s) > a - DATA_APP_BROWSER + 1) ? s[a - DATA_APP_BROWSER + 1] : '1';
    if (c != '0') appMask |= dataAppBit(a);
  }
}

static inline void dataConfigFormat(char* out, size_t n, bool master, uint8_t appMask) {
  if (n < (size_t)DATA_APP_COUNT + 1) { if (n) out[0] = 0; return; }
  size_t k = 0;
  out[k++] = master ? '1' : '0';
  for (uint8_t a = DATA_APP_BROWSER; a < DATA_APP_COUNT; a++)
    out[k++] = (appMask & dataAppBit(a)) ? '1' : '0';
  out[k] = 0;
}

// "0 B", "820 B", "1.4 kB", "56 kB", "2.3 MB" (1 kB = 1000 B, as phones show it)
static inline void dataFormatBytes(char* out, size_t n, uint32_t b) {
  if (b < 1000) snprintf(out, n, "%u B", (unsigned)b);
  else if (b < 10000) snprintf(out, n, "%u.%u kB", (unsigned)(b / 1000), (unsigned)(b % 1000 / 100));
  else if (b < 1000000) snprintf(out, n, "%u kB", (unsigned)(b / 1000));
  else if (b < 10000000) snprintf(out, n, "%u.%u MB", (unsigned)(b / 1000000), (unsigned)(b % 1000000 / 100000));
  else snprintf(out, n, "%u MB", (unsigned)(b / 1000000));
}
