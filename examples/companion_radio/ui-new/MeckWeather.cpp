// Weather for the lock screen -- see MeckWeather.h and docs/weather.md
#if defined(MECK_WEATHER) && MECK_WEATHER

#include "MeckWeather.h"
#include "MeckNet.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#ifdef CONFIG_PM_ENABLE
  #include <esp_pm.h>
#endif

MeckWeather meckWeather;

#define WX_DIR    "/weather"
#define WX_FILE   "/weather/forecast.bin"
#define WX_TMP    "/weather/forecast.tmp"
#define WX_MAGIC  0x31585757u   // "WWX1"
#define WX_RESP_LEN 8192        // a reply is about 2 kB

// mbedTLS in PSRAM: internal RAM has no block large enough for a handshake
// next to WiFi and Bluetooth (as in MeckHa and the Web Reader)
static void* wxPsramCalloc(size_t num, size_t size) {
  void* p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(num, size);
}
static void wxTlsInPsram() {
  static bool done = false;
  if (!done) { mbedtls_platform_set_calloc_free(wxPsramCalloc, free); done = true; }
}

// Collects the reply into a fixed buffer; the rest is read and dropped, so
// HTTPClient finishes the response either way. full() says it did not fit.
class WxBufStream : public Stream {
public:
  WxBufStream(char* buf, size_t cap) : _buf(buf), _cap(cap) { buf[0] = 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    size_t k = _len + 1 < _cap ? min(n, _cap - 1 - _len) : 0;
    memcpy(_buf + _len, b, k);
    _len += k;
    _buf[_len] = 0;
    if (k < n) _over = true;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t length() const { return _len; }
  bool full() const { return _over; }
private:
  char* _buf;
  size_t _cap, _len = 0;
  bool _over = false;
};

static int32_t wxDeg100(double d) { return (int32_t)lround(d * 100); }

void MeckWeather::begin() {
  load();
}

void MeckWeather::fetchSoon() {
  _next = millis();
}

void MeckWeather::loop(bool enabled, bool pos, double lat, double lon, uint32_t now) {
  // Nothing to ask for: no weather wanted, no place, or no clock to read the
  // forecast by
  if (!enabled || !pos || now < 1700000000u) {
    if (_netWait) {
      meckNetRelease(DATA_APP_WEATHER);
      _netWait = false;
    }
    return;
  }
  const int32_t la = wxDeg100(lat), lo = wxDeg100(lon);
  // Somewhere else than the forecast is for: the next request goes now
  if (_fc.fetched && !_netWait &&
      (labs(la - _fc.lat100) > MOVED_100 || labs(lo - _fc.lon100) > MOVED_100) &&
      (int32_t)(millis() - _next) < 0 && (!_lastTry || now - _lastTry >= RETRY_MS / 1000)) {
    Serial.println("Weather: moved, asking for the new place");
    fetchSoon();
  }
  if (_netWait || (int32_t)(millis() - _next) >= 0) due(la, lo, now);
}

// A request is due: on WiFi now; without it over mobile data when weather
// may use it. The link is asked for without blocking, the request goes once
// it is up, and the link is let go right after.
void MeckWeather::due(int32_t lat100, int32_t lon100, uint32_t now) {
  if (meckNetUp(DATA_APP_WEATHER)) {
    const bool wifi = meckNetWifi();
    if (!wifi) meckNetRequest(DATA_APP_WEATHER);   // hold it through the request
    bool ok = fetch(lat100, lon100, now);
    if (_netWait || !wifi) meckNetRelease(DATA_APP_WEATHER);
    _netWait = false;
    _next = millis() + (ok ? (wifi ? WIFI_INTERVAL_MS : DATA_INTERVAL_MS) : RETRY_MS);
    return;
  }
  if (!_netWait) {
    // No WiFi and no mobile data for weather: waits for WiFi to come
    if (!meckNet4gAllowed(DATA_APP_WEATHER)) return;
    Serial.println("Weather: request due, bringing up mobile data");
    meckNetRequest(DATA_APP_WEATHER);
    _netWait = true;
    _netWaitSince = millis();
    return;
  }
#ifdef MODEM_DATA
  bool failed = modemManager.dataFailed() && millis() - _netWaitSince > 1000;
#else
  bool failed = true;
#endif
  if (failed || millis() - _netWaitSince > NET_WAIT_MS) {
    Serial.println("Weather: no mobile data, next try later");
    meckNetRelease(DATA_APP_WEATHER);
    _netWait = false;
    _lastResult = -1;
    _next = millis() + RETRY_MS;
  }
}

bool MeckWeather::fetch(int32_t lat100, int32_t lon100, uint32_t now) {
  _lastTry = now;
  char url[320];
  wxUrl(url, sizeof(url), lat100, lon100);
  char* resp = (char*)heap_caps_malloc(WX_RESP_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!resp) { _lastResult = -1; return false; }

  pmHold(true);   // no light sleep in the middle of a TLS session
  const bool wifi = meckNetWifi();
  int code = -1;
  size_t len = 0;
  bool full = false;
  {
    MeckNetUse use(DATA_APP_WEATHER);
    wxTlsInPsram();
    WiFiClientSecure tls;
    tls.useBuiltinCACertBundle();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setReuse(false);
    if (http.begin(tls, url)) {
      code = http.GET();
      if (code == 200) {
        WxBufStream s(resp, WX_RESP_LEN);
        http.writeToStream(&s);
        len = s.length();
        full = s.full();
      }
      http.end();
    }
  }
  pmHold(false);

  bool ok = false;
  if (code == 200 && !full) {
    WxForecast f;
    if (wxParse(resp, len, now, f)) {
      f.lat100 = lat100;
      f.lon100 = lon100;
      _fc = f;
      _version++;
      save();
      ok = true;
    }
  }
  heap_caps_free(resp);
  _lastResult = code;
  Serial.printf("Weather: HTTP %d, %u bytes%s%s\n", code, (unsigned)len, wifi ? "" : " (4G)",
                ok ? "" : (code == 200 ? ", could not read it" : ""));
  return ok;
}

void MeckWeather::save() const {
  if (!SD.exists(WX_DIR)) SD.mkdir(WX_DIR);
  File f = SD.open(WX_TMP, FILE_WRITE);
  if (!f) { Serial.println("Weather: cannot write the SD copy"); return; }
  uint32_t head[2] = { WX_MAGIC, (uint32_t)sizeof(WxForecast) };
  bool ok = f.write((const uint8_t*)head, sizeof(head)) == sizeof(head) &&
            f.write((const uint8_t*)&_fc, sizeof(_fc)) == sizeof(_fc);
  f.close();
  if (!ok) { SD.remove(WX_TMP); return; }
  SD.remove(WX_FILE);
  SD.rename(WX_TMP, WX_FILE);
}

void MeckWeather::load() {
  File f = SD.open(WX_FILE, FILE_READ);
  if (!f) return;
  uint32_t head[2] = {};
  WxForecast fc;
  bool ok = f.read((uint8_t*)head, sizeof(head)) == sizeof(head) && head[0] == WX_MAGIC &&
            head[1] == sizeof(WxForecast) && f.read((uint8_t*)&fc, sizeof(fc)) == (int)sizeof(fc);
  f.close();
  if (!ok || fc.nHours > WX_HOURS || fc.nQuarters > WX_QUARTERS) return;
  _fc = fc;
  _version++;
  Serial.println("Weather: forecast from the SD copy");
}

void MeckWeather::pmHold(bool on) {
#ifdef CONFIG_PM_ENABLE
  if (!_pmLock && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meck_wx",
                                     (esp_pm_lock_handle_t*)&_pmLock) != ESP_OK) return;
  if (on) esp_pm_lock_acquire((esp_pm_lock_handle_t)_pmLock);
  else esp_pm_lock_release((esp_pm_lock_handle_t)_pmLock);
#else
  (void)on;
#endif
}

// --- Debug CLI -------------------------------------------------------------------

void MeckWeather::cmd(const char* arg, uint32_t now, int32_t offSec) {
  static const char* const kIcon[] = { "sun", "moon", "partly", "cloud", "fog", "rain", "heavy", "snow", "storm" };
  if (strcmp(arg, "") == 0 || strcmp(arg, "status") == 0) {
    if (_fc.fetched) {
      Serial.printf("Weather: forecast from %lu min ago, %d h + %d quarters, for %.2f, %.2f\n",
                    now > _fc.fetched ? (unsigned long)((now - _fc.fetched) / 60) : 0UL, _fc.nHours,
                    _fc.nQuarters, _fc.lat100 / 100.0, _fc.lon100 / 100.0);
    } else {
      Serial.println("Weather: no forecast");
    }
    WxNow w = wxNowAt(_fc, now, offSec);
    if (w.valid) {
      Serial.printf("  now: %s %d C", kIcon[w.icon], w.tempC);
      if (w.rain != WX_RAIN_NONE)
        Serial.printf(", rain %s %u:%02u", w.rain == WX_RAIN_STARTS ? "starts" : "ends", w.rainMin / 60, w.rainMin % 60);
      Serial.println();
    } else {
      Serial.println("  now: nothing to show");
    }
    long next = (long)(_next - millis());
    Serial.printf("  last request HTTP %d, next in %ld s, WiFi %s, 4G %s for weather%s\n", _lastResult,
                  next > 0 ? next / 1000 : 0L, meckNetWifi() ? "up" : "down",
                  meckNet4gAllowed(DATA_APP_WEATHER) ? "allowed" : "not allowed", _netWait ? ", waiting for it" : "");
  } else if (strcmp(arg, "fetch") == 0) {
    fetchSoon();
    Serial.println("Weather: request on the next loop (needs the setting on and a position)");
  } else if (strcmp(arg, "rain") == 0) {
    // The quarter hours as the forecast has them, 0.1 mm each
    for (int i = 0; i < _fc.nQuarters; i++) {
      uint16_t m = wxLocalMinute(_fc.quarterStart + i * 900, offSec);
      Serial.printf("%s%u:%02u %u", i % 8 ? "  " : (i ? "\n" : ""), m / 60, m % 60, _fc.rain10[i]);
    }
    Serial.println();
  } else if (strcmp(arg, "clear") == 0) {
    memset(&_fc, 0, sizeof(_fc));
    _version++;
    SD.remove(WX_FILE);
    Serial.println("Weather: forecast cleared");
  } else {
    Serial.println("weather [status]|fetch|rain|clear");
  }
}

#endif
