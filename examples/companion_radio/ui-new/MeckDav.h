#pragma once
// =============================================================================
// CalDAV / CardDAV account (docs/caldav-design.md, experimental)
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
// Discovery (RFC 6764/4791/6352): current-user-principal, calendar-home-set
// and addressbook-home-set, the calendars and address books in them; one step
// per loop() pass.
//
// Contacts (docs/calendar-account.md): the address books are read after the
// pairing, after boot and every 6 hours, on WiFi while the deck is unlocked;
// only when their ctag / sync-token changed are the cards fetched. The phone
// numbers become the synced part of smsContacts (/sms/dav-contacts.txt).
// Nothing is ever written to the server. Credentials are sent to the address the
// user typed in only (iCloud's partition hosts excepted, davSameSite).
//
// Events (the lock screen agenda, MeckAgenda): the next days of every
// calendar, recurring events expanded by the server, every hour and after
// local midnight, on WiFi while the deck is unlocked. Read only too.
//
// The password stays in NVS, never on the SD card, and never in a log.
// WiFi only for now; mobile data for it comes later.
// =============================================================================

#if defined(MECK_DAV) && MECK_DAV

#include <stdint.h>
#include "MeckDavUtil.h"

struct SMSContact;   // SMSContacts.h

class MeckDav {
public:
  enum Phase : uint8_t { PH_OFF, PH_NO_WIFI, PH_SCAN, PH_WORKING, PH_DONE, PH_FAILED };

  static constexpr uint32_t PAIR_WINDOW_MS = 5UL * 60 * 1000;
  static constexpr int QR_MAX = 37;                 // version 5
  static constexpr int MAX_CALS = 8;
  static constexpr int MAX_TRIES = 5;
  static constexpr int MAX_BOOKS = 4;

  struct Cal {
    char url[200];     // absolute URL of the calendar collection
    char name[40];
  };

  void begin();         // setup(): read NVS
  void loop(bool locked);   // every loop(): web server, pairing steps, contacts sync

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
  int bookCount() const { return _bookCount; }
  const Cal& book(int i) const { return _books[i]; }
  void forget();

  // --- Contacts sync ---
  void syncNow();                                   // at the next loop() pass
  bool syncing() const { return _sy != SY_IDLE; }
  bool syncOk() const { return _syncOk; }
  uint32_t syncAgoMs() const;                       // since the last sync, 0: none yet
  const char* syncError() const { return _syncErr; }
  void syncAdd(const char* name, const char* phone);   // vCard reader callback

  // --- Events sync (the agenda) ---
  void setClock(uint32_t now, int32_t utcOffsetSec) { _now = now; _utcOff = utcOffsetSec; }
  int32_t utcOffset() const { return _utcOff; }
  void eventsNow() { if (configured() && _calCount) _evWanted = true; }
  bool eventsSyncing() const { return _ev != EV_IDLE; }
  void eventAdd(const DavEvent& e);                 // iCalendar reader callback

  void cmd(const char* arg);    // debug CLI "dav ..."
  static constexpr int EV_BUF = 256;                // events of one sync, before the agenda keeps the earliest

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
  char _abHome[200] = "";         // when the contacts live apart from the calendars
  Cal _tmpCals[MAX_CALS] = {};
  int _tmpCount = 0;
  Cal _tmpBooks[MAX_BOOKS] = {};
  int _tmpBookCount = 0;
  // Address books (NVS too) and the state each was last synced at
  Cal _books[MAX_BOOKS] = {};
  int _bookCount = 0;
  char _bookTokens[MAX_BOOKS][120] = {};
  char _error[64] = "";
  const char* _step = "";
  uint8_t _workStep = 0;
  uint8_t _route = 0;            // discovery: 0 not yet, 1 typed path tried, 2 by the principal
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
  char* _resp = nullptr;         // reply buffer in PSRAM, only while a discovery or sync runs
  size_t _respCap = 0;
  // Contacts sync
  enum SyncPhase : uint8_t { SY_IDLE, SY_STATE, SY_LIST, SY_FETCH, SY_NEXT };
  static constexpr size_t DAV_BODY_LEN = 8192;   // one multiget request
  SyncPhase _sy = SY_IDLE;
  bool _syncWanted = false, _syncOk = false, _syChanged = false, _syForce = false;
  uint32_t _syncAt = 0;          // millis() of the last finished sync, 0 = none since boot
  char _syncErr[64] = "";
  char _syTokens[MAX_BOOKS][120] = {};
  int _syBook = 0, _syCards = 0, _syNext = 0, _syN = 0, _syDropped = 0;
  SMSContact* _syList = nullptr;          // the new list (PSRAM)
  char* _syPool = nullptr;                // card paths of one book (PSRAM)
  uint32_t* _syOff = nullptr;
  int32_t* _syLen = nullptr;
  char* _syBody = nullptr;                // multiget request body (PSRAM)
  // Events sync
  enum EvPhase : uint8_t { EV_IDLE, EV_CAL };
  EvPhase _ev = EV_IDLE;
  bool _evWanted = false, _evOk = false;
  uint32_t _evAt = 0;            // millis() of the last finished events sync, 0 = none since boot
  uint32_t _evDay = 0;           // local midnight the last good one was for
  uint32_t _evWin = 0;           // window start of the one running
  char _evErr[64] = "";
  int _evCal = 0, _evN = 0, _evDropped = 0;
  DavEvent* _evList = nullptr;   // PSRAM, EV_BUF
  char* _evBody = nullptr;       // calendar-query body
  uint32_t _now = 0;             // RTC time and local offset, from main.cpp every loop()
  int32_t _utcOff = 0;

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
  int requestOnce(const char* method, const char* url, int depth, const char* body,
                  char* location, size_t locationLen);
  int findCalendars(const char* homeUrl);          // status code; calendars and books go to _tmpCals / _tmpBooks
  int wellKnown(const char* which);                // "caldav" / "carddav": the principal from /.well-known
  bool syncDue() const;
  void syncTick(bool locked);
  void syncStart();
  void syncStep();
  void syncEnd();
  void syncFail(const char* why);
  bool eventsDue() const;
  void eventsTick(bool locked);
  void eventsStart();
  void eventsStep();
  void eventsEnd();
  void eventsFail(const char* why);
  void eventsPrint() const;
  int propfindPrincipal(const char* url, char* location, size_t locationLen);   // status code; the principal goes to _principal

  // Web server handlers
  void pageForm();
  void pageSubmit();
  void pageStatus();
  void sendPage(int code, const char* title, const char* body, int refreshSecs = 0);
};

MeckDav& meckDavGet();     // the one instance, in PSRAM (MeckDav.cpp)
#define meckDav (meckDavGet())

#endif
