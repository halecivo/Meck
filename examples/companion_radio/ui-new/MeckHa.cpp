// Home Assistant link (experimental) -- see MeckHa.h and docs/home-assistant.md
#if defined(MECK_HA) && MECK_HA

#include "MeckHa.h"
#include "MeckNet.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/platform.h>
#include <mbedtls/sha256.h>
#include <mdns.h>
#include <qrcode.h>
#include <time.h>
#ifdef CONFIG_PM_ENABLE
  #include <esp_pm.h>
#endif

MeckHa meckHa;

#define HA_NVS        "meckha"
#define HA_RESP_LEN   24576   // /api/config lists every integration before the URLs
#define HA_BODY_LEN   1536

// --- Small helpers ------------------------------------------------------------

// mbedTLS in PSRAM: internal RAM has no block large enough for a handshake
// next to WiFi and Bluetooth (the Web Reader does the same)
static void* haPsramCalloc(size_t num, size_t size) {
  void* p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(num, size);
}
static void haTlsInPsram() {
  static bool done = false;
  if (!done) { mbedtls_platform_set_calloc_free(haPsramCalloc, free); done = true; }
}

static char* haAlloc(size_t n) {
  char* p = (char*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) p[0] = 0;
  return p;
}

static void haRandomHex(char* out, int bytes) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < bytes; i++) {
    uint8_t b = (uint8_t)esp_random();
    out[2 * i] = hex[b >> 4];
    out[2 * i + 1] = hex[b & 15];
  }
  out[2 * bytes] = 0;
}

static String haHtmlEsc(const char* s) {
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

static uint32_t haNowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
}

// Collects a response body into a fixed buffer; the rest is read and dropped,
// so HTTPClient finishes the response (chunked included) either way
class HaBufStream : public Stream {
public:
  HaBufStream(char* buf, size_t cap) : _buf(buf), _cap(cap) { if (cap) buf[0] = 0; }
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
private:
  char* _buf;
  size_t _cap, _len = 0;
};

// --- NVS ------------------------------------------------------------------------

void MeckHa::begin() {
  load();
  if (paired()) postSoon(60000);   // a first update once WiFi had time to come up
}

void MeckHa::load() {
  Preferences p;
  if (!p.begin(HA_NVS, true)) return;   // nothing stored yet
  p.getString("url", _url, sizeof(_url));
  p.getString("int", _int, sizeof(_int));
  p.getString("ext", _ext, sizeof(_ext));
  p.getString("cloud", _cloud, sizeof(_cloud));
  p.getString("remote", _remote, sizeof(_remote));
  p.getString("hook", _hook, sizeof(_hook));
  p.getString("refresh", _refresh, sizeof(_refresh));
  p.getString("ssid", _ssid, sizeof(_ssid));
  p.end();
}

void MeckHa::save() {
  Preferences p;
  if (!p.begin(HA_NVS, false)) { Serial.println("HA: NVS open failed"); return; }
  p.putString("url", _url);
  p.putString("int", _int);
  p.putString("ext", _ext);
  p.putString("cloud", _cloud);
  p.putString("remote", _remote);
  p.putString("hook", _hook);
  p.putString("refresh", _refresh);
  p.putString("ssid", _ssid);
  p.end();
}

void MeckHa::forget() {
  // Best effort: the device stays in HA until it is deleted there. On WiFi
  // to the address paired with; over 4G to one that answers from outside.
  const char* base = _url;
  if (!meckNetWifi()) {
    base = _ext[0] ? _ext : _remote;
    char host[96];
    haUrlHost(base, host, sizeof(host));
    if (!base[0] || haHostIsLocal(host)) base = "";
  }
  if (_refresh[0] && base[0] && meckNetWaitUp(DATA_APP_HA, 45000)) {
    char url[160], body[256], enc[200];
    snprintf(url, sizeof(url), "%s/auth/revoke", base);
    haUrlEncode(enc, sizeof(enc), _refresh);
    snprintf(body, sizeof(body), "token=%s", enc);
    MeckNetUse use(DATA_APP_HA);
    int code = request("POST", url, "application/x-www-form-urlencoded", body, nullptr, nullptr, 0);
    Serial.printf("HA: token revoked (%d)\n", code);
    meckNetRelease(DATA_APP_HA);
  }
  _url[0] = _int[0] = _ext[0] = _cloud[0] = _remote[0] = _hook[0] = _refresh[0] = _ssid[0] = 0;
  _removed = false;
  _lastPostEpoch = 0;
  _lastResult = 0;
  _lastBase[0] = 0;
  _goodIdx = -1;
  Preferences p;
  if (p.begin(HA_NVS, false)) { p.clear(); p.end(); }
  Serial.println("HA: forgotten");
}

// --- Pairing --------------------------------------------------------------------

uint32_t MeckHa::pairLeftMs() const {
  if (!pairingActive()) return 0;
  uint32_t used = millis() - _pairStart;
  return used >= PAIR_WINDOW_MS ? 0 : PAIR_WINDOW_MS - used;
}

void MeckHa::startPairing() {
  stopPairing();
  _error[0] = 0;
  if (WiFi.status() != WL_CONNECTED || !(WiFi.getMode() & WIFI_STA)) {
    _phase = PH_NO_WIFI;
    return;
  }
  String ip = WiFi.localIP().toString();
  snprintf(_clientId, sizeof(_clientId), "http://%s/", ip.c_str());
  snprintf(_pageUrl, sizeof(_pageUrl), "http://%s/ha", ip.c_str());
  haRandomHex(_state, 16);
  uint8_t rnd[32];
  for (int i = 0; i < 32; i += 4) { uint32_t r = esp_random(); memcpy(rnd + i, &r, 4); }
  haBase64Url(_verifier, sizeof(_verifier), rnd, sizeof(rnd));
  _code[0] = 0;
  _found[0] = 0;
  _mdnsTries = 0;
  _pairStart = millis();
  _phase = PH_SCAN;
  makeQr();
  serverStart();
  mdnsStart();
  awake(true);
  Serial.printf("HA: pairing, page %s\n", _pageUrl);
}

void MeckHa::stopPairing() {
  serverStop();
  mdnsStop();
  clearPairing();
  awake(false);
  _phase = PH_OFF;
}

void MeckHa::clearPairing() {
  if (_access) { memset(_access, 0, strlen(_access)); heap_caps_free(_access); _access = nullptr; }
  memset(_code, 0, sizeof(_code));
  memset(_verifier, 0, sizeof(_verifier));
  _state[0] = 0;
  _workStep = 0;
  _step = "";
}

void MeckHa::fail(const char* why) {
  snprintf(_error, sizeof(_error), "%s", why);
  Serial.printf("HA: pairing failed: %s\n", why);
  serverStop();
  mdnsStop();
  clearPairing();
  awake(false);
  _phase = PH_FAILED;
}

// QR of the page URL, kept as bits: the encoder's handle only lives in its callback
static uint8_t* s_qrBits = nullptr;
static int* s_qrSize = nullptr;
static void haQrCallback(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > MeckHa::QR_MAX) { *s_qrSize = 0; return; }
  memset(s_qrBits, 0, (MeckHa::QR_MAX * MeckHa::QR_MAX + 7) / 8);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) s_qrBits[(y * n + x) >> 3] |= 1 << ((y * n + x) & 7);
  *s_qrSize = n;
}

void MeckHa::makeQr() {
  _qrSize = 0;
  s_qrBits = _qr;
  s_qrSize = &_qrSize;
  esp_qrcode_config_t cfg = { haQrCallback, 5, ESP_QRCODE_ECC_MED };
  if (esp_qrcode_generate(&cfg, _pageUrl) != ESP_OK) _qrSize = 0;
}

bool MeckHa::qrModule(int x, int y) const {
  if (x < 0 || y < 0 || x >= _qrSize || y >= _qrSize) return false;
  int i = y * _qrSize + x;
  return (_qr[i >> 3] >> (i & 7)) & 1;
}

// --- mDNS: Home Assistant announces _home-assistant._tcp with its URLs -------

void MeckHa::mdnsStart() {
  if (_mdnsTries >= 3) return;   // HA answers at once when it is there; leaving waits for a running query
  _mdnsTries++;
  if (!_mdnsUp) {
    if (mdns_init() != ESP_OK) return;
    _mdnsUp = true;
  }
  _mdns = mdns_query_async_new(nullptr, "_home-assistant", "_tcp", MDNS_TYPE_PTR, 3000, 4, nullptr);
}

void MeckHa::mdnsPoll() {
  if (!_mdns) return;
  mdns_result_t* res = nullptr;
  if (!mdns_query_async_get_results((mdns_search_once_t*)_mdns, 0, &res, nullptr)) return;
  for (mdns_result_t* r = res; r && !_found[0]; r = r->next) {
    for (size_t i = 0; i < r->txt_count; i++) {
      const char* k = r->txt[i].key;
      const char* v = r->txt[i].value;
      if (v && (strcmp(k, "internal_url") == 0 || strcmp(k, "base_url") == 0) &&
          haNormalizeBaseUrl(v, _found, sizeof(_found)))
        break;
    }
    if (!_found[0] && r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
      const uint8_t* a = (const uint8_t*)&r->addr->addr.u_addr.ip4.addr;
      snprintf(_found, sizeof(_found), "http://%u.%u.%u.%u:%u", a[0], a[1], a[2], a[3], r->port);
    }
  }
  if (res) mdns_query_results_free(res);
  mdns_query_async_delete((mdns_search_once_t*)_mdns);
  _mdns = nullptr;
  if (_found[0]) Serial.println("HA: found by mDNS");
  else if (_phase == PH_SCAN) mdnsStart();   // ask again while the page is not open yet
}

void MeckHa::mdnsStop() {
  if (_mdns) {
    mdns_result_t* res = nullptr;
    mdns_query_async_get_results((mdns_search_once_t*)_mdns, 3500, &res, nullptr);
    if (res) mdns_query_results_free(res);
    mdns_query_async_delete((mdns_search_once_t*)_mdns);
    _mdns = nullptr;
  }
  if (_mdnsUp) { mdns_free(); _mdnsUp = false; }
}

// --- Web server ------------------------------------------------------------------

#define HA_SRV ((WebServer*)_server)

void MeckHa::serverStart() {
  if (_server) return;
  WebServer* s = new WebServer(80);
  _server = s;
  s->on("/", [this]() { pageRoot(); });
  s->on("/ha", [this]() { pageForm(); });
  s->on("/ha/go", [this]() { pageGo(); });
  s->on("/ha/cb", [this]() { pageCallback(); });
  s->on("/ha/status", [this]() { pageStatus(); });
  s->onNotFound([this]() { HA_SRV->send(404, "text/plain", "Not found"); });
  s->begin();
}

void MeckHa::serverStop() {
  if (!_server) return;
  HA_SRV->stop();
  delete HA_SRV;
  _server = nullptr;
}

void MeckHa::sendPage(int code, const char* title, const char* body, int refreshSecs) {
  String h = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">";
  if (refreshSecs > 0) h += "<meta http-equiv=refresh content=\"" + String(refreshSecs) + ";url=/ha/status\">";
  h += "<title>Meck - Home Assistant</title><style>"
       "body{font-family:system-ui,sans-serif;max-width:30em;margin:1.5em auto;padding:0 1em;line-height:1.4}"
       "input,button{font-size:1.05em;width:100%;padding:.6em;box-sizing:border-box;margin:.3em 0}"
       "small{color:#555}</style></head><body><h2>";
  h += title;
  h += "</h2>";
  h += body;
  h += "</body></html>";
  HA_SRV->sendHeader("Cache-Control", "no-store");
  HA_SRV->send(code, "text/html", h);
}

// The OAuth client id is this address; send people to the form
void MeckHa::pageRoot() {
  HA_SRV->sendHeader("Location", "/ha", true);
  HA_SRV->send(302, "text/plain", "");
}

void MeckHa::pageForm() {
  if (_phase != PH_SCAN && _phase != PH_LOGIN) { pageStatus(); return; }
  const char* prefill = _found[0] ? _found : _url;
  String b = "<p>Home Assistant address, as this phone opens it:</p>"
             "<form action=/ha/go><input name=url autocapitalize=off autocorrect=off spellcheck=false "
             "placeholder=\"http://homeassistant.local:8123\" value=\"";
  b += haHtmlEsc(prefill);
  b += "\"><button>Log in to Home Assistant</button></form>"
       "<p><small>";
  b += _found[0] ? "The deck found this address on the network. " : "";
  b += "After the login, Home Assistant sends this phone back to the deck, so the phone has to be "
       "on the same network as the deck (home WiFi, or the phone's hotspot with the deck on it). "
       "Away from home use the external https address.</small></p>";
  sendPage(200, "Connect the deck to Home Assistant", b.c_str());
}

void MeckHa::pageGo() {
  if (_phase != PH_SCAN && _phase != PH_LOGIN) { pageStatus(); return; }
  char base[128];
  if (!haNormalizeBaseUrl(HA_SRV->arg("url").c_str(), base, sizeof(base))) {
    sendPage(400, "Not an address", "<p>Enter the address as http://host:port or https://host.</p>"
                                    "<p><a href=/ha>Back</a></p>");
    return;
  }
  snprintf(_pairUrl, sizeof(_pairUrl), "%s", base);
  // The challenge is the SHA-256 of the verifier (PKCE S256): a code that
  // leaks on the way back is no use without the verifier, which never
  // leaves the deck
  uint8_t dig[32];
  mbedtls_sha256((const uint8_t*)_verifier, strlen(_verifier), dig, 0);
  char challenge[48], cid[64], redir[80], loc[420];
  haBase64Url(challenge, sizeof(challenge), dig, sizeof(dig));
  char cb[48];
  snprintf(cb, sizeof(cb), "%sha/cb", _clientId);
  haUrlEncode(cid, sizeof(cid), _clientId);
  haUrlEncode(redir, sizeof(redir), cb);
  snprintf(loc, sizeof(loc), "%s/auth/authorize?response_type=code&client_id=%s&redirect_uri=%s&state=%s"
                             "&code_challenge=%s&code_challenge_method=S256",
           _pairUrl, cid, redir, _state, challenge);
  _phase = PH_LOGIN;
  HA_SRV->sendHeader("Location", loc, true);
  HA_SRV->send(302, "text/plain", "");
  Serial.println("HA: phone sent to the login");
}

void MeckHa::pageCallback() {
  if (_phase != PH_LOGIN) { pageStatus(); return; }
  String st = HA_SRV->arg("state"), code = HA_SRV->arg("code");
  if (st != _state || code.length() == 0 || code.length() >= sizeof(_code)) {
    sendPage(400, "Login not accepted", "<p>This login does not belong to the pairing on the deck. "
                                        "Scan the QR code on the deck again.</p>");
    return;
  }
  snprintf(_code, sizeof(_code), "%s", code.c_str());
  _phase = PH_WORKING;
  _workStep = 0;
  _step = "Logging in";
  sendPage(200, "Finishing on the deck", "<p>The deck is registering with Home Assistant...</p>", 2);
}

void MeckHa::pageStatus() {
  switch (_phase) {
    case PH_WORKING: sendPage(200, "Finishing on the deck", (String("<p>") + _step + "...</p>").c_str(), 2); break;
    case PH_DONE:    sendPage(200, "Done", "<p>The deck is now a device in Home Assistant "
                                           "(Settings &gt; Devices &amp; services &gt; Mobile App). "
                                           "You can close this page.</p>"); break;
    case PH_FAILED:  sendPage(200, "Pairing failed", (String("<p>") + haHtmlEsc(_error) +
                                                      "</p><p>Try again from the deck.</p>").c_str()); break;
    default:         sendPage(200, "Pairing is not running", "<p>Start it on the deck: Settings &gt; "
                                                             "Experimental &gt; Home Assistant.</p>"); break;
  }
}

// --- After the login: token, config, registration, sensors ----------------------

void MeckHa::workStep() {
  char* resp = haAlloc(HA_RESP_LEN);
  char* body = haAlloc(HA_BODY_LEN);
  if (!resp || !body) { heap_caps_free(resp); heap_caps_free(body); fail("Out of memory"); return; }
  char url[200];
  int code;
  switch (_workStep) {
    case 0: {   // code -> tokens
      char cid[64], enc[120];
      haUrlEncode(cid, sizeof(cid), _clientId);
      haUrlEncode(enc, sizeof(enc), _code);
      snprintf(body, HA_BODY_LEN, "grant_type=authorization_code&code=%s&client_id=%s&code_verifier=%s",
               enc, cid, _verifier);
      snprintf(url, sizeof(url), "%s/auth/token", _pairUrl);
      code = request("POST", url, "application/x-www-form-urlencoded", body, nullptr, resp, HA_RESP_LEN);
      memset(_code, 0, sizeof(_code));
      if (code != 200) { snprintf(body, 64, code < 0 ? "No connection to HA (%d)" : "Login refused (%d)", code); fail(body); break; }
      _access = haAlloc(1024);
      char refresh[sizeof(_refresh)];
      if (!_access || !haJsonGetStr(resp, "access_token", _access, 1024) ||
          !haJsonGetStr(resp, "refresh_token", refresh, sizeof(refresh))) { fail("No token in the reply"); break; }
      snprintf(_refresh, sizeof(_refresh), "%s", refresh);
      memset(refresh, 0, sizeof(refresh));
      _step = "Reading HA addresses";
      _workStep++;
      break;
    }
    case 1: {   // the addresses HA knows itself by
      snprintf(url, sizeof(url), "%s/api/config", _pairUrl);
      code = request("GET", url, nullptr, nullptr, _access, resp, HA_RESP_LEN);
      char tmp[128];
      _int[0] = _ext[0] = 0;
      if (code == 200) {
        if (haJsonGetStr(resp, "internal_url", tmp, sizeof(tmp))) haNormalizeBaseUrl(tmp, _int, sizeof(_int));
        if (haJsonGetStr(resp, "external_url", tmp, sizeof(tmp))) haNormalizeBaseUrl(tmp, _ext, sizeof(_ext));
      } else {
        Serial.printf("HA: config not read (%d), using the paired address only\n", code);
      }
      _step = "Registering the deck";
      _workStep++;
      break;
    }
    case 2: {   // mobile_app registration
      char name[64], nameEsc[100], devId[20];
      meckHaDeviceName(name, sizeof(name));
      haJsonEsc(nameEsc, sizeof(nameEsc), name);
      snprintf(devId, sizeof(devId), "meck-%012llx", (unsigned long long)ESP.getEfuseMac());
      snprintf(body, HA_BODY_LEN,
               "{\"device_id\":\"%s\",\"app_id\":\"meck\",\"app_name\":\"Meck\",\"app_version\":\"%s\","
               "\"device_name\":\"%s\",\"manufacturer\":\"LilyGO\",\"model\":\"T-Deck MAX\","
               "\"os_name\":\"Meck\",\"os_version\":\"%s\",\"supports_encryption\":false,\"app_data\":{}}",
               devId, FIRMWARE_VERSION, nameEsc, FIRMWARE_VERSION);
      snprintf(url, sizeof(url), "%s/api/mobile_app/registrations", _pairUrl);
      code = request("POST", url, "application/json", body, _access, resp, HA_RESP_LEN);
      if (code != 201 && code != 200) {
        snprintf(body, 64, code < 0 ? "No connection to HA (%d)" : "Registration refused (%d)", code);
        fail(body);
        break;
      }
      if (!haJsonGetStr(resp, "webhook_id", _hook, sizeof(_hook))) { fail("No webhook in the reply"); break; }
      if (!haJsonGetStr(resp, "cloudhook_url", _cloud, sizeof(_cloud))) _cloud[0] = 0;
      char tmp[128];
      _remote[0] = 0;
      if (haJsonGetStr(resp, "remote_ui_url", tmp, sizeof(tmp))) haNormalizeBaseUrl(tmp, _remote, sizeof(_remote));
      snprintf(_url, sizeof(_url), "%s", _pairUrl);
      snprintf(_ssid, sizeof(_ssid), "%s", WiFi.SSID().c_str());
      _removed = false;
      _goodIdx = -1;
      save();
      _step = "Adding the sensors";
      _workStep++;
      break;
    }
    default: {  // one sensor per pass, so the screen keeps up
      int id = _workStep - 3;
      HaSensorValues v;
      meckHaSensors(v);
      if (!registerSensor(id, v)) { fail("Sensors not added"); break; }
      if (++_workStep - 3 >= HA_SENSOR_COUNT) {
        clearPairing();
        _phase = PH_DONE;
        _doneAt = millis();
        _lastPostEpoch = haNowEpoch();
        _lastResult = 201;
        _nextPost = millis() + POST_INTERVAL_MS;
        Serial.println("HA: paired");
      }
      break;
    }
  }
  heap_caps_free(resp);
  heap_caps_free(body);
}

bool MeckHa::registerSensor(int id, const HaSensorValues& v) {
  char body[512], resp[256];
  if (!haBuildRegister(body, sizeof(body), id, v)) return false;
  int code = webhook(body, resp, sizeof(resp));
  return code == 200 || code == 201;
}

// --- Updates ---------------------------------------------------------------------

bool MeckHa::atHome() const {
  if (_forceAway) return false;
  return _ssid[0] && WiFi.status() == WL_CONNECTED && WiFi.SSID() == _ssid;
}

void MeckHa::postSoon(uint32_t delayMs) {
  if (!paired()) return;
  _nextPost = millis() + delayMs;
}

uint32_t MeckHa::nextPostInMs() const {
  if (!paired() || _removed) return 0;
  int32_t d = (int32_t)(_nextPost - millis());
  return d > 0 ? (uint32_t)d : 0;
}

// Sends to the first address that answers; a connection failure moves on to
// the next one, an HTTP answer (even an error) ends it
int MeckHa::webhook(const char* body, char* resp, size_t respLen) {
  const char* bases[5];
  bool home = atHome();
  int n = haWebhookBases(home, _url, _int, _ext, _cloud, _remote, bases, 5);
  if (!meckNetWifi()) n = haDropLocalBases(bases, n);   // over 4G
  String ssid = WiFi.SSID();   // "" over 4G: a network of its own for _goodIdx
  if (strcmp(ssid.c_str(), _goodSsid) != 0) { _goodIdx = -1; snprintf(_goodSsid, sizeof(_goodSsid), "%s", ssid.c_str()); }
  // The address that answered last time first, then the usual order
  int order[5], m = 0;
  if (_goodIdx >= 0 && _goodIdx < n) order[m++] = _goodIdx;
  for (int i = 0; i < n; i++) if (i != _goodIdx) order[m++] = i;
  int code = -1;
  for (int k = 0; k < m; k++) {
    int i = order[k];
    char url[260];
    haWebhookUrl(url, sizeof(url), bases[i], _cloud, _hook);
    code = request("POST", url, "application/json", body, nullptr, resp, respLen);
    if (code > 0) {
      _goodIdx = i;
      // bases[] points into the members, repeats already dropped
      const char* which = bases[i] == _cloud ? "cloudhook" : bases[i] == _remote ? "remote UI"
                        : bases[i] == _int ? "internal" : bases[i] == _ext ? "external" : "paired address";
      snprintf(_lastBase, sizeof(_lastBase), "%s", which);
      return code;
    }
  }
  _goodIdx = -1;
  return code;
}

void MeckHa::post() {
  HaSensorValues v;
  meckHaSensors(v);
  char* body = haAlloc(HA_BODY_LEN);
  char* resp = haAlloc(2048);
  if (!body || !resp) { heap_caps_free(body); heap_caps_free(resp); _nextPost = millis() + RETRY_MS; return; }
  pmHold(true);
  MeckNetUse use(DATA_APP_HA);
  int code = -1;
  if (haBuildUpdate(body, HA_BODY_LEN, v)) code = webhook(body, resp, 2048);
  _lastResult = code;
  if (code == 410) {
    // Deleted in HA: stop sending until it is paired again
    _removed = true;
    Serial.println("HA: registration deleted in Home Assistant");
  } else if (code == 200) {
    _lastPostEpoch = haNowEpoch();
    if (strstr(resp, "not_registered")) _reregister = true;
  }
  if (_reregister && code == 200) {
    _reregister = false;
    for (int id = 0; id < HA_SENSOR_COUNT; id++) registerSensor(id, v);
  }
  pmHold(false);
  _nextPost = millis() + (code == 200 ? POST_INTERVAL_MS : RETRY_MS);
  Serial.printf("HA: update %d via %s%s\n", code, _lastBase[0] ? _lastBase : "-",
                meckNetWifi() ? "" : " (4G)");
  if (_forceAway) { _forceAway = false; _goodIdx = -1; }
  heap_caps_free(body);
  heap_caps_free(resp);
}

void MeckHa::loop() {
  if (_server) HA_SRV->handleClient();
  if (_mdns) mdnsPoll();
  if (_phase == PH_SCAN || _phase == PH_LOGIN) {
    if (pairLeftMs() == 0) fail("Timed out");
    else if (WiFi.status() != WL_CONNECTED) fail("WiFi lost");
  } else if (_phase == PH_WORKING) {
    workStep();
  } else if (_phase == PH_DONE && _server && millis() - _doneAt > 60000) {
    serverStop();   // the phone had its "Done" page
    mdnsStop();
    awake(false);
  }
  if (paired() && !_removed && !pairingActive() && (int32_t)(millis() - _nextPost) >= 0) {
    updateDue();
  } else if (_netWait) {
    meckNetRelease(DATA_APP_HA);   // unpaired or pairing meanwhile
    _netWait = false;
  }
}

// An update is due: on WiFi now; without it over mobile data when HA may
// use it. The link is asked for without blocking, the update goes once it
// is up, and HA lets go of it right after (the same 15 minutes either way).
void MeckHa::updateDue() {
  if (meckNetUp(DATA_APP_HA)) {
    if (!meckNetWifi()) meckNetRequest(DATA_APP_HA);   // hold it through the update
    post();
    if (_netWait || !meckNetWifi()) meckNetRelease(DATA_APP_HA);
    _netWait = false;
    return;
  }
  if (!_netWait) {
    if (!meckNet4gAllowed(DATA_APP_HA)) return;   // waits for WiFi, as before
    // Only home network addresses known: nothing would answer over 4G, so
    // no paid link for it
    const char* bases[5];
    int n = haWebhookBases(false, _url, _int, _ext, _cloud, _remote, bases, 5);
    if (haDropLocalBases(bases, n) == 0) {
      if (!_netNoAddrLogged) Serial.println("HA: no address reachable over 4G, waiting for WiFi");
      _netNoAddrLogged = true;
      return;
    }
    _netNoAddrLogged = false;
    Serial.println("HA: update due, bringing up mobile data");
    meckNetRequest(DATA_APP_HA);
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
    Serial.println("HA: no mobile data, next try later");
    meckNetRelease(DATA_APP_HA);
    _netWait = false;
    _lastResult = -1;
    _nextPost = millis() + RETRY_MS;
  }
}

// No automatic light sleep while the web server waits for the phone or a
// request is on the way: two holders, the pairing and a single request
void MeckHa::awake(bool on) {
  if (on == _pairAwake) return;
  _pairAwake = on;
  pmHold(on);
}

void MeckHa::pmHold(bool on) {
#ifdef CONFIG_PM_ENABLE
  if (!_pmLock && esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meck_ha",
                                     (esp_pm_lock_handle_t*)&_pmLock) != ESP_OK) return;
  // The lock counts acquisitions itself
  if (on) esp_pm_lock_acquire((esp_pm_lock_handle_t)_pmLock);
  else esp_pm_lock_release((esp_pm_lock_handle_t)_pmLock);
#else
  (void)on;
#endif
}

// --- HTTP ------------------------------------------------------------------------

int MeckHa::request(const char* method, const char* url, const char* contentType, const char* body,
                    const char* bearer, char* resp, size_t respLen) {
  if (resp && respLen) resp[0] = 0;
  if (!meckNetUp(DATA_APP_HA)) return -1;
  WiFiClient plain;
  WiFiClientSecure* tls = nullptr;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  bool ok;
  if (haUrlIsHttps(url)) {
    haTlsInPsram();
    tls = new WiFiClientSecure();
    char host[96];
    haUrlHost(url, host, sizeof(host));
    // A LAN-only host has a self-signed certificate as a rule; everything
    // reachable from the internet is checked against the CA bundle
    if (haHostIsLocal(host)) tls->setInsecure();
    else tls->useBuiltinCACertBundle();
    ok = http.begin(*tls, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) { delete tls; return -1; }
  if (contentType) http.addHeader("Content-Type", contentType);
  if (bearer) http.addHeader("Authorization", String("Bearer ") + bearer);
  int code;
  if (strcmp(method, "GET") == 0) code = http.GET();
  else code = http.sendRequest(method, (uint8_t*)body, body ? strlen(body) : 0);
  if (code > 0 && resp && respLen) {
    HaBufStream s(resp, respLen);
    http.writeToStream(&s);
  }
  http.end();
  delete tls;
  return code;
}

// --- Debug CLI -------------------------------------------------------------------

void MeckHa::cmd(const char* arg) {
  static const char* const kPhase[] = { "off", "no-wifi", "scan", "login", "working", "done", "failed" };
  if (strcmp(arg, "") == 0 || strcmp(arg, "status") == 0 || strcmp(arg, "status full") == 0) {
    bool full = strcmp(arg, "status full") == 0;
    char a[16], b[16];
    haMask(a, sizeof(a), _hook);
    haMask(b, sizeof(b), _refresh);
    Serial.printf("HA: %s, phase %s%s%s\n", paired() ? (_removed ? "removed in HA" : "paired") : "not paired",
                  kPhase[_phase], _error[0] ? ", error: " : "", _error);
    Serial.printf("  webhook %s, refresh %s, cloudhook %s, remote %s, at home %s\n", a, b,
                  _cloud[0] ? "yes" : "no", _remote[0] ? "yes" : "no", atHome() ? "yes" : "no");
    {
      const char* bases[5];
      int n = haDropLocalBases(bases, haWebhookBases(false, _url, _int, _ext, _cloud, _remote, bases, 5));
      Serial.printf("  over 4G: %d address(es), 4G %s for HA%s\n", n,
                    meckNet4gAllowed(DATA_APP_HA) ? "allowed" : "not allowed", _netWait ? ", waiting for it" : "");
    }
    if (full) Serial.printf("  url %s\n  internal %s\n  external %s\n", _url, _int, _ext);
    Serial.printf("  last update %d via %s, %lu s ago, next in %lu s\n", _lastResult,
                  _lastBase[0] ? _lastBase : "-",
                  _lastPostEpoch && haNowEpoch() ? (unsigned long)(haNowEpoch() - _lastPostEpoch) : 0UL,
                  (unsigned long)(nextPostInMs() / 1000));
    if (pairingActive()) Serial.printf("  page %s, %lu s left, mDNS %s\n", _pageUrl,
                                       (unsigned long)(pairLeftMs() / 1000), _found[0] ? "found HA" : "nothing");
  } else if (strcmp(arg, "pair") == 0) {
    startPairing();
    if (_phase == PH_NO_WIFI) Serial.println("HA: WiFi is not connected");
  } else if (strcmp(arg, "stop") == 0) {
    stopPairing();
    Serial.println("HA: pairing stopped");
  } else if (strcmp(arg, "post") == 0 || strcmp(arg, "post away") == 0) {
    // "post away": one update in the order used away from the home WiFi,
    // to try the external address from home
    if (!paired()) Serial.println("HA: not paired");
    else { _removed = false; _forceAway = arg[4] != 0; _goodIdx = -1; postSoon(0); }
  } else if (strcmp(arg, "sensors") == 0) {
    HaSensorValues v;
    meckHaSensors(v);
    char* body = haAlloc(HA_BODY_LEN);
    if (body && haBuildUpdate(body, HA_BODY_LEN, v)) Serial.println(body);
    heap_caps_free(body);
  } else if (strcmp(arg, "forget") == 0) {
    stopPairing();
    forget();
  } else {
    Serial.println("ha [status [full] | pair | stop | post [away] | sensors | forget]");
  }
}

#endif
