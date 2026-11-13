#pragma once
// =============================================================================
// Internet for the apps (web reader, IRC, Home Assistant): WiFi when it is
// joined, otherwise mobile data when the setting and the app's own 4G switch
// allow it. Whatever works over WiFi has to work over 4G as well; an app
// that wants to spare the user's data plan asks meckNetSaveData().
//
//   if (!meckNetUp(app)) meckNetRequest(app);   // non-blocking; poll meckNetUp()
//   { MeckNetUse use(app); ... the request ... } // counts its 4G bytes to the app
//   meckNetRelease(app);                         // done: the link may go down
// =============================================================================

#include <WiFi.h>
#include "MeckDataUse.h"
#ifdef HAS_4G_MODEM
  #include "ModemManager.h"
#endif

static inline bool meckNetWifi() { return WiFi.status() == WL_CONNECTED; }

// The app can reach the internet right now
static inline bool meckNetUp(uint8_t app) {
  if (meckNetWifi()) return true;
#ifdef MODEM_DATA
  return modemManager.isDataUp() && ModemManager::dataAllowed() && ModemManager::dataAppAllowed(app);
#else
  (void)app;
  return false;
#endif
}

// Mobile data could be brought up for the app (no WiFi needed for this)
static inline bool meckNet4gAllowed(uint8_t app) {
#ifdef MODEM_DATA
  return ModemManager::dataAllowed() && ModemManager::dataAppAllowed(app) && modemManager.isReady();
#else
  (void)app;
  return false;
#endif
}

// The app's traffic goes over mobile data: keep it small. For now that is
// simply "no WiFi, on 4G".
static inline bool meckNetSaveData() {
#ifdef MODEM_DATA
  return !meckNetWifi() && modemManager.isDataUp();
#else
  return false;
#endif
}

// No WiFi: ask for mobile data for the app (returns at once), or keep the
// link it already has from the idle timeout. Nothing to do on WiFi.
static inline void meckNetRequest(uint8_t app) {
#ifdef MODEM_DATA
  if (meckNetWifi()) return;
  modemManager.requestData(app);
  modemManager.restoreDataDns();   // WiFi may have changed the name server
#else
  (void)app;
#endif
}

static inline void meckNetRelease(uint8_t app) {
#ifdef MODEM_DATA
  modemManager.releaseData(app);
#else
  (void)app;
#endif
}

// Blocking version for a user action (main loop stops meanwhile): true once
// the app has a network, false when 4G is not allowed or did not come up
static inline bool meckNetWaitUp(uint8_t app, uint32_t timeoutMs) {
  if (meckNetUp(app)) { meckNetRequest(app); return true; }
#ifdef MODEM_DATA
  if (!meckNet4gAllowed(app)) return false;
  meckNetRequest(app);
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (meckNetUp(app)) { modemManager.restoreDataDns(); return true; }
    // FAILED stays until the next request, so only after it left OFF
    if (modemManager.dataFailed() && millis() - start > 1000) break;
    delay(200);
  }
  meckNetRelease(app);
#endif
  return false;
}

// Counts the 4G traffic of one request to the app (main loop, scoped)
class MeckNetUse {
public:
  explicit MeckNetUse(uint8_t app) {
#ifdef MODEM_DATA
    _prev = modemManager.dataTag();
    modemManager.setDataTag(app);
#else
    (void)app;
#endif
  }
  ~MeckNetUse() {
#ifdef MODEM_DATA
    modemManager.setDataTag(_prev);
#endif
  }
private:
  uint8_t _prev = DATA_APP_NONE;
};
