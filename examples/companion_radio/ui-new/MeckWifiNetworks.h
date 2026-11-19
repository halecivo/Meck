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
