#pragma once
// =============================================================================
// Weather for the lock screen (docs/weather.md): the Open-Meteo forecast and
// what the lock screen shows from it at a given time. Plain C++ (no Arduino),
// so test/weather runs it on a PC.
//
// One request brings the next 24 hours by the hour (temperature, condition)
// and the next 12 hours by the quarter hour (precipitation). The lock screen
// state is worked out from that again every minute, so the rain line and the
// temperature follow the clock between requests and without a network.
// =============================================================================

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int WX_HOURS = 24;
static const int WX_QUARTERS = 48;
static const int16_t WX_NO_TEMP = INT16_MIN;
static const uint8_t WX_NO_CODE = 0xFF;

// Data older than this is not shown: the clock alone is better than a
// forecast nobody updated since the morning
static const uint32_t WX_MAX_AGE = 6UL * 3600;
// The "current" block of a reply describes the quarter hour it came in;
// after that the hourly values take over
static const uint32_t WX_CURRENT_FOR = 30UL * 60;
// A quarter hour with at least 0.1 mm is wet (the API's resolution)
static const uint8_t WX_WET_TENTHS = 1;

enum WxIcon : uint8_t { WX_SUN, WX_MOON, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_HEAVY, WX_SNOW, WX_STORM };
enum WxRain : uint8_t { WX_RAIN_NONE, WX_RAIN_STARTS, WX_RAIN_ENDS };

// What one request brought; also the SD copy, so keep it plain data
struct WxForecast {
  uint32_t fetched;            // RTC seconds of the request; 0 = nothing
  int32_t lat100, lon100;      // where it is for, in 1/100 degree

  uint32_t curTime;            // "current" block; 0 = none
  int16_t curTemp10;           // 0.1 degC
  uint8_t curCode, curDay;

  uint32_t hourStart;          // time of hourly[0]
  uint8_t nHours;
  int16_t temp10[WX_HOURS];    // 0.1 degC, WX_NO_TEMP = null
  uint8_t code[WX_HOURS];      // WMO weather code, WX_NO_CODE = null
  uint8_t isDay[WX_HOURS];

  uint32_t quarterStart;       // time of minutely_15[0]: the end of its quarter
  uint8_t nQuarters;
  uint8_t rain10[WX_QUARTERS]; // 0.1 mm in the quarter hour before the time
};

// What the lock screen shows now
struct WxNow {
  bool valid;
  WxIcon icon;
  int tempC;
  WxRain rain;
  uint16_t rainMin;            // local minute of the day rain starts / ends
};

// --- JSON: just enough for this one reply ------------------------------------

// The value of "key" in the object [p, end), skipping nested objects and
// arrays and strings, so a key of an inner object is not taken
static inline const char* wxJsonValue(const char* p, const char* end, const char* key) {
  size_t kl = strlen(key);
  int depth = 0;
  if (p < end && *p == '{') p++;
  while (p < end) {
    char c = *p;
    if (c == '"') {
      const char* s = ++p;
      while (p < end && *p != '"') p += (*p == '\\') ? 2 : 1;
      if (p >= end) return nullptr;
      const char* e = p++;
      if (depth == 0) {
        const char* q = p;
        while (q < end && (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t')) q++;
        if (q < end && *q == ':') {
          if ((size_t)(e - s) == kl && memcmp(s, key, kl) == 0) {
            q++;
            while (q < end && (*q == ' ' || *q == '\n' || *q == '\r' || *q == '\t')) q++;
            return q < end ? q : nullptr;
          }
          p = q + 1;
        }
      }
      continue;
    }
    if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') { if (depth == 0) return nullptr; depth--; }
    p++;
  }
  return nullptr;
}

// The end (one past the closing bracket) of the object or array at p
static inline const char* wxJsonSkip(const char* p, const char* end) {
  if (p >= end || (*p != '{' && *p != '[')) return nullptr;
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

// A number at p (null and anything else: NAN)
static inline double wxJsonNumber(const char* p, const char* end) {
  if (!p || p >= end) return NAN;
  char buf[32];
  size_t n = 0;
  while (p + n < end && n < sizeof(buf) - 1 && p[n] && strchr("+-.0123456789eE", p[n])) n++;
  if (n == 0) return NAN;
  memcpy(buf, p, n);
  buf[n] = 0;
  char* e;
  double v = strtod(buf, &e);
  return e == buf ? NAN : v;
}

// The numbers of the array at p, up to max; returns how many there were in
// the array (more than max: the rest is not stored), -1 when it is no array
static inline int wxJsonNumbers(const char* p, const char* end, double* out, int max) {
  if (!p || p >= end || *p != '[') return -1;
  const char* e = wxJsonSkip(p, end);
  if (!e) return -1;
  int n = 0;
  p++;
  while (p < e - 1) {
    while (p < e - 1 && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    if (p >= e - 1) break;
    if (n < max) out[n] = wxJsonNumber(p, e - 1);
    n++;
    while (p < e - 1 && *p != ',') p++;
  }
  return n;
}

// The object "key" of [p, end) as [*os, *oe)
static inline bool wxJsonObject(const char* p, const char* end, const char* key, const char** os, const char** oe) {
  const char* v = wxJsonValue(p, end, key);
  if (!v || *v != '{') return false;
  const char* e = wxJsonSkip(v, end);
  if (!e) return false;
  *os = v;
  *oe = e;
  return true;
}

static inline int16_t wxTemp10(double v) {
  if (isnan(v) || v < -100 || v > 100) return WX_NO_TEMP;
  return (int16_t)lround(v * 10);
}

static inline uint8_t wxByte(double v, uint8_t none) {
  if (isnan(v) || v < 0 || v > 254) return none;
  return (uint8_t)lround(v);
}

// A time series "time" of the object [s, e): evenly spaced by step, at most
// max entries. Returns the count and the first time, 0 when it is unusable.
static inline int wxSeries(const char* s, const char* e, uint32_t step, int max, uint32_t* start, double* tmp) {
  int n = wxJsonNumbers(wxJsonValue(s, e, "time"), e, tmp, max);
  if (n <= 0) return 0;
  if (n > max) n = max;
  for (int i = 0; i < n; i++) {
    if (isnan(tmp[i]) || tmp[i] < 1e9) return 0;
    if (i && tmp[i] - tmp[i - 1] != (double)step) return 0;
  }
  *start = (uint32_t)tmp[0];
  return n;
}

// Reads a reply of the request wxUrl() makes. Needs the hourly block; the
// current and the quarter hour blocks are used when they are there.
static inline bool wxParse(const char* json, size_t len, uint32_t fetched, WxForecast& f) {
  memset(&f, 0, sizeof(f));
  const char* end = json + len;
  const char *s, *e;
  double v[WX_QUARTERS];

  if (!wxJsonObject(json, end, "hourly", &s, &e)) return false;
  int n = wxSeries(s, e, 3600, WX_HOURS, &f.hourStart, v);
  if (n == 0) return false;
  f.nHours = (uint8_t)n;
  for (int i = 0; i < WX_HOURS; i++) { f.temp10[i] = WX_NO_TEMP; f.code[i] = WX_NO_CODE; }
  int k = wxJsonNumbers(wxJsonValue(s, e, "temperature_2m"), e, v, WX_HOURS);
  for (int i = 0; i < k && i < n; i++) f.temp10[i] = wxTemp10(v[i]);
  k = wxJsonNumbers(wxJsonValue(s, e, "weather_code"), e, v, WX_HOURS);
  for (int i = 0; i < k && i < n; i++) f.code[i] = wxByte(v[i], WX_NO_CODE);
  k = wxJsonNumbers(wxJsonValue(s, e, "is_day"), e, v, WX_HOURS);
  for (int i = 0; i < n; i++) f.isDay[i] = (i < k && v[i] == 0) ? 0 : 1;

  if (wxJsonObject(json, end, "current", &s, &e)) {
    double t = wxJsonNumber(wxJsonValue(s, e, "time"), e);
    f.curTemp10 = wxTemp10(wxJsonNumber(wxJsonValue(s, e, "temperature_2m"), e));
    f.curCode = wxByte(wxJsonNumber(wxJsonValue(s, e, "weather_code"), e), WX_NO_CODE);
    f.curDay = wxJsonNumber(wxJsonValue(s, e, "is_day"), e) == 0 ? 0 : 1;
    if (!isnan(t) && t > 1e9 && f.curTemp10 != WX_NO_TEMP && f.curCode != WX_NO_CODE) f.curTime = (uint32_t)t;
  }

  if (wxJsonObject(json, end, "minutely_15", &s, &e)) {
    n = wxSeries(s, e, 900, WX_QUARTERS, &f.quarterStart, v);
    k = n ? wxJsonNumbers(wxJsonValue(s, e, "precipitation"), e, v, WX_QUARTERS) : -1;
    if (k >= n && n > 0) {
      f.nQuarters = (uint8_t)n;
      for (int i = 0; i < n; i++) {
        double r = isnan(v[i]) || v[i] < 0 ? 0 : v[i] * 10 + 0.5;
        f.rain10[i] = r > 255 ? 255 : (uint8_t)r;
      }
    } else {
      f.quarterStart = 0;
    }
  }
  f.fetched = fetched;
  return true;
}

// The request for a place; 1/100 degree is about a kilometre, which is all
// a forecast needs, and the API gets no more of the deck's position than that
static inline int wxUrl(char* out, size_t n, int32_t lat100, int32_t lon100) {
  return snprintf(out, n,
                  "https://api.open-meteo.com/v1/forecast?latitude=%s%ld.%02ld&longitude=%s%ld.%02ld"
                  "&current=temperature_2m,weather_code,is_day"
                  "&hourly=temperature_2m,weather_code,is_day&forecast_hours=%d"
                  "&minutely_15=precipitation&forecast_minutely_15=%d"
                  "&timeformat=unixtime&timezone=GMT",
                  lat100 < 0 ? "-" : "", labs(lat100) / 100, labs(lat100) % 100,
                  lon100 < 0 ? "-" : "", labs(lon100) / 100, labs(lon100) % 100, WX_HOURS, WX_QUARTERS);
}

// WMO weather code to the icon the lock screen draws
static inline WxIcon wxIconFor(uint8_t code, bool day) {
  if (code <= 1) return day ? WX_SUN : WX_MOON;
  if (code == 2) return day ? WX_PARTLY : WX_CLOUD;   // no partly cloudy night icon
  if (code == 3) return WX_CLOUD;
  if (code == 45 || code == 48) return WX_FOG;
  if (code >= 51 && code <= 57) return WX_RAIN;        // drizzle
  if (code == 61 || code == 63 || code == 66 || code == 80 || code == 81) return WX_RAIN;
  if (code == 65 || code == 67 || code == 82) return WX_HEAVY;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
  if (code >= 95 && code <= 99) return WX_STORM;
  return WX_CLOUD;
}

static inline int wxRoundTemp(int16_t t10) {
  return t10 >= 0 ? (t10 + 5) / 10 : -((-t10 + 5) / 10);
}

static inline uint16_t wxLocalMinute(uint32_t t, int32_t offSec) {
  int64_t l = (int64_t)t + offSec;
  l %= 86400;
  if (l < 0) l += 86400;
  return (uint16_t)(l / 60);
}

// What the lock screen shows at `now` (RTC seconds, UTC); offSec is the
// deck's offset to local time
static inline WxNow wxNowAt(const WxForecast& f, uint32_t now, int32_t offSec) {
  WxNow w;
  memset(&w, 0, sizeof(w));
  if (!f.fetched || !f.nHours) return w;
  // A clock that jumped back before the request, or data too old
  if (now + 3600 < f.fetched || now > f.fetched + WX_MAX_AGE) return w;

  int16_t t10 = WX_NO_TEMP;
  uint8_t code = WX_NO_CODE;
  bool day = true;
  if (f.curTime && now >= f.fetched && now < f.fetched + WX_CURRENT_FOR) {
    t10 = f.curTemp10;
    code = f.curCode;
    day = f.curDay != 0;
  } else {
    if (now < f.hourStart) return w;
    uint32_t i = (now - f.hourStart) / 3600;
    if (i >= f.nHours) return w;
    // Temperature between the two hours, the rest from the nearer one
    uint32_t r = (now - f.hourStart) % 3600;
    t10 = f.temp10[i];
    if (i + 1 < f.nHours && t10 != WX_NO_TEMP && f.temp10[i + 1] != WX_NO_TEMP)
      t10 = (int16_t)(t10 + ((int32_t)(f.temp10[i + 1] - t10) * (int32_t)r) / 3600);
    uint32_t j = (r >= 1800 && i + 1 < f.nHours) ? i + 1 : i;
    code = f.code[j];
    day = f.isDay[j] != 0;
  }
  if (t10 == WX_NO_TEMP || code == WX_NO_CODE) return w;
  w.valid = true;
  w.tempC = wxRoundTemp(t10);
  w.icon = wxIconFor(code, day);

  // Rain: the quarter hour now is in ends at quarterStart + q * 900
  if (f.nQuarters && f.quarterStart && now + 900 > f.quarterStart) {
    uint32_t q = now <= f.quarterStart ? 0 : (now - f.quarterStart + 899) / 900;
    if (q < f.nQuarters) {
      if (f.rain10[q] >= WX_WET_TENTHS) {
        uint32_t k = q;
        while (k + 1 < f.nQuarters && f.rain10[k + 1] >= WX_WET_TENTHS) k++;
        if (k + 1 < f.nQuarters) {   // it stops within what the forecast covers
          w.rain = WX_RAIN_ENDS;
          w.rainMin = wxLocalMinute(f.quarterStart + k * 900, offSec);
        }
      } else {
        for (uint32_t k = q + 1; k < f.nQuarters; k++) {
          if (f.rain10[k] < WX_WET_TENTHS) continue;
          w.rain = WX_RAIN_STARTS;
          w.rainMin = wxLocalMinute(f.quarterStart + (k - 1) * 900, offSec);
          break;
        }
      }
    }
  }
  return w;
}

static inline bool wxNowEqual(const WxNow& a, const WxNow& b) {
  if (a.valid != b.valid) return false;
  if (!a.valid) return true;
  return a.icon == b.icon && a.tempC == b.tempC && a.rain == b.rain && (a.rain == WX_RAIN_NONE || a.rainMin == b.rainMin);
}
