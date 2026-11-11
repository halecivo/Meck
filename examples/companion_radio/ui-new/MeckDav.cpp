// CalDAV account (experimental) -- see MeckDav.h and docs/caldav-design.md
#if defined(MECK_DAV) && MECK_DAV

#include "MeckDav.h"
#include "MeckHaUtil.h"      // haHostIsLocal: the one place a self-signed certificate is accepted
#if defined(MECK_HA) && MECK_HA
  #include "MeckHa.h"
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/platform.h>
#include <qrcode.h>
#ifdef CONFIG_PM_ENABLE
  #include <esp_pm.h>
#endif

MeckDav meckDav;

#define DAV_NVS       "meckdav"
#define DAV_RESP_LEN  24576   // a calendar list with a few dozen calendars, or a sync listing

// --- Small helpers ------------------------------------------------------------

// mbedTLS in PSRAM, as the Web Reader and Home Assistant do
static void* davPsramCalloc(size_t num, size_t size) {
  void* p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(num, size);
}
static void davTlsInPsram() {
  static bool done = false;
  if (!done) { mbedtls_platform_set_calloc_free(davPsramCalloc, free); done = true; }
}

static void davRandomHex(char* out, int bytes) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < bytes; i++) {
    uint8_t b = (uint8_t)esp_random();
    out[2 * i] = hex[b >> 4];
    out[2 * i + 1] = hex[b & 15];
  }
  out[2 * bytes] = 0;
}

static String davHtmlEsc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += *s;
    }
  }
  return o;
}

// Collects a response body into a fixed buffer; the rest is read and dropped
// so HTTPClient finishes the response (chunked included) either way
class DavBufStream : public Stream {
public:
  DavBufStream(char* buf, size_t cap) : _buf(buf), _cap(cap) { if (cap) buf[0] = 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    if (_cap) {
      size_t k = _len + 1 < _cap ? min(n, _cap - 1 - _len) : 0;
      memcpy(_buf + _len, b, k);
      _len += k;
      _buf[_len] = 0;
    }
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t length() const { return _len; }
private:
  char* _buf;
  size_t _cap, _len = 0;
};

// What the discovery collects from the replies
struct DavDisc {
  char principal[200];
  char home[200];
  MeckDav::Cal* cals;
  int n, max;
  char base[200];    // the URL the reply is for: hrefs are resolved against it
};

// Name of a calendar without a displayname: the last part of its path
static void davLastSegment(const char* href, char* out, size_t n) {
  size_t l = strlen(href);
  while (l && href[l - 1] == '/') l--;
  size_t s = l;
  while (s && href[s - 1] != '/') s--;
  size_t k = l - s < n - 1 ? l - s : n - 1;
  memcpy(out, href + s, k);
  out[k] = 0;
}

static void davDiscCb(const DavResponse& r, void* c) {
  DavDisc* d = (DavDisc*)c;
  if (r.principal[0] && !d->principal[0]) davResolve(d->base, r.principal, d->principal, sizeof(d->principal));
  if (r.home[0] && !d->home[0]) davResolve(d->base, r.home, d->home, sizeof(d->home));
  // A calendar that lists its components and has no VEVENT (iCloud's Reminders) is not for us
  if (d->cals && r.isCalendar && (r.hasVevent || !r.hasComps) && d->n < d->max && r.status != 404) {
    MeckDav::Cal& k = d->cals[d->n];
    if (!davResolve(d->base, r.href, k.url, sizeof(k.url))) return;
    if (r.name[0]) { snprintf(k.name, sizeof(k.name), "%.*s", (int)sizeof(k.name) - 1, r.name); davTrimUtf8(k.name); }
    else davLastSegment(r.href, k.name, sizeof(k.name));
    if (!k.name[0]) snprintf(k.name, sizeof(k.name), "Calendar");
    d->n++;
  }
}

static const char* davErrText(int rc) {
  if (rc == -2) return "The server sent us to another site";
  if (rc < 0) return "Cannot reach the server (address, WiFi, clock?)";
  if (rc == 401 || rc == 403) return "Wrong user name or password";
  if (rc == 404) return "Address not found on the server";
  return "That address does not answer like a CalDAV server";
}

// --- NVS ------------------------------------------------------------------------

void MeckDav::begin() { load(); }

void MeckDav::load() {
  Preferences p;
  if (!p.begin(DAV_NVS, true)) return;   // nothing stored yet
  p.getString("url", _url, sizeof(_url));
  p.getString("user", _user, sizeof(_user));
  p.getString("pass", _pass, sizeof(_pass));
  int n = p.getUChar("n", 0);
  _calCount = 0;
  for (int i = 0; i < n && i < MAX_CALS; i++) {
    char k[16];
    snprintf(k, sizeof(k), "cu%d", i);
    if (!p.getString(k, _cals[_calCount].url, sizeof(_cals[0].url))) continue;
    snprintf(k, sizeof(k), "cn%d", i);
    p.getString(k, _cals[_calCount].name, sizeof(_cals[0].name));
    _calCount++;
  }
  p.end();
}

void MeckDav::save() {
  Preferences p;
  if (!p.begin(DAV_NVS, false)) { Serial.println("DAV: NVS open failed"); return; }
  p.clear();
  p.putString("url", _url);
  p.putString("user", _user);
  p.putString("pass", _pass);
  p.putUChar("n", (uint8_t)_calCount);
  for (int i = 0; i < _calCount; i++) {
    char k[16];
    snprintf(k, sizeof(k), "cu%d", i);
    p.putString(k, _cals[i].url);
    snprintf(k, sizeof(k), "cn%d", i);
    p.putString(k, _cals[i].name);
  }
  p.end();
}

void MeckDav::forget() {
  // Basic authentication has nothing to revoke from here: change or remove the
  // deck's user on the server to cut it off
  memset(_pass, 0, sizeof(_pass));
  _url[0] = _user[0] = _pass[0] = 0;
  _calCount = 0;
  memset(_cals, 0, sizeof(_cals));
  Preferences p;
  if (p.begin(DAV_NVS, false)) { p.clear(); p.end(); }
  Serial.println("DAV: account forgotten");
}

// --- Pairing --------------------------------------------------------------------

uint32_t MeckDav::pairLeftMs() const {
  if (!pairingActive()) return 0;
  uint32_t used = millis() - _pairStart;
  return used >= PAIR_WINDOW_MS ? 0 : PAIR_WINDOW_MS - used;
}

void MeckDav::startPairing() {
  stopPairing();
  _error[0] = 0;
  if (WiFi.status() != WL_CONNECTED || !(WiFi.getMode() & WIFI_STA)) {
    _phase = PH_NO_WIFI;
    return;
  }
#if defined(MECK_HA) && MECK_HA
  if (meckHa.pairingActive()) {   // both want port 80
    snprintf(_error, sizeof(_error), "Home Assistant pairing is running");
    _phase = PH_FAILED;
    return;
  }
#endif
  String ip = WiFi.localIP().toString();
  davRandomHex(_token, 8);     // one-time: valid while this pairing waits for the form
  snprintf(_pageUrl, sizeof(_pageUrl), "http://%s/dav/%s", ip.c_str(), _token);
  snprintf(_statusPath, sizeof(_statusPath), "/dav/%s/s", _token);
  _tries = 0;
  _pairStart = millis();
  _phase = PH_SCAN;
  makeQr();
  serverStart();
  awake(true);
  Serial.printf("DAV: pairing, page %s\n", _pageUrl);
}

void MeckDav::stopPairing() {
  serverStop();
  endDiscovery();
  awake(false);
  memset(_token, 0, sizeof(_token));
  _phase = PH_OFF;
}

void MeckDav::endDiscovery() {
  if (_resp) { heap_caps_free(_resp); _resp = nullptr; }
  memset(_pairPass, 0, sizeof(_pairPass));
  _pairUser[0] = _pairUrl[0] = 0;
  _principal[0] = _home[0] = 0;
  _workStep = 0;
}

void MeckDav::fail(const char* why) {
  snprintf(_error, sizeof(_error), "%s", why);
  Serial.printf("DAV: failed: %s\n", why);
  endDiscovery();
  awake(false);
  _doneAt = millis();   // the server stays up a minute: the phone gets the reason on its status page
  _phase = PH_FAILED;
}

// Wrong password, wrong address: the phone gets the form again, a few times
void MeckDav::retry(const char* why) {
  Serial.printf("DAV: %s\n", why);
  if (_blocking || ++_tries >= MAX_TRIES) { fail(why); return; }
  snprintf(_error, sizeof(_error), "%s", why);
  endDiscovery();
  _phase = PH_SCAN;
}

// QR of the page URL, kept as bits: the encoder's handle only lives in its callback
static uint8_t* s_davQrBits = nullptr;
static int* s_davQrSize = nullptr;
static void davQrCallback(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > MeckDav::QR_MAX) { *s_davQrSize = 0; return; }
  memset(s_davQrBits, 0, (MeckDav::QR_MAX * MeckDav::QR_MAX + 7) / 8);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) s_davQrBits[(y * n + x) >> 3] |= 1 << ((y * n + x) & 7);
  *s_davQrSize = n;
}

void MeckDav::makeQr() {
  _qrSize = 0;
  s_davQrBits = _qr;
  s_davQrSize = &_qrSize;
  esp_qrcode_config_t cfg = { davQrCallback, 5, ESP_QRCODE_ECC_MED };
  if (esp_qrcode_generate(&cfg, _pageUrl) != ESP_OK) _qrSize = 0;
}

bool MeckDav::qrModule(int x, int y) const {
  if (x < 0 || y < 0 || x >= _qrSize || y >= _qrSize) return false;
  int i = y * _qrSize + x;
  return (_qr[i >> 3] >> (i & 7)) & 1;
}

// --- Web server ------------------------------------------------------------------

#define DAV_SRV ((WebServer*)_server)

void MeckDav::serverStart() {
  if (_server) return;
  WebServer* s = new WebServer(80);
  _server = s;
  static char path[48];
  snprintf(path, sizeof(path), "/dav/%s", _token);
  s->on(path, HTTP_GET, [this]() { pageForm(); });
  s->on(path, HTTP_POST, [this]() { pageSubmit(); });
  s->on(_statusPath, HTTP_GET, [this]() { pageStatus(); });
  s->onNotFound([this]() { DAV_SRV->send(404, "text/plain", "Not found"); });
  s->begin();
}

void MeckDav::serverStop() {
  if (!_server) return;
  DAV_SRV->stop();
  delete DAV_SRV;
  _server = nullptr;
}

void MeckDav::sendPage(int code, const char* title, const char* body, int refreshSecs) {
  String h = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">";
  if (refreshSecs > 0) h += "<meta http-equiv=refresh content=\"" + String(refreshSecs) + ";url=" + _statusPath + "\">";
  h += "<title>Meck - Calendar account</title><style>"
       "body{font-family:system-ui,sans-serif;max-width:30em;margin:1.5em auto;padding:0 1em;line-height:1.4}"
       "input,button{font-size:1.05em;width:100%;padding:.6em;box-sizing:border-box;margin:.3em 0}"
       "small{color:#555}.err{color:#b00020}</style></head><body><h2>";
  h += title;
  h += "</h2>";
  h += body;
  h += "</body></html>";
  DAV_SRV->sendHeader("Cache-Control", "no-store");
  DAV_SRV->send(code, "text/html", h);
}

void MeckDav::pageForm() {
  if (_phase != PH_SCAN) { pageStatus(); return; }
  String b;
  if (_error[0]) { b += "<p class=err>"; b += davHtmlEsc(_error); b += "</p>"; }
  b += "<form method=post action=\"/dav/";
  b += _token;
  b += "\"><input name=url type=url autocapitalize=off autocorrect=off spellcheck=false "
       "placeholder=\"https://dav.example.com\" value=\"";
  b += davHtmlEsc(_url[0] ? _url : "https://");
  b += "\"><input name=user autocapitalize=off autocorrect=off spellcheck=false placeholder=\"User name\" value=\"";
  b += davHtmlEsc(_user);
  b += "\"><input name=pass type=password autocomplete=off placeholder=\"Password\">"
       "<button>Connect</button></form><p><small>Any CalDAV server (Radicale, Nextcloud, Fastmail, "
       "iCloud with an app-specific password). The deck uses its own user name, one that can be switched "
       "off on the server. The password travels from this phone to the deck over your WiFi without "
       "encryption: do this at home.</small></p>";
  sendPage(200, "Add a calendar account to the deck", b.c_str());
}

void MeckDav::pageSubmit() {
  if (_phase != PH_SCAN) { pageStatus(); return; }
  char url[128];
  String u = DAV_SRV->arg("url"), n = DAV_SRV->arg("user"), p = DAV_SRV->arg("pass");
  if (!davNormalizeUrl(u.c_str(), url, sizeof(url))) {
    snprintf(_error, sizeof(_error), "That is not a server address");
    pageForm();
    return;
  }
  if (n.length() == 0 || n.length() >= sizeof(_pairUser) || p.length() == 0 || p.length() >= sizeof(_pairPass)) {
    snprintf(_error, sizeof(_error), "User name and password are needed (up to 63 characters each)");
    pageForm();
    return;
  }
  work(url, n.c_str(), p.c_str());
  _error[0] = 0;
  DAV_SRV->sendHeader("Location", _statusPath, true);
  DAV_SRV->send(303, "text/plain", "");
  Serial.printf("DAV: form sent for %s\n", url);
}

void MeckDav::pageStatus() {
  switch (_phase) {
    case PH_WORKING: {
      String b = "<p>";
      b += davHtmlEsc(_step);
      b += "...</p>";
      sendPage(200, "Checking the server", b.c_str(), 2);
      return;
    }
    case PH_SCAN: {
      String b;
      if (_error[0]) { b += "<p class=err>"; b += davHtmlEsc(_error); b += "</p>"; }
      b += "<p><a href=\"/dav/";
      b += _token;
      b += "\">Back to the form</a></p>";
      sendPage(200, "Not connected", b.c_str());
      return;
    }
    case PH_DONE: {
      String b = "<p>The deck is connected to <b>";
      b += davHtmlEsc(_user);
      b += "</b> and found these calendars:</p><ul>";
      for (int i = 0; i < _calCount; i++) { b += "<li>"; b += davHtmlEsc(_cals[i].name); b += "</li>"; }
      b += "</ul><p>You can close this page.</p>";
      sendPage(200, "Done", b.c_str());
      return;
    }
    case PH_FAILED: {
      String b = "<p class=err>";
      b += davHtmlEsc(_error);
      b += "</p><p>Start again on the deck.</p>";
      sendPage(200, "Not connected", b.c_str());
      return;
    }
    default:
      sendPage(200, "Not pairing", "<p>The deck is not waiting for an account.</p>");
  }
}

// --- Discovery -------------------------------------------------------------------

void MeckDav::work(const char* url, const char* user, const char* pass) {
  snprintf(_pairUrl, sizeof(_pairUrl), "%s", url);
  snprintf(_pairUser, sizeof(_pairUser), "%s", user);
  snprintf(_pairPass, sizeof(_pairPass), "%s", pass);
  _principal[0] = _home[0] = 0;
  _tmpCount = 0;
  _workStep = 0;
  _step = "Looking for the account";
  _phase = PH_WORKING;
}

// PROPFIND current-user-principal on url; the answer's principal (absolute) goes to _principal
int MeckDav::propfindPrincipal(const char* url, char* location, size_t locationLen) {
  int rc = request("PROPFIND", url, 0, DAV_BODY_PRINCIPAL, location, locationLen);
  if (rc == 207) {
    DavDisc d;
    memset(&d, 0, sizeof(d));
    snprintf(d.base, sizeof(d.base), "%s", url);
    davParseMultistatus(_resp, strlen(_resp), davDiscCb, &d);
    if (d.principal[0]) snprintf(_principal, sizeof(_principal), "%s", d.principal);
  }
  return rc;
}

int MeckDav::findCalendars(const char* homeUrl) {
  int rc = request("PROPFIND", homeUrl, 1, DAV_BODY_CAL_LIST);
  _tmpCount = 0;
  if (rc == 207) {
    DavDisc d;
    memset(&d, 0, sizeof(d));
    snprintf(d.base, sizeof(d.base), "%s", homeUrl);
    d.cals = _tmpCals;
    d.max = MAX_CALS;
    memset(_tmpCals, 0, sizeof(_tmpCals));
    davParseMultistatus(_resp, strlen(_resp), davDiscCb, &d);
    _tmpCount = d.n;
  }
  return rc;
}

// One step per loop() pass, so the screen can follow
void MeckDav::workStep() {
  char loc[200], url[200];
  switch (_workStep) {
    case 0: {
      _step = "Looking for the account";
      int rc = propfindPrincipal(_pairUrl, loc, sizeof(loc));
      if (rc == 401 || rc == 403) { retry(davErrText(rc)); return; }
      if (rc < 0) { retry(davErrText(rc)); return; }
      // A bare host: the server may tell where its CalDAV lives (RFC 6764)
      if (!_principal[0] && strcmp(davUrlPath(_pairUrl), "/") == 0) {
        char origin[128];
        if (davUrlOrigin(_pairUrl, origin, sizeof(origin))) {
          snprintf(url, sizeof(url), "%s/.well-known/caldav", origin);
          rc = propfindPrincipal(url, loc, sizeof(loc));
          if (rc == 401 || rc == 403) { retry(davErrText(rc)); return; }
          if ((rc == 301 || rc == 302 || rc == 307 || rc == 308) && loc[0]) {
            char target[200];
            if (davResolve(url, loc, target, sizeof(target))) {
              rc = propfindPrincipal(target, loc, sizeof(loc));
              if (rc == 401 || rc == 403 || rc < 0) { retry(davErrText(rc)); return; }
            }
          }
        }
      }
      if (rc == 404 && !_principal[0]) { retry(davErrText(rc)); return; }
      // No principal: the address may be the calendar home itself
      if (!_principal[0]) { snprintf(_home, sizeof(_home), "%s", _pairUrl); _workStep = 2; }
      else _workStep = 1;
      return;
    }
    case 1: {
      _step = "Looking for the calendars";
      int rc = request("PROPFIND", _principal, 0, DAV_BODY_CAL_HOME);
      if (rc != 207) { retry(davErrText(rc)); return; }
      DavDisc d;
      memset(&d, 0, sizeof(d));
      snprintf(d.base, sizeof(d.base), "%s", _principal);
      davParseMultistatus(_resp, strlen(_resp), davDiscCb, &d);
      snprintf(_home, sizeof(_home), "%s", d.home[0] ? d.home : _principal);
      _workStep = 2;
      return;
    }
    case 2: {
      _step = "Reading the calendar list";
      int rc = findCalendars(_home);
      if (rc != 207) { retry(davErrText(rc)); return; }
      if (_tmpCount == 0) { retry("No calendars found at this address"); return; }
      _workStep = 3;
      return;
    }
    default: {
      _step = "Saving";
      snprintf(_url, sizeof(_url), "%s", _pairUrl);
      snprintf(_user, sizeof(_user), "%s", _pairUser);
      snprintf(_pass, sizeof(_pass), "%s", _pairPass);
      memcpy(_cals, _tmpCals, sizeof(_cals));
      _calCount = _tmpCount;
      save();
      endDiscovery();
      _error[0] = 0;
      _doneAt = millis();
      _phase = PH_DONE;
      Serial.printf("DAV: account saved, %d calendar(s)\n", _calCount);
    }
  }
}

void MeckDav::loop() {
  if (_server) DAV_SRV->handleClient();
  if (_phase == PH_SCAN) {
    if (pairLeftMs() == 0) fail("Timed out");
    else if (WiFi.status() != WL_CONNECTED) fail("WiFi lost");
  } else if (_phase == PH_WORKING && !_blocking) {
    if (pairLeftMs() == 0) fail("Timed out");
    else workStep();
  } else if ((_phase == PH_DONE || _phase == PH_FAILED) && _server && millis() - _doneAt > 60000) {
    serverStop();   // the phone had its "Done" page
    awake(false);
  }
}

// No automatic light sleep while the web server waits for the phone
void MeckDav::awake(bool on) {
  if (on == _awake) return;
  _awake = on;
#ifdef CONFIG_PM_ENABLE
  if (!_pmLock && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meck_dav",
                                     (esp_pm_lock_handle_t*)&_pmLock) != ESP_OK) return;
  if (on) esp_pm_lock_acquire((esp_pm_lock_handle_t)_pmLock);
  else esp_pm_lock_release((esp_pm_lock_handle_t)_pmLock);
#endif
}

// --- HTTP ------------------------------------------------------------------------

int MeckDav::request(const char* method, const char* url, int depth, const char* body,
                     char* location, size_t locationLen) {
  if (location && locationLen) location[0] = 0;
  if (!_resp) {
    _resp = (char*)heap_caps_malloc(DAV_RESP_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_resp) return -1;
  }
  _resp[0] = 0;
  if (WiFi.status() != WL_CONNECTED) return -1;
  // The password goes to the address the user typed in, wherever a reply points
  if (!davSameSite(_pairUrl, url)) return -2;
  char auth[200];
  if (!davBasicAuth(auth, sizeof(auth), _pairUser, _pairPass)) return -1;

  WiFiClient plain;
  WiFiClientSecure* tls = nullptr;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("Meck");
  const char* keys[] = { "Location" };
  http.collectHeaders(keys, 1);
  bool ok;
  if (davUrlIsHttps(url)) {
    davTlsInPsram();
    tls = new WiFiClientSecure();
    char host[96];
    davUrlHost(url, host, sizeof(host));
    // A LAN-only host has a self-signed certificate as a rule (as for Home
    // Assistant); everything reachable from the internet is checked against
    // the CA bundle, which needs the deck's clock to be right
    if (haHostIsLocal(host)) tls->setInsecure();
    else tls->useBuiltinCACertBundle();
    ok = http.begin(*tls, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) { delete tls; return -1; }
  http.addHeader("Authorization", auth);
  http.addHeader("Content-Type", "application/xml; charset=utf-8");
  http.addHeader("Depth", depth ? "1" : "0");
  int code = http.sendRequest(method, (uint8_t*)body, body ? strlen(body) : 0);
  memset(auth, 0, sizeof(auth));
  if (code > 0) {
    if (location && locationLen && http.hasHeader("Location"))
      snprintf(location, locationLen, "%s", http.header("Location").c_str());
    DavBufStream s(_resp, DAV_RESP_LEN);
    http.writeToStream(&s);
  }
  http.end();
  delete tls;
  return code;
}

// --- Debug CLI -------------------------------------------------------------------

static void davLsCb(const DavResponse& r, void* c) {
  int* n = (int*)c;
  if ((*n)++ < 10) Serial.printf("  %s %s%s\n", r.href, r.etag, r.status == 404 ? " (deleted)" : "");
}

void MeckDav::cmd(const char* arg) {
  static const char* const kPhase[] = { "off", "no-wifi", "scan", "working", "done", "failed" };
  if (strcmp(arg, "") == 0 || strcmp(arg, "status") == 0) {
    char m[24];
    davMask(m, sizeof(m), _pass);
    Serial.printf("DAV: %s, phase %s%s%s\n", configured() ? "account stored" : "no account", kPhase[_phase],
                  _error[0] ? ", error: " : "", _error);
    if (configured()) {
      Serial.printf("  server %s, user %s, password %s\n", _url, _user, m);
      for (int i = 0; i < _calCount; i++) Serial.printf("  [%d] %s  %s\n", i, _cals[i].name, _cals[i].url);
    }
    if (pairingActive()) Serial.printf("  page %s, %lu s left\n", _pageUrl, (unsigned long)(pairLeftMs() / 1000));
    return;
  }
  if (strcmp(arg, "pair") == 0) { startPairing(); Serial.printf("DAV: phase %s, page %s\n", kPhase[_phase], _pageUrl); return; }
  if (strcmp(arg, "stop") == 0) { stopPairing(); return; }
  if (strcmp(arg, "forget") == 0) { stopPairing(); forget(); return; }
  // "dav login <url> <user> <password>": discovery and save without the phone (blocking)
  if (strncmp(arg, "login ", 6) == 0) {
    char u[128], n[64], p[64], url[128];
    if (sscanf(arg + 6, "%127s %63s %63s", u, n, p) != 3 || !davNormalizeUrl(u, url, sizeof(url))) {
      Serial.println("DAV: dav login <url> <user> <password>");
      return;
    }
    stopPairing();
    _blocking = true;
    work(url, n, p);
    while (_phase == PH_WORKING) workStep();
    _blocking = false;
    Serial.printf("DAV: %s%s%s\n", _phase == PH_DONE ? "saved" : "failed", _error[0] ? ": " : "", _error);
    _phase = PH_OFF;
    memset(p, 0, sizeof(p));
    return;
  }
  // "dav test": the stored account again, nothing is changed on the server
  if (strcmp(arg, "test") == 0) {
    if (!configured()) { Serial.println("DAV: no account"); return; }
    stopPairing();
    _blocking = true;
    work(_url, _user, _pass);
    while (_phase == PH_WORKING) workStep();
    _blocking = false;
    Serial.printf("DAV: %s%s%s\n", _phase == PH_DONE ? "ok" : "failed", _error[0] ? ": " : "", _error);
    _phase = PH_OFF;
    return;
  }
  // "dav ls <n>": what is in calendar n (sync-collection, read only), the first ten
  if (strncmp(arg, "ls ", 3) == 0) {
    int i = atoi(arg + 3);
    if (!configured() || i < 0 || i >= _calCount) { Serial.println("DAV: no such calendar"); return; }
    snprintf(_pairUrl, sizeof(_pairUrl), "%s", _url);
    snprintf(_pairUser, sizeof(_pairUser), "%s", _user);
    snprintf(_pairPass, sizeof(_pairPass), "%s", _pass);
    int rc = request("REPORT", _cals[i].url, 1, DAV_BODY_SYNC_ALL);
    if (rc != 207) { Serial.printf("DAV: %d\n", rc); endDiscovery(); return; }
    int n = 0;
    char tok[40] = "";
    int cnt = davParseMultistatus(_resp, strlen(_resp), davLsCb, &n, tok, sizeof(tok));
    Serial.printf("DAV: %d item(s), reply %u bytes\n", cnt, (unsigned)strlen(_resp));
    endDiscovery();
    return;
  }
  Serial.println("DAV: dav [status] | pair | stop | forget | test | ls <n> | login <url> <user> <password>");
}

#endif
