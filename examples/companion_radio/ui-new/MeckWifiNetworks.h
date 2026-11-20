#pragma once

// Saved WiFi networks, kept in /web/wifi.cfg as network name / password
// line pairs, the most recently joined first. Older firmware wrote one pair
// and the Web Reader reads the first one, so both keep working with the list.

#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#define MECK_WIFI_CFG        "/web/wifi.cfg"
#define MECK_WIFI_MAX_SAVED  8

struct MeckWifiNet {
  char ssid[33];   // upstream's wifi_ssid / wifi_pwd sizes
  char pass[64];
};

// Fills nets (up to max) and returns how many networks are saved
inline int meckWifiLoad(MeckWifiNet* nets, int max) {
  int n = 0;
  if (SD.exists(MECK_WIFI_CFG)) {
    File f = SD.open(MECK_WIFI_CFG, FILE_READ);
    if (f) {
      while (n < max && f.available()) {
        String s = f.readStringUntil('\n'); s.trim();
        String p = f.readStringUntil('\n'); p.trim();
        if (s.length() == 0) continue;   // cleared entry ("set wifi.clear")
        int k = 0;
        while (k < n && strcmp(nets[k].ssid, s.c_str()) != 0) k++;
        if (k < n) continue;             // listed twice: the first one counts
        snprintf(nets[n].ssid, sizeof(nets[n].ssid), "%s", s.c_str());
        snprintf(nets[n].pass, sizeof(nets[n].pass), "%s", p.c_str());
        n++;
      }
      f.close();
    }
  }
  digitalWrite(SDCARD_CS, HIGH);
  return n;
}

inline bool meckWifiStore(const MeckWifiNet* nets, int n) {
  bool ok = false;
  if (SD.exists("/web") || SD.mkdir("/web")) {
    File f = SD.open(MECK_WIFI_CFG, FILE_WRITE);
    if (f) {
      for (int i = 0; i < n; i++) {
        f.println(nets[i].ssid);
        f.println(nets[i].pass);
      }
      f.close();
      ok = true;
    }
  }
  digitalWrite(SDCARD_CS, HIGH);
  return ok;
}

// Saves a network as the most recent one; the oldest goes when the list is full
inline bool meckWifiRemember(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return false;
  MeckWifiNet nets[MECK_WIFI_MAX_SAVED];
  int n = meckWifiLoad(nets, MECK_WIFI_MAX_SAVED);
  int k = 0;
  while (k < n && strcmp(nets[k].ssid, ssid) != 0) k++;
  if (k == n) k = (n < MECK_WIFI_MAX_SAVED) ? n++ : n - 1;
  for (int i = k; i > 0; i--) nets[i] = nets[i - 1];
  snprintf(nets[0].ssid, sizeof(nets[0].ssid), "%s", ssid);
  snprintf(nets[0].pass, sizeof(nets[0].pass), "%s", pass ? pass : "");
  return meckWifiStore(nets, n);
}

inline bool meckWifiForget(const char* ssid) {
  MeckWifiNet nets[MECK_WIFI_MAX_SAVED];
  int n = meckWifiLoad(nets, MECK_WIFI_MAX_SAVED);
  int k = 0;
  while (k < n && strcmp(nets[k].ssid, ssid) != 0) k++;
  if (k == n) return false;
  for (int i = k; i < n - 1; i++) nets[i] = nets[i + 1];
  return meckWifiStore(nets, n - 1);
}

// The saved password for a network, or false when it is not saved
inline bool meckWifiFindSaved(const char* ssid, char* pass, size_t passLen) {
  MeckWifiNet nets[MECK_WIFI_MAX_SAVED];
  int n = meckWifiLoad(nets, MECK_WIFI_MAX_SAVED);
  for (int i = 0; i < n; i++) {
    if (strcmp(nets[i].ssid, ssid) == 0) {
      snprintf(pass, passLen, "%s", nets[i].pass);
      return true;
    }
  }
  return false;
}

// After a finished scan of 'found' results: the saved network heard
// strongest, or -1 when none is in range
inline int meckWifiBestInScan(const MeckWifiNet* nets, int n, int found) {
  int best = -1, bestRssi = -1000;
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    for (int k = 0; k < n; k++) {
      if (s == nets[k].ssid && rssi > bestRssi) { best = k; bestRssi = rssi; }
    }
  }
  return best;
}

// -----------------------------------------------------------------------------
// Where each saved network is (docs/position.md): learned from GPS fixes
// while joined to it, and given as the deck's position while joined again.
// Kept apart from wifi.cfg, so the pairs there stay as they were, in
// /web/wifiloc.cfg, one line per network: "<flags> <lat> <lon> <fixes> <name>".
// -----------------------------------------------------------------------------

#define MECK_WIFI_LOC_CFG   "/web/wifiloc.cfg"
#define MECK_WIFI_LOC_MAX   16

enum : uint8_t {
  WLOC_OFF   = 1,   // the user turned the location off for this network
  WLOC_MOVED = 2,   // turned off by itself: a GPS fix far from it (a hotspot)
};

struct MeckWifiLoc {
  char ssid[33];
  uint8_t flags;
  double lat, lon;
  uint16_t fixes;   // GPS fixes averaged into it; 0 = no location
};

inline int meckWifiLocLoad(MeckWifiLoc* locs, int max) {
  int n = 0;
  if (SD.exists(MECK_WIFI_LOC_CFG)) {
    File f = SD.open(MECK_WIFI_LOC_CFG, FILE_READ);
    if (f) {
      while (n < max && f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        unsigned flags = 0, fixes = 0;
        double lat = 0, lon = 0;
        int name = 0;
        if (sscanf(line.c_str(), "%u %lf %lf %u %n", &flags, &lat, &lon, &fixes, &name) < 4 || !name) continue;
        if (!line.c_str()[name]) continue;
        MeckWifiLoc& l = locs[n++];
        snprintf(l.ssid, sizeof(l.ssid), "%s", line.c_str() + name);
        l.flags = (uint8_t)flags;
        l.lat = lat;
        l.lon = lon;
        l.fixes = (uint16_t)fixes;
      }
      f.close();
    }
  }
  digitalWrite(SDCARD_CS, HIGH);
  return n;
}

inline bool meckWifiLocStore(const MeckWifiLoc* locs, int n) {
  bool ok = false;
  if (SD.exists("/web") || SD.mkdir("/web")) {
    File f = SD.open(MECK_WIFI_LOC_CFG, FILE_WRITE);
    if (f) {
      for (int i = 0; i < n; i++) {
        if (!locs[i].fixes && !locs[i].flags) continue;   // nothing to keep
        f.printf("%u %.6f %.6f %u %s\n", (unsigned)locs[i].flags, locs[i].lat, locs[i].lon,
                 (unsigned)locs[i].fixes, locs[i].ssid);
      }
      f.close();
      ok = true;
    }
  }
  digitalWrite(SDCARD_CS, HIGH);
  return ok;
}

// The network's entry, or one with no location and no flags when it has none
inline MeckWifiLoc meckWifiLocGet(const char* ssid) {
  MeckWifiLoc locs[MECK_WIFI_LOC_MAX];
  int n = meckWifiLocLoad(locs, MECK_WIFI_LOC_MAX);
  for (int i = 0; i < n; i++)
    if (strcmp(locs[i].ssid, ssid) == 0) return locs[i];
  MeckWifiLoc none = {};
  snprintf(none.ssid, sizeof(none.ssid), "%s", ssid);
  return none;
}

// Saves the entry (an empty one removes the line); the first in the file
// goes when it is full
inline bool meckWifiLocPut(const MeckWifiLoc& loc) {
  MeckWifiLoc locs[MECK_WIFI_LOC_MAX];
  int n = meckWifiLocLoad(locs, MECK_WIFI_LOC_MAX);
  int k = 0;
  while (k < n && strcmp(locs[k].ssid, loc.ssid) != 0) k++;
  if (k == n) {
    if (n == MECK_WIFI_LOC_MAX) {
      for (int i = 0; i < n - 1; i++) locs[i] = locs[i + 1];
      k = n - 1;
    } else {
      n++;
    }
  }
  locs[k] = loc;
  return meckWifiLocStore(locs, n);
}

// A forgotten network takes its location with it
inline void meckWifiLocDrop(const char* ssid) {
  MeckWifiLoc none = {};
  snprintf(none.ssid, sizeof(none.ssid), "%s", ssid);
  meckWifiLocPut(none);
}
