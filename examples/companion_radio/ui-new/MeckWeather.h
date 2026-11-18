#pragma once
// =============================================================================
// Weather for the lock screen (docs/weather.md)
//
// Fetches the Open-Meteo forecast for the deck's position (GPS fix, else the
// advert position) about every hour over WiFi, or over mobile data when it
// and the app's own 4G switch allow it, a bit less often there. Never over
// Bluetooth: the phone link carries no internet. The forecast is kept in RAM
// and on the SD card (/weather/forecast.bin), and the lock screen works out
// what to show from it every minute (MeckWeatherUtil.h).
// =============================================================================

#if defined(MECK_WEATHER) && MECK_WEATHER

#include <stdint.h>
#include "MeckWeatherUtil.h"

class MeckWeather {
public:
  static constexpr uint32_t WIFI_INTERVAL_MS = 60UL * 60 * 1000;
  static constexpr uint32_t DATA_INTERVAL_MS = 2UL * 60 * 60 * 1000;   // over 4G
  static constexpr uint32_t RETRY_MS         = 10UL * 60 * 1000;       // after a failed request
  static constexpr uint32_t NET_WAIT_MS      = 90UL * 1000;            // mobile data coming up
  // Moving this far (1/100 degree, ~10 km) asks for the new place at once
  static constexpr int32_t MOVED_100 = 10;

  void begin();   // setup(): the SD copy, if any
  // Every loop(): fetches when one is due. enabled = the lock screen shows
  // weather; pos = the deck knows where it is; now = RTC seconds (UTC).
  void loop(bool enabled, bool pos, double lat, double lon, uint32_t now);

  const WxForecast& forecast() const { return _fc; }
  // Bumped whenever the forecast changes (a request, the debug CLI)
  uint32_t version() const { return _version; }
  void fetchSoon();

  void cmd(const char* arg, uint32_t now, int32_t offSec);   // debug CLI "weather ..."

private:
  WxForecast _fc = {};
  uint32_t _version = 0;
  uint32_t _next = 0;            // millis() of the next request (the first goes at once)
  bool _netWait = false;         // a request waits for mobile data to come up
  uint32_t _netWaitSince = 0;
  int _lastResult = 0;           // HTTP code of the last request, -1 no network
  uint32_t _lastTry = 0;         // RTC seconds
  void* _pmLock = nullptr;

  void due(int32_t lat100, int32_t lon100, uint32_t now);
  bool fetch(int32_t lat100, int32_t lon100, uint32_t now);
  void save() const;
  void load();
  void pmHold(bool on);
};

extern MeckWeather meckWeather;

#endif
