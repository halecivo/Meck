// Unit tests for the lock screen weather (examples/companion_radio/ui-new/MeckWeatherUtil.h).
// Plain C++, no Arduino or PlatformIO needed. On Windows through WSL, from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/weather/test_weather.cpp -o /tmp/test_weather && /tmp/test_weather"

#include "MeckWeatherUtil.h"
#include <stdio.h>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %ld, want %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

static const uint32_t T0 = 1794603600;   // a whole hour (UTC)

// A reply shaped like Open-Meteo's: hourly from T0, quarters from T0 + q0
static std::string reply(const double* rain, int nq, uint32_t q0, double curTemp = 3.2, int curCode = 2,
                         bool withCurrent = true) {
  char b[64];
  std::string s = "{\"latitude\":50.07752,\"longitude\":14.427236,\"utc_offset_seconds\":0,"
                  "\"current_units\":{\"time\":\"unixtime\",\"interval\":\"seconds\",\"temperature_2m\":\"\xC2\xB0" "C\"},";
  if (withCurrent) {
    snprintf(b, sizeof(b), "\"current\":{\"time\":%u,\"interval\":900,", T0 + 900);
    s += b;
    snprintf(b, sizeof(b), "\"temperature_2m\":%.1f,\"weather_code\":%d,\"is_day\":0},", curTemp, curCode);
    s += b;
  }
  s += "\"minutely_15_units\":{\"time\":\"unixtime\",\"precipitation\":\"mm\"},\"minutely_15\":{\"time\":[";
  for (int i = 0; i < nq; i++) { snprintf(b, sizeof(b), "%s%u", i ? "," : "", T0 + q0 + i * 900); s += b; }
  s += "],\"precipitation\":[";
  for (int i = 0; i < nq; i++) { snprintf(b, sizeof(b), "%s%.2f", i ? "," : "", rain[i]); s += b; }
  s += "]},\"hourly_units\":{\"time\":\"unixtime\"},\"hourly\":{\"time\":[";
  for (int i = 0; i < 24; i++) { snprintf(b, sizeof(b), "%s%u", i ? "," : "", T0 + i * 3600); s += b; }
  // 3.2 at T0, then one degree more each hour; codes 0 at night, 61 from hour 5
  s += "],\"temperature_2m\":[";
  for (int i = 0; i < 24; i++) { snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", 3.2 + i); s += b; }
  s += "],\"weather_code\":[";
  for (int i = 0; i < 24; i++) { snprintf(b, sizeof(b), "%s%d", i ? "," : "", i < 5 ? 0 : 61); s += b; }
  s += "],\"is_day\":[";
  for (int i = 0; i < 24; i++) { snprintf(b, sizeof(b), "%s%d", i ? "," : "", i >= 8 && i < 18); s += b; }
  s += "]}}";
  return s;
}

static void testJson() {
  const char* j = "{\"a_units\":{\"time\":\"x\"},\"a\":{\"time\":[1, 2,null ,4],\"v\":-2.5,\"s\":\"q\\\"]{\"},\"b\":7}";
  const char* end = j + strlen(j);
  const char *s, *e;
  CHECK(wxJsonObject(j, end, "a", &s, &e));
  CHECK(*s == '{' && e[-1] == '}');
  CHECK(wxJsonNumber(wxJsonValue(s, e, "v"), e) == -2.5);
  CHECK(isnan(wxJsonNumber(wxJsonValue(s, e, "s"), e)));
  CHECK(wxJsonValue(j, end, "time") == nullptr);   // only inside the inner objects
  CHECK(wxJsonNumber(wxJsonValue(j, end, "b"), end) == 7);
  double v[8];
  CHECK_EQ(wxJsonNumbers(wxJsonValue(s, e, "time"), e, v, 8), 4);
  CHECK(v[0] == 1 && v[1] == 2 && isnan(v[2]) && v[3] == 4);
  CHECK_EQ(wxJsonNumbers(wxJsonValue(s, e, "time"), e, v, 2), 4);   // counts all, stores two
  const char* empty = "[]";
  CHECK_EQ(wxJsonNumbers(empty, empty + 2, v, 8), 0);
  CHECK(!wxJsonObject(j, end, "b", &s, &e));   // a number, not an object
}

static void testParse() {
  double rain[WX_QUARTERS] = {0};
  rain[3] = 0.3;
  std::string r = reply(rain, WX_QUARTERS, 900);
  WxForecast f;
  CHECK(wxParse(r.c_str(), r.size(), T0 + 1000, f));
  CHECK_EQ(f.fetched, T0 + 1000);
  CHECK_EQ(f.nHours, 24);
  CHECK_EQ(f.hourStart, T0);
  CHECK_EQ(f.temp10[0], 32);
  CHECK_EQ(f.temp10[23], 262);
  CHECK_EQ(f.code[5], 61);
  CHECK_EQ(f.isDay[8], 1);
  CHECK_EQ(f.curTime, T0 + 900);
  CHECK_EQ(f.curTemp10, 32);
  CHECK_EQ(f.curCode, 2);
  CHECK_EQ(f.nQuarters, WX_QUARTERS);
  CHECK_EQ(f.quarterStart, T0 + 900);
  CHECK_EQ(f.rain10[3], 3);

  // Truncated reply: no hourly block, nothing
  CHECK(!wxParse(r.c_str(), r.size() / 2, T0, f));
  CHECK(!wxParse("{\"error\":true,\"reason\":\"x\"}", 27, T0, f));
  // Missing quarters still give temperature and icon
  std::string r2 = reply(rain, 0, 900);
  CHECK(wxParse(r2.c_str(), r2.size(), T0, f));
  CHECK_EQ(f.nQuarters, 0);
}

static void testUrl() {
  char u[400];
  wxUrl(u, sizeof(u), 5008, 1442);
  CHECK(strstr(u, "latitude=50.08&longitude=14.42&") != nullptr);
  wxUrl(u, sizeof(u), -305, -7);
  CHECK(strstr(u, "latitude=-3.05&longitude=-0.07&") != nullptr);
  CHECK(strstr(u, "timeformat=unixtime") != nullptr);
}

static void testIcons() {
  CHECK_EQ(wxIconFor(0, true), WX_SUN);
  CHECK_EQ(wxIconFor(1, false), WX_MOON);
  CHECK_EQ(wxIconFor(2, true), WX_PARTLY);
  CHECK_EQ(wxIconFor(2, false), WX_CLOUD);
  CHECK_EQ(wxIconFor(45, true), WX_FOG);
  CHECK_EQ(wxIconFor(53, true), WX_RAIN);
  CHECK_EQ(wxIconFor(82, true), WX_HEAVY);
  CHECK_EQ(wxIconFor(75, true), WX_SNOW);
  CHECK_EQ(wxIconFor(95, true), WX_STORM);
  CHECK_EQ(wxRoundTemp(-5), -1);   // -0.5 rounds away from zero
  CHECK_EQ(wxRoundTemp(-4), 0);
  CHECK_EQ(wxRoundTemp(45), 5);
  CHECK_EQ(wxLocalMinute(T0 + 15 * 3600 + 20 * 60, 3600), (16 * 60 + 20 + 21 * 60) % 1440);
}

static void testNow() {
  double rain[WX_QUARTERS] = {0};
  // Quarters end at T0+900, T0+1800, ...: rain in quarters 4..6, i.e.
  // T0+1:00 .. T0+1:45
  rain[4] = 0.2; rain[5] = 1.1; rain[6] = 0.1;
  std::string r = reply(rain, WX_QUARTERS, 900);
  WxForecast f;
  CHECK(wxParse(r.c_str(), r.size(), T0 + 1000, f));

  // Right after the request: the current block
  WxNow w = wxNowAt(f, T0 + 1100, 0);
  CHECK(w.valid);
  CHECK_EQ(w.tempC, 3);
  CHECK_EQ(w.icon, WX_CLOUD);    // code 2 at night
  CHECK_EQ(w.rain, WX_RAIN_STARTS);
  CHECK_EQ(w.rainMin, (T0 % 86400) / 60 + 60);   // start of quarter 4 = T0 + 1:00

  // While it rains: the end, local time
  w = wxNowAt(f, T0 + 3600 + 600, 3600);
  CHECK_EQ(w.rain, WX_RAIN_ENDS);
  CHECK_EQ(w.rainMin, ((T0 + 3600) % 86400) / 60 + 105);
  // Exactly at the end of a wet quarter it still rains
  w = wxNowAt(f, T0 + 3600 + 900, 0);
  CHECK_EQ(w.rain, WX_RAIN_ENDS);
  // After it: dry for the rest
  w = wxNowAt(f, T0 + 3600 + 2700 + 1, 0);
  CHECK_EQ(w.rain, WX_RAIN_NONE);

  // Later: hourly values, temperature between the hours
  w = wxNowAt(f, T0 + 2 * 3600 + 1800, 0);   // 5.2 .. 6.2 -> 5.7
  CHECK(w.valid);
  CHECK_EQ(w.tempC, 6);
  CHECK_EQ(w.icon, WX_MOON);
  w = wxNowAt(f, T0 + 4 * 3600 + 1800, 0);   // nearer hour 5: rain
  CHECK_EQ(w.icon, WX_RAIN);

  // Too old, or the clock went back far
  CHECK(!wxNowAt(f, T0 + 1000 + WX_MAX_AGE + 1, 0).valid);
  CHECK(!wxNowAt(f, T0 + 1000 - 3601, 0).valid);
  CHECK(wxNowAt(f, T0 + 1000 + WX_MAX_AGE, 0).valid);
}

static void testRainWithoutEnd() {
  double rain[WX_QUARTERS];
  for (int i = 0; i < WX_QUARTERS; i++) rain[i] = 0.5;
  std::string r = reply(rain, WX_QUARTERS, 900);
  WxForecast f;
  CHECK(wxParse(r.c_str(), r.size(), T0 + 1000, f));
  WxNow w = wxNowAt(f, T0 + 1100, 0);
  CHECK(w.valid);
  CHECK_EQ(w.rain, WX_RAIN_NONE);   // no end in sight: the icon says it all

  // Dry quarter now, rain in the very next one
  rain[0] = rain[1] = 0;
  r = reply(rain, WX_QUARTERS, 900);
  CHECK(wxParse(r.c_str(), r.size(), T0 + 1000, f));
  w = wxNowAt(f, T0 + 1000, 0);     // quarter 1 (ends T0+1800) is dry
  CHECK_EQ(w.rain, WX_RAIN_STARTS);
  CHECK_EQ(w.rainMin, (T0 % 86400) / 60 + 30);
}

static void testEqual() {
  WxNow a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  CHECK(wxNowEqual(a, b));
  a.valid = b.valid = true;
  a.rainMin = 5;   // ignored without rain
  CHECK(wxNowEqual(a, b));
  a.rain = WX_RAIN_STARTS;
  CHECK(!wxNowEqual(a, b));
}

int main() {
  testJson();
  testParse();
  testUrl();
  testIcons();
  testNow();
  testRainWithoutEnd();
  testEqual();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
