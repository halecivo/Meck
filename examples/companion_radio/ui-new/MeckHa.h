#pragma once
// =============================================================================
// Home Assistant link (experimental, docs/home-assistant.md)
//
// Pairing: the deck shows a QR code with a page on its own small web server.
// The phone that opens it is sent to Home Assistant's login, and Home
// Assistant sends the browser back to the deck with a one-time code (HA
// accepts a LAN address as the OAuth client). The deck trades the code for
// a token, registers itself as a mobile_app device and its sensors.
//
// After that only the webhook is used: every 15 minutes, and when the
// charger is plugged in or out, the sensor states go to HA over WiFi, or
// with no WiFi over mobile data when HA may use it (MeckNet.h; the same
// interval, the link is brought up for the update and let go after it). At
// home the internal address is tried first, elsewhere Nabu Casa's cloudhook
// or the external https address; over 4G only the addresses from outside.
// Pairing needs WiFi: the phone opens the deck's page on the home network.
//
//   OFF --pair--> SCAN --page opened, login--> LOGIN --code back--> WORKING --> DONE
//    ^             |  no WiFi: NO_WIFI           |                     |
//    +-------------+-----------------------------+------ FAILED <------+
//
// Secrets (refresh token, webhook id) stay in NVS, never on the SD card.
// =============================================================================

#if defined(MECK_HA) && MECK_HA

#include <stdint.h>
#include "MeckHaUtil.h"

// main.cpp: what the deck reports, and the name it registers with
extern void meckHaSensors(HaSensorValues& v);
extern void meckHaDeviceName(char* out, size_t n);

class MeckHa {
public:
  enum Phase : uint8_t { PH_OFF, PH_NO_WIFI, PH_SCAN, PH_LOGIN, PH_WORKING, PH_DONE, PH_FAILED };

  static constexpr uint32_t PAIR_WINDOW_MS   = 5UL * 60 * 1000;   // the page and the login
  static constexpr uint32_t POST_INTERVAL_MS = 15UL * 60 * 1000;
  static constexpr uint32_t RETRY_MS         = 2UL * 60 * 1000;   // after a failed update
  static constexpr uint32_t NET_WAIT_MS      = 90UL * 1000;       // mobile data coming up
  static constexpr int QR_MAX = 37;                                // version 5

  void begin();                 // setup(): read NVS
  void loop();                  // every loop(): web server, pairing steps, updates

  // --- Pairing (the HA screen) ---
  void startPairing();
  void stopPairing();           // leaving the screen
  Phase phase() const { return _phase; }
  bool pairingActive() const { return _phase == PH_SCAN || _phase == PH_LOGIN || _phase == PH_WORKING; }
  const char* pageUrl() const { return _pageUrl; }      // http://<deck>/ha, what the QR holds
  const char* foundUrl() const { return _found; }       // found by mDNS, "" = none
  const char* error() const { return _error; }
  const char* step() const { return _step; }            // what WORKING is doing
  uint32_t pairLeftMs() const;
  int qrSize() const { return _qrSize; }
  bool qrModule(int x, int y) const;

  // --- Link ---
  bool paired() const { return _hook[0] != 0; }
  bool removed() const { return _removed; }             // HA deleted the registration (410)
  const char* haUrl() const { return _url; }
  bool atHome() const;
  void postSoon(uint32_t delayMs = 0);
  void forget();                                        // revoke the token, clear NVS
  uint32_t lastPostEpoch() const { return _lastPostEpoch; }
  int lastResult() const { return _lastResult; }        // HTTP code, < 0 no connection, 0 none yet
  const char* lastBase() const { return _lastBase; }    // where the last update went
  uint32_t nextPostInMs() const;

  void cmd(const char* arg);    // debug CLI "ha ..."

private:
  Phase _phase = PH_OFF;
  // Stored (NVS "meckha")
  char _url[128] = "";          // the address paired with
  char _int[128] = "";          // HA's internal_url
  char _ext[128] = "";          // HA's external_url
  char _cloud[192] = "";        // Nabu Casa cloudhook (full webhook URL)
  char _remote[128] = "";       // Nabu Casa remote UI
  char _hook[80] = "";          // webhook id
  char _refresh[192] = "";      // refresh token (revoke on forget; later the WebSocket)
  char _ssid[33] = "";          // home WiFi: the internal address is tried first there
  // Pairing
  char _pageUrl[48] = "";
  char _clientId[40] = "";      // http://<deck ip>/
  char _found[128] = "";
  char _pairUrl[128] = "";      // what the phone sent to
  char _state[33] = "";
  char _verifier[64] = "";
  char _code[96] = "";
  char _error[64] = "";
  const char* _step = "";
  uint8_t _workStep = 0;
  uint32_t _pairStart = 0, _doneAt = 0;
  uint8_t _qr[(QR_MAX * QR_MAX + 7) / 8] = {};
  int _qrSize = 0;
  void* _server = nullptr;      // WebServer
  void* _mdns = nullptr;        // mdns_search_once_t
  bool _mdnsUp = false;
  uint8_t _mdnsTries = 0;
  char* _access = nullptr;      // access token, PSRAM, only while pairing
  // Updates
  uint32_t _nextPost = 0;
  bool _removed = false;
  bool _reregister = false;
  uint32_t _lastPostEpoch = 0;
  int _lastResult = 0;
  char _lastBase[24] = "";
  int8_t _goodIdx = -1;         // the address that worked, until the network changes
  char _goodSsid[33] = "";
  bool _forceAway = false;      // debug "ha post away"
  bool _netWait = false;        // an update waits for mobile data to come up
  uint32_t _netWaitSince = 0;
  bool _netNoAddrLogged = false;   // said once: no address to use over 4G
  void* _pmLock = nullptr;
  bool _pairAwake = false;

  void load();
  void save();
  void clearPairing();
  void fail(const char* why);
  void serverStart();
  void serverStop();
  void mdnsStart();
  void mdnsPoll();
  void mdnsStop();
  void makeQr();
  void workStep();
  void updateDue();
  void post();
  bool registerSensor(int id, const HaSensorValues& v);
  int webhook(const char* body, char* resp, size_t respLen);
  void awake(bool on);          // held through the pairing
  void pmHold(bool on);         // held through one update

  // HTTP(S) request; returns the status code (< 0: no connection) and up
  // to respLen - 1 bytes of the body
  int request(const char* method, const char* url, const char* contentType, const char* body,
              const char* bearer, char* resp, size_t respLen);

  // Web server handlers
  void pageForm();
  void pageGo();
  void pageCallback();
  void pageStatus();
  void pageRoot();
  void sendPage(int code, const char* title, const char* body, int refreshSecs = 0);
};

extern MeckHa meckHa;

#endif
