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
#ifdef HAS_4G_MODEM
  #include "SMSContacts.h"   // where the synced contacts go
#endif
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <mbedtls/platform.h>
#include <qrcode.h>
#ifdef CONFIG_PM_ENABLE
  #include <esp_pm.h>
#endif

MeckDav meckDav;

#define DAV_NVS       "meckdav"
#define DAV_RESP_LEN  24576   // a calendar list with a few dozen calendars, or a sync listing
#define DAV_SYNC_LEN  (512 * 1024)   // contacts sync: a card listing, a batch of cards (PSRAM)
#define DAV_HREF_POOL (256 * 1024)   // the card paths of one address book (PSRAM)
#define DAV_MAX_CARDS 2000
#define DAV_BATCH_CARDS 20
#define DAV_BATCH_BYTES (360 * 1024) // what the cards of one multiget may add up to
#define DAV_SYNC_EVERY_MS  (6UL * 3600 * 1000)
#define DAV_SYNC_RETRY_MS  (15UL * 60 * 1000)
#define DAV_SYNC_FIRST_MS  (60UL * 1000)        // after boot: let WiFi and the rest settle

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

// Internal RAM is what the WiFi driver and every TCP connection draw on; the
// pairing page has to fit in what is left, so it is logged
static void davHeap(const char* what) {
  Serial.printf("DAV: %s, internal free=%u largest=%u lowest=%u\n", what,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
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
  bool full() const { return _cap && _len + 1 >= _cap; }
private:
  char* _buf;
  size_t _cap, _len = 0;
};

// What the discovery collects from the replies
struct DavDisc {
  char principal[200];
  char home[200];
  char abHome[200];
  MeckDav::Cal* cals;
  int n, max;
  MeckDav::Cal* books;
  int nb, maxb;
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
  if (r.abHome[0] && !d->abHome[0]) davResolve(d->base, r.abHome, d->abHome, sizeof(d->abHome));
  if (d->books && r.isAddressbook && d->nb < d->maxb && r.status != 404) {
    MeckDav::Cal& k = d->books[d->nb];
    if (!davResolve(d->base, r.href, k.url, sizeof(k.url))) return;
    for (int i = 0; i < d->nb; i++) if (!strcmp(d->books[i].url, k.url)) return;   // home listed twice
    if (r.name[0]) { snprintf(k.name, sizeof(k.name), "%.*s", (int)sizeof(k.name) - 1, r.name); davTrimUtf8(k.name); }
    else davLastSegment(r.href, k.name, sizeof(k.name));
    if (!k.name[0]) snprintf(k.name, sizeof(k.name), "Contacts");
    d->nb++;
    return;
  }
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
  if (rc == -3) return "The server's reply is too large";
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
  n = p.getUChar("nb", 0);
  _bookCount = 0;
  for (int i = 0; i < n && i < MAX_BOOKS; i++) {
    char k[16];
    snprintf(k, sizeof(k), "bu%d", i);
    if (!p.getString(k, _books[_bookCount].url, sizeof(_books[0].url))) continue;
    snprintf(k, sizeof(k), "bn%d", i);
    p.getString(k, _books[_bookCount].name, sizeof(_books[0].name));
    snprintf(k, sizeof(k), "bt%d", i);
    p.getString(k, _bookTokens[_bookCount], sizeof(_bookTokens[0]));
    _bookCount++;
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
  p.putUChar("nb", (uint8_t)_bookCount);
  for (int i = 0; i < _bookCount; i++) {
    char k[16];
    snprintf(k, sizeof(k), "bu%d", i);
    p.putString(k, _books[i].url);
    snprintf(k, sizeof(k), "bn%d", i);
    p.putString(k, _books[i].name);
    snprintf(k, sizeof(k), "bt%d", i);
    p.putString(k, _bookTokens[i]);
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
  syncEnd();
  _bookCount = 0;
  memset(_books, 0, sizeof(_books));
  memset(_bookTokens, 0, sizeof(_bookTokens));
  _syncAt = 0;
  _syncErr[0] = 0;
  _syncWanted = false;
#ifdef HAS_4G_MODEM
  smsContacts.clearSynced();
#endif
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
  syncEnd();
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
  davHeap("before server");
  serverStart();
  awake(true);
  davHeap("server up");
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
  if (_resp) { heap_caps_free(_resp); _resp = nullptr; _respCap = 0; }
  memset(_pairPass, 0, sizeof(_pairPass));
  _pairUser[0] = _pairUrl[0] = 0;
  _principal[0] = _home[0] = _abHome[0] = 0;
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
  s->onNotFound([this]() { davHeap("other request"); DAV_SRV->send(404, "text/plain", "Not found"); });
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
  davHeap("form requested");
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
  davHeap("form posted");
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
  _principal[0] = _home[0] = _abHome[0] = 0;
  _tmpCount = _tmpBookCount = 0;
  memset(_tmpCals, 0, sizeof(_tmpCals));
  memset(_tmpBooks, 0, sizeof(_tmpBooks));
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

// Calendars and address books in a home collection (Radicale keeps both in one);
// added to what an earlier home gave
int MeckDav::findCalendars(const char* homeUrl) {
  int rc = request("PROPFIND", homeUrl, 1, DAV_BODY_CAL_LIST);
  if (rc == 207) {
    DavDisc d;
    memset(&d, 0, sizeof(d));
    snprintf(d.base, sizeof(d.base), "%s", homeUrl);
    d.cals = _tmpCals;
    d.n = _tmpCount;
    d.max = MAX_CALS;
    d.books = _tmpBooks;
    d.nb = _tmpBookCount;
    d.maxb = MAX_BOOKS;
    davParseMultistatus(_resp, strlen(_resp), davDiscCb, &d);
    _tmpCount = d.n;
    _tmpBookCount = d.nb;
  }
  return rc;
}

// A bare host: the server may tell where its CalDAV or CardDAV lives (RFC 6764)
int MeckDav::wellKnown(const char* which) {
  char origin[128], url[200], loc[200];
  if (!davUrlOrigin(_pairUrl, origin, sizeof(origin))) return 404;
  snprintf(url, sizeof(url), "%s/.well-known/%s", origin, which);
  int rc = propfindPrincipal(url, loc, sizeof(loc));
  if ((rc == 301 || rc == 302 || rc == 307 || rc == 308) && loc[0]) {
    char target[200];
    if (davResolve(url, loc, target, sizeof(target))) rc = propfindPrincipal(target, loc, sizeof(loc));
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
      // A bare host: the server may tell where its CalDAV or CardDAV lives
      if (!_principal[0] && strcmp(davUrlPath(_pairUrl), "/") == 0) {
        rc = wellKnown("caldav");
        if (!_principal[0] && rc != 401 && rc != 403 && rc >= 0) rc = wellKnown("carddav");
        if (rc == 401 || rc == 403 || rc < 0) { retry(davErrText(rc)); return; }
      }
      if (rc == 404 && !_principal[0]) { retry(davErrText(rc)); return; }
      // No principal: the address may be the home (or a calendar, an address book) itself
      if (!_principal[0]) { snprintf(_home, sizeof(_home), "%s", _pairUrl); _workStep = 2; }
      else _workStep = 1;
      return;
    }
    case 1: {
      _step = "Looking for the calendars and contacts";
      int rc = request("PROPFIND", _principal, 0, DAV_BODY_CAL_HOME);
      if (rc != 207) { retry(davErrText(rc)); return; }
      DavDisc d;
      memset(&d, 0, sizeof(d));
      snprintf(d.base, sizeof(d.base), "%s", _principal);
      davParseMultistatus(_resp, strlen(_resp), davDiscCb, &d);
      snprintf(_home, sizeof(_home), "%s", d.home[0] ? d.home : d.abHome[0] ? d.abHome : _principal);
      if (d.abHome[0] && strcmp(d.abHome, _home) != 0) snprintf(_abHome, sizeof(_abHome), "%s", d.abHome);
      _workStep = 2;
      return;
    }
    case 2: {
      _step = "Reading the calendar list";
      int rc = findCalendars(_home);
      if (rc != 207) { retry(davErrText(rc)); return; }
      _workStep = _abHome[0] ? 3 : 4;
      return;
    }
    case 3: {
      _step = "Reading the address books";
      int rc = findCalendars(_abHome);   // contacts kept apart (Nextcloud, iCloud)
      if (rc != 207) { retry(davErrText(rc)); return; }
      _workStep = 4;
      return;
    }
    case 4: {
      if (_tmpCount == 0 && _tmpBookCount == 0) { retry("No calendars or address books found at this address"); return; }
      _workStep = 5;
      return;
    }
    default: {
      _step = "Saving";
      snprintf(_url, sizeof(_url), "%s", _pairUrl);
      snprintf(_user, sizeof(_user), "%s", _pairUser);
      snprintf(_pass, sizeof(_pass), "%s", _pairPass);
      memcpy(_cals, _tmpCals, sizeof(_cals));
      _calCount = _tmpCount;
      // An address book already synced keeps its state: only what changed is fetched again
      char tokens[MAX_BOOKS][sizeof(_bookTokens[0])];
      memset(tokens, 0, sizeof(tokens));
      for (int i = 0; i < _tmpBookCount; i++)
        for (int j = 0; j < _bookCount; j++)
          if (!strcmp(_tmpBooks[i].url, _books[j].url)) memcpy(tokens[i], _bookTokens[j], sizeof(tokens[i]));
      memcpy(_books, _tmpBooks, sizeof(_books));
      memcpy(_bookTokens, tokens, sizeof(_bookTokens));
      _bookCount = _tmpBookCount;
      save();
      endDiscovery();
      _error[0] = 0;
      _doneAt = millis();
      _phase = PH_DONE;
      _syncWanted = _bookCount > 0;
      Serial.printf("DAV: account saved, %d calendar(s), %d address book(s)\n", _calCount, _bookCount);
    }
  }
}

void MeckDav::loop(bool locked) {
  if (_server) DAV_SRV->handleClient();
  if (!pairingActive() && !_blocking) syncTick(locked);
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
  // WiFi modem sleep (Arduino's default) wakes the station only for DTIM
  // beacons: a phone looking for the deck gets no ARP answer for seconds and
  // the page crawls or never opens. Off while the server waits, back after.
  if (on) {
    _psSaved = esp_wifi_get_ps((wifi_ps_type_t*)&_psBefore) == ESP_OK;
    if (_psSaved) esp_wifi_set_ps(WIFI_PS_NONE);
  } else if (_psSaved) {
    esp_wifi_set_ps((wifi_ps_type_t)_psBefore);
    _psSaved = false;
  }
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
  size_t want = _sy != SY_IDLE ? DAV_SYNC_LEN : DAV_RESP_LEN;
  if (_resp && _respCap != want) { heap_caps_free(_resp); _resp = nullptr; }
  if (!_resp) {
    _resp = (char*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_resp) return -1;
    _respCap = want;
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
    DavBufStream s(_resp, _respCap);
    http.writeToStream(&s);
    if (s.full()) code = -3;
  }
  http.end();
  delete tls;
  return code;
}

// --- Contacts sync (CardDAV, read only) -------------------------------------------
//
// One request per loop() pass, like the discovery: the state of every address
// book (ctag / sync-token); when one has changed, the card list of each book
// and the cards in batches (addressbook-multiget). The numbers go to
// smsContacts as a whole new list; nothing is written to the server. WiFi
// only, never while the deck is locked.

#ifdef HAS_4G_MODEM

static int davNameCmp(const void* a, const void* b) {
  int c = strcasecmp(((const SMSContact*)a)->name, ((const SMSContact*)b)->name);
  return c ? c : strcmp(((const SMSContact*)a)->phone, ((const SMSContact*)b)->phone);
}

// One number of a card into the list being built
static void davAddContact(const char* name, const char* phone, void* c) {
  MeckDav* m = (MeckDav*)c;
  m->syncAdd(name, phone);
}

void MeckDav::syncAdd(const char* name, const char* phone) {
  if (!_syList || _syN >= SMS_SYNCED_MAX) { _syDropped++; return; }
  for (int i = 0; i < _syN; i++) if (!strcmp(_syList[i].phone, phone)) return;   // in two cards
  SMSContact& k = _syList[_syN];
  snprintf(k.phone, sizeof(k.phone), "%s", phone);
  snprintf(k.name, sizeof(k.name), "%s", name);
  davTrimUtf8(k.name);
  for (char* p = k.name; *p; p++) if (*p == '=' || *p == '\n' || *p == '\r') *p = ' ';   // the file's format
  k.valid = true;
  _syN++;
}

// The card paths of a listing: everything in the book that is not a collection
struct DavHrefList {
  char* pool;
  size_t used, cap;
  uint32_t* off;
  int32_t* len;
  int n, max;
  const char* book;   // the book's own URL: its own entry is skipped
};

static void davHrefCb(const DavResponse& r, void* c) {
  DavHrefList* h = (DavHrefList*)c;
  if (r.isCollection || r.status == 404 || !r.href[0] || h->n >= h->max) return;
  // Paths only: a multiget names cards by path on the book's server
  const char* path = r.href;
  if (!strncmp(path, "http://", 7) || !strncmp(path, "https://", 8)) path = davUrlPath(path);
  if (!strcmp(path, davUrlPath(h->book))) return;
  size_t l = strlen(path) + 1;
  if (h->used + l > h->cap) return;
  memcpy(h->pool + h->used, path, l);
  h->off[h->n] = (uint32_t)h->used;
  h->len[h->n] = (int32_t)r.contentLength;
  h->used += l;
  h->n++;
}

static void davCardsCb(const DavResponse& r, void* c) {
  if (r.data && r.status != 404) davVcardParse(r.data, r.dataLen, davAddContact, c);
}

static void davStateCb(const DavResponse& r, void* c) {
  char* tok = (char*)c;
  // sync-token changes with every change; getctag is the older way to say it
  if (!tok[0] && r.syncToken[0]) snprintf(tok, 120, "%s", r.syncToken);
  else if (!tok[0] && r.ctag[0]) snprintf(tok, 120, "%s", r.ctag);
}

uint32_t MeckDav::syncAgoMs() const {
  if (!_syncAt) return 0;
  uint32_t d = millis() - _syncAt;
  return d ? d : 1;
}

void MeckDav::syncNow() {
  if (!configured() || _bookCount == 0) return;
  _syncWanted = true;
}

bool MeckDav::syncDue() const {
  if (!configured() || _bookCount == 0) return false;
  if (_syncWanted) return true;
  uint32_t now = millis();
  if (_syncAt == 0) return now > DAV_SYNC_FIRST_MS;
  return now - _syncAt >= (_syncOk ? DAV_SYNC_EVERY_MS : DAV_SYNC_RETRY_MS);
}

void MeckDav::syncTick(bool locked) {
  if (_sy == SY_IDLE) {
    if (locked || !syncDue() || WiFi.status() != WL_CONNECTED || !(WiFi.getMode() & WIFI_STA)) return;
    syncStart();
    return;
  }
  if (locked) return;   // goes on after the unlock
  if (WiFi.status() != WL_CONNECTED) { syncFail("WiFi lost"); return; }
  syncStep();
}

void MeckDav::syncStart() {
  _syncWanted = false;
  snprintf(_pairUrl, sizeof(_pairUrl), "%s", _url);
  snprintf(_pairUser, sizeof(_pairUser), "%s", _user);
  snprintf(_pairPass, sizeof(_pairPass), "%s", _pass);
  memset(_syTokens, 0, sizeof(_syTokens));
  _syBook = 0;
  _syChanged = false;
  _syN = _syDropped = 0;
  _sy = SY_STATE;
  Serial.printf("DAV: contacts sync, %d address book(s)\n", _bookCount);
}

void MeckDav::syncEnd() {
  if (_syList) { heap_caps_free(_syList); _syList = nullptr; }
  if (_syPool) { heap_caps_free(_syPool); _syPool = nullptr; }
  if (_syOff) { heap_caps_free(_syOff); _syOff = nullptr; }
  if (_syLen) { heap_caps_free(_syLen); _syLen = nullptr; }
  if (_syBody) { heap_caps_free(_syBody); _syBody = nullptr; }
  if (_sy != SY_IDLE) endDiscovery();   // the reply buffer and the copied password
  _sy = SY_IDLE;
}

void MeckDav::syncFail(const char* why) {
  snprintf(_syncErr, sizeof(_syncErr), "%s", why);
  Serial.printf("DAV: contacts sync failed: %s\n", why);
  _syncOk = false;
  _syncAt = millis();
  if (!_syncAt) _syncAt = 1;
  syncEnd();
}

void MeckDav::syncStep() {
  static const uint32_t PSRAM = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  switch (_sy) {
    case SY_STATE: {   // has book _syBook changed?
      const Cal& b = _books[_syBook];
      int rc = request("PROPFIND", b.url, 0, DAV_BODY_BOOK_STATE);
      if (rc != 207) { syncFail(davErrText(rc)); return; }
      davParseMultistatus(_resp, strlen(_resp), davStateCb, _syTokens[_syBook]);
      if (!_syTokens[_syBook][0] || strcmp(_syTokens[_syBook], _bookTokens[_syBook]) != 0) _syChanged = true;
      if (++_syBook < _bookCount) return;
      if (!_syChanged && smsContacts.syncedCount() > 0) {
        Serial.println("DAV: contacts unchanged");
        _syncOk = true;
        _syncErr[0] = 0;
        _syncAt = millis();
        if (!_syncAt) _syncAt = 1;
        syncEnd();
        return;
      }
      // Something changed: all the books again, so the list is whole
      _syList = (SMSContact*)heap_caps_calloc(SMS_SYNCED_MAX, sizeof(SMSContact), PSRAM);
      _syPool = (char*)heap_caps_malloc(DAV_HREF_POOL, PSRAM);
      _syOff = (uint32_t*)heap_caps_malloc(DAV_MAX_CARDS * sizeof(uint32_t), PSRAM);
      _syLen = (int32_t*)heap_caps_malloc(DAV_MAX_CARDS * sizeof(int32_t), PSRAM);
      _syBody = (char*)heap_caps_malloc(DAV_BODY_LEN, PSRAM);
      if (!_syList || !_syPool || !_syOff || !_syLen || !_syBody) { syncFail("Out of memory"); return; }
      _syBook = 0;
      _sy = SY_LIST;
      return;
    }
    case SY_LIST: {   // the cards of book _syBook
      const Cal& b = _books[_syBook];
      int rc = request("PROPFIND", b.url, 1, DAV_BODY_BOOK_LIST);
      if (rc != 207) { syncFail(davErrText(rc)); return; }
      DavHrefList h = { _syPool, 0, DAV_HREF_POOL, _syOff, _syLen, 0, DAV_MAX_CARDS, b.url };
      davParseMultistatus(_resp, strlen(_resp), davHrefCb, &h);
      _syCards = h.n;
      _syNext = 0;
      Serial.printf("DAV: %s: %d card(s)\n", b.name, _syCards);
      _sy = _syCards ? SY_FETCH : SY_NEXT;
      return;
    }
    case SY_FETCH: {   // the next batch of cards
      const char* hrefs[DAV_BATCH_CARDS];
      int n = 0;
      long bytes = 0;
      while (_syNext < _syCards && n < DAV_BATCH_CARDS) {
        long l = _syLen[_syNext] > 0 ? _syLen[_syNext] : 4096;
        if (l > DAV_BATCH_BYTES) {   // a card with a large photo: alone it would not fit either
          Serial.printf("DAV: card of %ld bytes skipped\n", l);
          _syNext++;
          continue;
        }
        if (n && bytes + l > DAV_BATCH_BYTES) break;
        hrefs[n++] = _syPool + _syOff[_syNext++];
        bytes += l;
      }
      if (n) {
        if (!davMultigetBody(_syBody, DAV_BODY_LEN, hrefs, n)) { syncFail("Card names too long"); return; }
        int rc = request("REPORT", _books[_syBook].url, 1, _syBody);
        if (rc != 207) { syncFail(davErrText(rc)); return; }
        davParseMultistatus(_resp, strlen(_resp), davCardsCb, this);
      }
      if (_syNext >= _syCards) _sy = SY_NEXT;
      return;
    }
    case SY_NEXT: {
      if (++_syBook < _bookCount) { _sy = SY_LIST; return; }
      // All read: the new list replaces the old one, then the books count as synced
      qsort(_syList, _syN, sizeof(SMSContact), davNameCmp);
      SMSContact* list = _syList;
      _syList = nullptr;   // smsContacts owns it now (and frees it if the write fails)
      int n = _syN;
      if (!smsContacts.replaceSynced(list, n)) { syncFail("Cannot write to the SD card"); return; }
      memcpy(_bookTokens, _syTokens, sizeof(_bookTokens));
      save();
      _syncOk = true;
      _syncErr[0] = 0;
      _syncAt = millis();
      if (!_syncAt) _syncAt = 1;
      Serial.printf("DAV: contacts synced, %d number(s)%s\n", n, _syDropped ? " (list full, some left out)" : "");
      syncEnd();
      return;
    }
    default:
      syncEnd();
  }
}

#else   // no phone, no place for the contacts

uint32_t MeckDav::syncAgoMs() const { return 0; }
void MeckDav::syncNow() {}
bool MeckDav::syncDue() const { return false; }
void MeckDav::syncTick(bool) {}
void MeckDav::syncStart() {}
void MeckDav::syncEnd() { _sy = SY_IDLE; }
void MeckDav::syncFail(const char*) {}
void MeckDav::syncStep() {}
void MeckDav::syncAdd(const char*, const char*) {}

#endif

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
      for (int i = 0; i < _bookCount; i++)
        Serial.printf("  book %s  %s  %s\n", _books[i].name, _books[i].url, _bookTokens[i][0] ? "synced" : "not synced");
#ifdef HAS_4G_MODEM
      Serial.printf("  contacts: %d synced, %s", smsContacts.syncedCount(), syncing() ? "syncing now" : _syncAt ? "" : "no sync since boot");
      if (!syncing() && _syncAt) Serial.printf("last sync %lu min ago %s%s", (unsigned long)(syncAgoMs() / 60000),
                                               _syncOk ? "ok" : "failed: ", _syncOk ? "" : _syncErr);
      Serial.println();
#endif
    }
    davHeap("heap");
    if (pairingActive()) Serial.printf("  page %s, %lu s left\n", _pageUrl, (unsigned long)(pairLeftMs() / 1000));
    return;
  }
  // "dav sync": the contacts now, waiting for the end (blocking); read only
  if (strcmp(arg, "sync") == 0) {
    if (!configured() || _bookCount == 0) { Serial.println("DAV: no address book"); return; }
    if (WiFi.status() != WL_CONNECTED) { Serial.println("DAV: no WiFi"); return; }
    stopPairing();
    syncEnd();
    syncStart();
    while (_sy != SY_IDLE) {
      if (WiFi.status() != WL_CONNECTED) { syncFail("WiFi lost"); break; }
      syncStep();
    }
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
    syncEnd();   // shares the credentials and the reply buffer
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
    syncEnd();   // shares the credentials and the reply buffer
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
    syncEnd();
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
  Serial.println("DAV: dav [status] | pair | stop | forget | test | sync | ls <n> | login <url> <user> <password>");
}

#endif
