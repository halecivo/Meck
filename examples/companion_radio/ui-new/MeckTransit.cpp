// Departures on the lock screen -- see MeckTransit.h and docs/transit-widget.md
#if defined(MECK_TRANSIT) && MECK_TRANSIT

#include "MeckTransit.h"
#include "MeckNet.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#ifdef CONFIG_PM_ENABLE
  #include <esp_pm.h>
#endif

MeckTransit meckTransit;

#define TR_NVS "meck_tr"

// mbedTLS in PSRAM: internal RAM has no block large enough for a handshake
// next to WiFi and Bluetooth (as in MeckWeather and MeckHa)
static void* trPsramCalloc(size_t num, size_t size) {
  void* p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(num, size);
}

// Collects the reply into a fixed buffer; the rest is read and dropped, so
// HTTPClient finishes the response either way
class TrBufStream : public Stream {
public:
  TrBufStream(char* buf, size_t cap) : _buf(buf), _cap(cap) { buf[0] = 0; }
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

void MeckTransit::begin() {
  Preferences p;
  if (!p.begin(TR_NVS, true)) return;   // nothing stored yet
  p.getString("url", _url, sizeof(_url));
  p.getString("token", _token, sizeof(_token));
  p.end();
}

static bool trStore(const char* key, const char* val) {
  Preferences p;
  if (!p.begin(TR_NVS, false)) return false;
  bool ok = val[0] ? p.putString(key, val) > 0 : (p.remove(key), true);
  p.end();
  return ok;
}

bool MeckTransit::setUrl(const char* url) {
  while (*url == ' ') url++;
  if (strlen(url) >= sizeof(_url)) return false;
  if (url[0] && strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0 && strstr(url, "://")) return false;
  strncpy(_url, url, sizeof(_url) - 1);
  _url[sizeof(_url) - 1] = 0;
  _have = false;   // a board of another server is not this one's
  _fails = 0;
  _version++;
  fetchSoon();
  return trStore("url", _url);
}

bool MeckTransit::setToken(const char* token) {
  while (*token == ' ') token++;
  if (strlen(token) >= sizeof(_token)) return false;
  strncpy(_token, token, sizeof(_token) - 1);
  _token[sizeof(_token) - 1] = 0;
  _fails = 0;
  fetchSoon();
  return trStore("token", _token);
}

void MeckTransit::printConfig() const {
  Serial.printf("  > url %s\n", _url[0] ? _url : "(none)");
  Serial.printf("  > token %s\n", _token[0] ? "set" : "(none)");
}

void MeckTransit::fetchSoon() {
  _due = true;
}

void MeckTransit::schedule(uint32_t inS) {
  _due = false;
  _next = millis() + inS * 1000;
}

void MeckTransit::loop(bool enabled, bool locked, int rows, bool pos, double lat, double lon,
                       uint32_t now, int32_t offSec) {
  // The lock screen just came: what it shows should be current. Not more
  // often than a request may go anyway.
  if (locked && !_wasLocked && !_due) {
    uint32_t since = _tried ? (millis() - _lastTryMs) / 1000 : TR_MIN_INTERVAL_S;
    uint32_t wait = since >= TR_MIN_INTERVAL_S ? 0 : TR_MIN_INTERVAL_S - since;
    if (!_fails && (int32_t)(_next - (millis() + wait * 1000)) > 0) schedule(wait);
  }
  _wasLocked = locked;

  // Nothing to ask for: switched off, no server, not shown, or no clock to
  // read the times by. Mobile data asked for is let go.
  if (!enabled || !configured() || !locked || now < 1700000000u || _demo) {
    if (_netWait) {
      meckNetRelease(DATA_APP_TRANSIT);
      _netWait = false;
    }
    return;
  }
  if (_netWait || _due || (int32_t)(millis() - _next) >= 0) due(rows, pos, lat, lon, now, offSec);
}

// A request is due: on WiFi now; without it over mobile data when the app
// may use it. The link is asked for without blocking, the request goes once
// it is up, and the link is let go right after.
void MeckTransit::due(int rows, bool pos, double lat, double lon, uint32_t now, int32_t offSec) {
  if (meckNetUp(DATA_APP_TRANSIT)) {
    const bool wifi = meckNetWifi();
    if (!wifi) meckNetRequest(DATA_APP_TRANSIT);   // hold it through the request
    bool ok = fetch(rows, pos, lat, lon, now, offSec);
    if (_netWait || !wifi) meckNetRelease(DATA_APP_TRANSIT);
    _netWait = false;
    if (ok) {
      uint32_t at = trNextRequest(_board, _shownRows, now, offSec, !wifi);
      schedule(at - now);
    } else {
      failed();
    }
    return;
  }
  if (!_netWait) {
    // No WiFi and no mobile data for this app: waits for WiFi to come (it
    // is the user's to switch on), checking again after a back-off step
    if (!meckNet4gAllowed(DATA_APP_TRANSIT)) {
      _lastResult = -1;
      _tried = true;
      _lastTryMs = millis();
      failed();
      return;
    }
    Serial.println("Transit: request due, bringing up mobile data");
    meckNetRequest(DATA_APP_TRANSIT);
    _netWait = true;
    _netWaitSince = millis();
    return;
  }
#ifdef MODEM_DATA
  bool netFailed = modemManager.dataFailed() && millis() - _netWaitSince > 1000;
#else
  bool netFailed = true;
#endif
  if (netFailed || millis() - _netWaitSince > NET_WAIT_MS) {
    Serial.println("Transit: no mobile data, next try later");
    meckNetRelease(DATA_APP_TRANSIT);
    _netWait = false;
    _lastResult = -1;
    _tried = true;
    _lastTryMs = millis();
    failed();
  }
}

void MeckTransit::failed() {
  if (_fails < 255) _fails++;
  schedule(trBackoff(_fails));
  _version++;   // the stale mark
}

bool MeckTransit::fetch(int rows, bool pos, double lat, double lon, uint32_t now, int32_t offSec) {
  _tried = true;
  _lastTryMs = millis();
  if (rows < 1) rows = 1;
  if (rows > TR_KEEP_ROWS) rows = TR_KEEP_ROWS;
  char url[200];
  trUrl(url, sizeof(url), _url, rows, pos, trDeg1e4(lat), trDeg1e4(lon));
  char* resp = (char*)heap_caps_malloc(RESP_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!resp) { _lastResult = -1; return false; }

  pmHold(true);   // no light sleep in the middle of a request
  const uint32_t t0 = millis();
  const bool https = strncmp(url, "https://", 8) == 0;
  int code = -1;
  size_t len = 0;
  bool full = false;
  {
    MeckNetUse use(DATA_APP_TRANSIT);
    static bool tlsPsram = false;
    if (https && !tlsPsram) { mbedtls_platform_set_calloc_free(trPsramCalloc, free); tlsPsram = true; }
    WiFiClientSecure tls;
    WiFiClient plain;
    if (https) tls.useBuiltinCACertBundle();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.setReuse(false);
    bool begun = https ? http.begin(tls, url) : http.begin(plain, url);
    char auth[96];
    if (begun) {
      snprintf(auth, sizeof(auth), "Bearer %s", _token);
      http.addHeader("Authorization", auth);
      http.addHeader("Accept", "application/json");
      code = http.GET();
      if (code == 200) {
        TrBufStream s(resp, RESP_LEN);
        http.writeToStream(&s);
        len = s.length();
        full = s.full();
      }
      http.end();
    }
    memset(auth, 0, sizeof(auth));   // no copy of the token left on the stack
  }
  _lastMs = millis() - t0;   // awake for the request: what an update costs
  pmHold(false);

  bool ok = false;
  if (code == 200 && !full) {
    TrBoard b;
    if (trParse(resp, len, now, offSec, b)) {
      // The same key: the same rows, nothing to redraw (only the times of
      // the reply move on). A change from stale to fresh still redraws,
      // as the widget's view changes.
      const bool same = _have && !_fails && b.key[0] && strcmp(b.key, _board.key) == 0;
      _board = b;
      _have = true;
      _fails = 0;
      if (!same) _version++;
      ok = true;
    }
  }
  heap_caps_free(resp);
  _lastResult = code;
  _lastBytes = (uint32_t)len;
  Serial.printf("Transit: HTTP %d, %u bytes in %lu ms, %d rows asked%s%s\n", code, (unsigned)len, (unsigned long)_lastMs, rows,
                meckNetWifi() ? "" : " (4G)", ok ? "" : (code == 200 ? ", could not read it" : ""));
  return ok;
}

// Debug CLI "transit ping": where the time of a request goes. Connecting
// (TCP and TLS) and the wait for the first byte, for /health and for the
// widget itself, by hand so the two can be told apart. Nothing is printed
// of the reply; the token goes only to the server, as in a request.
void MeckTransit::ping(uint32_t now, int32_t offSec) {
  (void)now; (void)offSec;
  if (!configured() || !meckNetWifi()) { Serial.println("Transit: ping needs the url, the token and WiFi"); return; }
  const bool https = strncmp(_url, "https://", 8) == 0 || !strstr(_url, "://");
  const char* h = strstr(_url, "://");
  h = h ? h + 3 : _url;
  char host[64];
  size_t hl = strcspn(h, ":/");
  if (hl >= sizeof(host)) hl = sizeof(host) - 1;
  memcpy(host, h, hl);
  host[hl] = 0;
  uint16_t port = https ? 443 : 80;
  if (h[hl] == ':') port = (uint16_t)atoi(h + hl + 1);
  const char* prefix = strchr(h + hl, '/');
  char base[48] = "";
  if (prefix) {
    snprintf(base, sizeof(base), "%s", prefix);
    size_t bl = strlen(base);
    while (bl && base[bl - 1] == '/') base[--bl] = 0;
  }
  static bool tlsPsram = false;
  if (https && !tlsPsram) { mbedtls_platform_set_calloc_free(trPsramCalloc, free); tlsPsram = true; }
  pmHold(true);
  for (int k = 0; k < 2; k++) {
    WiFiClientSecure tls;
    WiFiClient plain;
    if (https) tls.useBuiltinCACertBundle();
    WiFiClient& c = https ? (WiFiClient&)tls : plain;
    const uint32_t t0 = millis();
    bool ok = https ? tls.connect(host, port) : plain.connect(host, port);
    const uint32_t t1 = millis();
    if (!ok) { Serial.printf("Transit: ping %s: no connection after %lu ms\n", k ? "widget" : "health", (unsigned long)(t1 - t0)); continue; }
    c.printf("GET %s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n", base, k ? "/compact/widget?rows=8" : "/health", host);
    if (k) c.printf("Authorization: Bearer %s\r\n", _token);
    c.print("\r\n");
    while (!c.available() && c.connected() && millis() - t1 < 20000) delay(5);
    const uint32_t t2 = millis();
    size_t n = 0;
    while ((c.connected() || c.available()) && millis() - t2 < 10000) {
      if (c.available()) { c.read(); n++; } else delay(2);
    }
    const uint32_t t3 = millis();
    c.stop();
    Serial.printf("Transit: ping %s: connect %lu ms, first byte %lu ms, rest %lu ms, %u bytes\n",
                  k ? "widget" : "health", (unsigned long)(t1 - t0), (unsigned long)(t2 - t1),
                  (unsigned long)(t3 - t2), (unsigned)n);
  }
  pmHold(false);
}

void MeckTransit::pmHold(bool on) {
#ifdef CONFIG_PM_ENABLE
  if (!_pmLock && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meck_tr",
                                     (esp_pm_lock_handle_t*)&_pmLock) != ESP_OK) return;
  if (on) esp_pm_lock_acquire((esp_pm_lock_handle_t)_pmLock);
  else esp_pm_lock_release((esp_pm_lock_handle_t)_pmLock);
#else
  (void)on;
#endif
}

// --- Debug CLI -------------------------------------------------------------------

// Made-up boards to check the layout on screenshots (RAM only; no requests
// while one is shown). The rows follow the design's examples.
void MeckTransit::demo(const char* what, uint32_t now, int32_t offSec) {
  // In the /compact/widget v1 format (halecivo/neujede PR #24, api/README.md)
  // Times from now on, so the minutes to go have something to count
  char tb[20][6];
  int ti = 0;
  auto hm = [&](int inMin) -> const char* {
    const unsigned l = (unsigned)((((int64_t)now + offSec) % 86400 + 86400) % 86400) / 60 + (unsigned)inMin;
    char* o = tb[ti++ % 20];
    snprintf(o, 6, "%02u:%02u", (l / 60) % 24, l % 60);
    return o;
  };
  static const char* const kBoardFmt =
    "{\"v\":1,\"kind\":\"board\",\"h\":\"\xC5\x98\xC3\xAD\xC4\x8D" "any, n\xC3\xA1" "dra\xC5\xBE\xC3\xAD\",\"rows\":["
    "{\"m\":\"R\",\"l\":\"S9\",\"t\":\"Praha hl.n.\",\"pl\":\"1\",\"at\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\",\"late\":1}]},"
    "{\"m\":\"B\",\"l\":\"381\",\"t\":\"H\xC3\xA1je\",\"d\":2,\"pl\":\"B\",\"at\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\"}]},"
    "{\"m\":\"R\",\"l\":\"S9\",\"t\":\"Bene\xC5\xA1ov u Prahy\",\"d\":5,\"pl\":\"3\",\"pc\":1,\"at\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\"}]},"
    "{\"m\":\"R\",\"l\":\"S9\",\"t\":\"Praha Masarykovo n.\",\"d\":489,\"pl\":\"2\",\"at\":\"23:12\",\"then\":[{\"t\":\"%s\"}]}"
    "],\"validUntil\":300,\"key\":\"demo1\"}";
  static const char* const kTripFmt =
    "{\"v\":1,\"kind\":\"trip\",\"h\":\"Do pr\xC3\xA1" "ce\",\"dep\":\"%s\",\"arr\":\"%s\",\"rows\":["
    "{\"m\":\"T\",\"l\":\"17\",\"t\":\"Bran\xC3\xADk\xE2\x86\x92Palack\xC3\xA9ho\",\"d\":1,\"pl\":\"A\",\"at\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\",\"late\":1}]},"
    "{\"m\":\"M\",\"l\":\"B\",\"t\":\"Karlovo n.\xE2\x86\x92" "Florenc\",\"p\":72,\"pl\":\"2\",\"at\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\"}]},"
    "{\"m\":\"R\",\"l\":\"S9\",\"t\":\"Florenc\xE2\x86\x92Vyso\xC4\x8D" "any\",\"pl\":\"4\",\"at\":\"%s\"}"
    "],\"validUntil\":300,\"key\":\"demo2\"}";
  // Alternatives, one per row: the first line in the chip, the others as text
  static const char* const kAltFmt =
    "{\"v\":1,\"kind\":\"trip\",\"h\":\"Do pr\xC3\xA1" "ce\",\"dep\":\"%s\",\"arr\":\"%s\",\"rows\":["
    "{\"m\":\"T\",\"l\":\"17\",\"t\":\"B\xE2\x86\x92S9\",\"d\":1,\"at\":\"%s\",\"arr\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\",\"late\":1}]},"
    "{\"m\":\"R\",\"l\":\"S9\",\"t\":\"8\",\"at\":\"%s\",\"arr\":\"%s\",\"then\":[{\"t\":\"%s\"}]},"
    "{\"m\":\"B\",\"l\":\"196\",\"t\":\"C\xE2\x86\x92" "8\xE2\x86\x92S9\",\"at\":\"%s\",\"arr\":\"%s\",\"then\":[{\"t\":\"%s\"},{\"t\":\"%s\"}]}"
    "],\"validUntil\":300,\"key\":\"demo3\"}";
  static char src[900];
  if (strcmp(what, "trip") == 0) snprintf(src, sizeof(src), kTripFmt, hm(7), hm(42), hm(6), hm(14), hm(22), hm(22), hm(25), hm(29), hm(31));
  else if (strcmp(what, "alt") == 0) snprintf(src, sizeof(src), kAltFmt, hm(7), hm(42), hm(6), hm(42), hm(14), hm(22), hm(12), hm(44), hm(42), hm(4), hm(47), hm(14), hm(24));
  else snprintf(src, sizeof(src), kBoardFmt, hm(3), hm(11), hm(41), hm(7), hm(7), hm(22), hm(14), hm(32), hm(62), hm(75));
  TrBoard b;
  if (!trParse(src, strlen(src), now, offSec, b)) return;
  int n = atoi(what);
  if (n >= 1 && n <= TR_MAX_ROWS) b.nRows = (uint8_t)n;
  _demo = true;
  _have = true;
  _fails = 0;
  if (strcmp(what, "empty") == 0) b.nRows = 0;
  if (strcmp(what, "stale") == 0) { b.fetched = now - 600; _fails = 3; }
  if (strcmp(what, "offline") == 0) { _have = false; _fails = 2; }
  _board = b;
  _version++;
}

void MeckTransit::cmd(const char* arg, uint32_t now, int32_t offSec) {
  if (strcmp(arg, "") == 0 || strcmp(arg, "status") == 0) {
    printConfig();
    if (_have) {
      Serial.printf("Transit: board from %lu s ago, %d rows, \"%s\", valid for %ld s%s\n",
                    now > _board.fetched ? (unsigned long)(now - _board.fetched) : 0UL, _board.nRows, _board.title,
                    _board.validUntil ? (long)_board.validUntil - (long)now : 0L, _demo ? " (demo)" : "");
      for (int i = 0; i < _board.nRows; i++) {
        const TrRow& r = _board.rows[i];
        Serial.printf("  [%s] %s [%s] [%s%s] %s%s  %s%s %s%s\n", r.line, r.text, r.badge, r.plat,
                      r.platChanged ? "!" : "", r.time.hm, r.time.late ? "*" : "", r.then[0].hm,
                      r.then[0].late ? "*" : "", r.then[1].hm, r.then[1].late ? "*" : "");
        if (r.arr[0]) Serial.printf("      arrives %s\n", r.arr);
      }
    } else {
      Serial.println("Transit: no board");
    }
    long next = _due ? 0 : (long)(_next - millis());
    Serial.printf("  last request HTTP %d (%u bytes, %lu ms), %u failed in a row, next %s%ld s, WiFi %s, 4G %s%s\n",
                  _lastResult, (unsigned)_lastBytes, (unsigned long)_lastMs, _fails, _due ? "when locked, " : "in ",
                  next > 0 ? next / 1000 : 0L, meckNetWifi() ? "up" : "down",
                  meckNet4gAllowed(DATA_APP_TRANSIT) ? "allowed" : "not allowed", _netWait ? ", waiting for it" : "");
  } else if (strcmp(arg, "ping") == 0) {
    ping(now, offSec);
  } else if (strcmp(arg, "fetch") == 0) {
    _demo = false;
    fetchSoon();
    Serial.println("Transit: request on the next loop while locked (needs the setting, url and token)");
  } else if (memcmp(arg, "demo", 4) == 0) {
    demo(arg[4] == ' ' ? arg + 5 : "", now, offSec);
    Serial.printf("Transit: demo board \"%s\" (RAM only, no requests until \"transit fetch\")\n", arg);
  } else if (strcmp(arg, "clear") == 0) {
    _demo = false;
    _have = false;
    memset(&_board, 0, sizeof(_board));
    _version++;
    fetchSoon();
    Serial.println("Transit: board cleared");
  } else {
    Serial.println("transit [status]|fetch|ping|clear|demo [1-4|trip|alt|empty|stale|offline]");
  }
}

#endif
