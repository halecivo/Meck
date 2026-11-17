#pragma once
// =============================================================================
// Departures on the lock screen (docs/transit-widget.md)
//
// Asks a Neujede server (GET /compact/widget) for ready rows: a stop board or
// the trip that fits now, as the server decides. Only while the deck is
// locked (the rows are only shown there): when it locks, then at the reply's
// validUntil, at most every 2 minutes (5 over 4G), with back-off on errors up
// to 15 minutes. Over WiFi when it is connected; else over mobile data when
// it and the app's 4G switch allow it, brought up for the request and let go
// right after. Never over Bluetooth.
//
// The server address and the token are kept in NVS (serial "set transit.url",
// "set transit.token"), never in the firmware or on the SD card.
// =============================================================================

#if defined(MECK_TRANSIT) && MECK_TRANSIT

#include <stdint.h>
#include "MeckTransitUtil.h"

class MeckTransit {
public:
  static constexpr uint32_t NET_WAIT_MS = 90UL * 1000;   // mobile data coming up
  static constexpr size_t RESP_LEN = 4096;               // a reply is <= 2 kB

  void begin();   // setup(): the address and token from NVS

  // Every loop(). enabled = the widget is switched on; locked = the lock
  // screen shows; rows = what the widget may use now (2-4); pos = a live
  // GPS fix (never switched on for this); now = RTC seconds (UTC).
  void loop(bool enabled, bool locked, int rows, bool pos, double lat, double lon, uint32_t now, int32_t offSec);

  bool configured() const { return _url[0] && _token[0]; }
  // A made-up board of the debug CLI is shown even when switched off or
  // not configured (RAM only, nothing in the settings changes)
  bool demoActive() const { return _demo; }
  const TrBoard& board() const { return _board; }
  bool have() const { return _have; }
  bool lastOk() const { return _fails == 0; }
  // Bumped whenever the board changes (a reply, a failure, the CLI)
  uint32_t version() const { return _version; }
  void fetchSoon();

  bool setUrl(const char* url);       // "" forgets it
  bool setToken(const char* token);   // "" forgets it
  void printConfig() const;           // serial "get transit": never the token itself

  void cmd(const char* arg, uint32_t now, int32_t offSec);   // debug CLI "transit ..."

private:
  char _url[96] = "";
  char _token[80] = "";
  TrBoard _board = {};
  bool _have = false;
  uint8_t _fails = 0;            // failed requests in a row
  uint32_t _version = 0;
  bool _due = true;              // a request goes as soon as it may
  uint32_t _next = 0;            // millis() of the next request, when !_due
  uint32_t _lastTryMs = 0;
  bool _tried = false;
  bool _wasLocked = false;
  bool _netWait = false;         // waiting for mobile data to come up
  uint32_t _netWaitSince = 0;
  int _lastResult = 0;           // HTTP code of the last request, -1 no network
  uint32_t _lastBytes = 0;
  uint32_t _lastMs = 0;          // how long the last request kept the deck awake
  bool _demo = false;            // the board is made up by the CLI: no requests
  void* _pmLock = nullptr;

  void schedule(uint32_t inS);
  void due(int rows, bool pos, double lat, double lon, uint32_t now, int32_t offSec);
  bool fetch(int rows, bool pos, double lat, double lon, uint32_t now, int32_t offSec);
  void failed();
  void pmHold(bool on);
  void demo(const char* what, uint32_t now, int32_t offSec);
  void ping(uint32_t now, int32_t offSec);
};

extern MeckTransit meckTransit;

#endif
