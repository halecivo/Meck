#pragma once
// =============================================================================
// CalDAV account (docs/caldav-design.md, experimental)
//
// Pairing: the deck shows a QR code with a page on its own small web server.
// The phone that opens it fills in the server (any CalDAV server), the user
// name and the password on its own keyboard; the deck then finds the
// account's calendars and stores the account. The password crosses the home
// WiFi as plain HTTP (the deck has no certificate): use a user made for the
// deck, one that can be revoked on the server.
//
//   OFF --pair--> SCAN --form sent--> WORKING --found--> DONE
//    ^             |  no WiFi: NO_WIFI      |  wrong password / not found: back to
//    +-------------+------- FAILED <-------+  SCAN (5 tries), then FAILED
//
// Discovery (RFC 6764/4791): current-user-principal, calendar-home-set, the
// calendars in it; one step per loop() pass. Only the calendar list is read,
// nothing is written to the server. Credentials are sent to the address the
// user typed in only (iCloud's partition hosts excepted, davSameSite).
//
// The password stays in NVS, never on the SD card, and never in a log.
// WiFi only for now; the sync itself (and mobile data for it) comes later.
// =============================================================================

#if defined(MECK_DAV) && MECK_DAV

#include <stdint.h>
#include "MeckDavUtil.h"

class MeckDav {
public:
  enum Phase : uint8_t { PH_OFF, PH_NO_WIFI, PH_SCAN, PH_WORKING, PH_DONE, PH_FAILED };

  static constexpr uint32_t PAIR_WINDOW_MS = 5UL * 60 * 1000;
  static constexpr int QR_MAX = 37;                 // version 5
  static constexpr int MAX_CALS = 8;
  static constexpr int MAX_TRIES = 5;

  struct Cal {
    char url[200];     // absolute URL of the calendar collection
    char name[40];
  };

  void begin();         // setup(): read NVS
  void loop();          // every loop(): web server, pairing steps

  // --- Pairing (the calendar account screen) ---
  void startPairing();
  void stopPairing();
  Phase phase() const { return _phase; }
  bool pairingActive() const { return _phase == PH_SCAN || _phase == PH_WORKING; }
  const char* pageUrl() const { return _pageUrl; }     // http://<deck>/dav/<token>, what the QR holds
  const char* error() const { return _error; }
  const char* step() const { return _step; }
  uint32_t pairLeftMs() const;
  int qrSize() const { return _qrSize; }
  bool qrModule(int x, int y) const;

  // --- Account ---
  bool configured() const { return _url[0] != 0; }
  const char* serverUrl() const { return _url; }
  const char* user() const { return _user; }
  int calCount() const { return _calCount; }
  const Cal& cal(int i) const { return _cals[i]; }
  void forget();

  void cmd(const char* arg);    // debug CLI "dav ..."

private:
  Phase _phase = PH_OFF;
  // Stored (NVS "meckdav")
  char _url[128] = "";
  char _user[64] = "";
  char _pass[64] = "";
  Cal _cals[MAX_CALS] = {};
  int _calCount = 0;
  // Pairing / discovery
  char _token[33] = "";
  char _pageUrl[56] = "";
  char _statusPath[48] = "";
  char _pairUrl[128] = "";
  char _pairUser[64] = "";
  char _pairPass[64] = "";
  char _principal[200] = "";
  char _home[200] = "";
  Cal _tmpCals[MAX_CALS] = {};
  int _tmpCount = 0;
  char _error[64] = "";
  const char* _step = "";
  uint8_t _workStep = 0;
  uint8_t _tries = 0;
  uint32_t _pairStart = 0, _doneAt = 0;
  uint8_t _qr[(QR_MAX * QR_MAX + 7) / 8] = {};
  int _qrSize = 0;
  void* _server = nullptr;       // WebServer
  void* _pmLock = nullptr;
  bool _awake = false;
  int _psBefore = 0;             // wifi_ps_type_t to put back when pairing ends
  bool _psSaved = false;
  bool _blocking = false;        // a debug CLI discovery: no way back to the form
  char* _resp = nullptr;         // reply buffer in PSRAM, only while a discovery runs

  void load();
  void save();
  void fail(const char* why);
  void retry(const char* why);   // back to the form with a message
  void serverStart();
  void serverStop();
  void makeQr();
  void awake(bool on);
  void workStep();
  void work(const char* url, const char* user, const char* pass);   // start a discovery
  void endDiscovery();

  // One DAV request with the credentials of the discovery: status code, < 0 no
  // connection, -2 refused (the URL is not on the typed-in site). The reply
  // body goes to _resp. A redirect's target goes to `location`.
  int request(const char* method, const char* url, int depth, const char* body,
              char* location = nullptr, size_t locationLen = 0);
  int findCalendars(const char* homeUrl);          // status code; calendars go to _tmpCals
  int propfindPrincipal(const char* url, char* location, size_t locationLen);   // status code; the principal goes to _principal

  // Web server handlers
  void pageForm();
  void pageSubmit();
  void pageStatus();
  void sendPage(int code, const char* title, const char* body, int refreshSecs = 0);
};

extern MeckDav meckDav;

#endif
