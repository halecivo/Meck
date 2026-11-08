#pragma once
// =============================================================================
// Home Assistant link (docs/home-assistant.md) -- the parts without hardware
//
// Small JSON and URL helpers and the mobile_app sensor payloads. The replies
// the deck reads (token, registration, config) are flat objects with a few
// string fields, so a key search does instead of a JSON library. No Arduino
// code here, so test/ha/test_ha.cpp runs it on a PC.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// --- JSON --------------------------------------------------------------------

// Value of the first "key": "<string>" in json, unescaped. False when the key
// is missing, its value is not a string (null included) or does not fit.
static inline bool haJsonGetStr(const char* json, const char* key, char* out, size_t n) {
  if (!json || !key || !out || n == 0) return false;
  out[0] = 0;
  size_t kl = strlen(key);
  for (const char* p = strchr(json, '"'); p; p = strchr(p + 1, '"')) {
    if (strncmp(p + 1, key, kl) != 0 || p[1 + kl] != '"') continue;
    const char* v = p + kl + 2;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
    if (*v != ':') continue;                 // the key text was a value
    v++;
    while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') v++;
    if (*v != '"') return false;
    v++;
    size_t o = 0;
    for (; *v && *v != '"'; v++) {
      char ch = *v;
      if (ch == '\\') {
        v++;
        switch (*v) {
          case 'n': ch = '\n'; break;
          case 't': ch = '\t'; break;
          case 'r': ch = '\r'; break;
          case 'b': ch = '\b'; break;
          case 'f': ch = '\f'; break;
          case 'u':                            // the fields read here are ASCII
            for (int i = 0; i < 4 && v[1]; i++) v++;
            ch = '?';
            break;
          case 0: return false;
          default: ch = *v; break;             // \" \\ \/
        }
      }
      if (o + 1 >= n) { out[0] = 0; return false; }
      out[o++] = ch;
    }
    if (*v != '"') { out[0] = 0; return false; }
    out[o] = 0;
    return true;
  }
  return false;
}

// s as the inside of a JSON string; returns the length written (truncated
// to fit, never cutting an escape in half)
static inline size_t haJsonEsc(char* out, size_t n, const char* s) {
  if (n == 0) return 0;
  size_t o = 0;
  for (; s && *s; s++) {
    unsigned char ch = (unsigned char)*s;
    char esc[8];
    if (ch == '"' || ch == '\\') { esc[0] = '\\'; esc[1] = (char)ch; esc[2] = 0; }
    else if (ch < 0x20) snprintf(esc, sizeof(esc), "\\u%04x", ch);
    else { esc[0] = (char)ch; esc[1] = 0; }
    size_t l = strlen(esc);
    if (o + l + 1 > n) break;
    memcpy(out + o, esc, l);
    o += l;
  }
  out[o] = 0;
  return o;
}

// --- URLs ----------------------------------------------------------------------

// Percent-encodes s for a query value; returns false when it does not fit
static inline bool haUrlEncode(char* out, size_t n, const char* s) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      if (o + 2 > n) { if (n) out[0] = 0; return false; }
      out[o++] = (char)ch;
    } else {
      if (o + 4 > n) { if (n) out[0] = 0; return false; }
      out[o++] = '%'; out[o++] = hex[ch >> 4]; out[o++] = hex[ch & 15];
    }
  }
  if (o + 1 > n) { if (n) out[0] = 0; return false; }
  out[o] = 0;
  return true;
}

// Turns what someone typed into a base URL: trims spaces and trailing
// slashes, adds http:// when no scheme was given, drops a path such as
// "/lovelace/0". False for an empty or non-http(s) address.
static inline bool haNormalizeBaseUrl(const char* in, char* out, size_t n) {
  if (!in || n < 12) return false;
  while (*in == ' ' || *in == '\t') in++;
  char tmp[160];
  size_t l = strlen(in);
  while (l && (in[l - 1] == ' ' || in[l - 1] == '\t' || in[l - 1] == '\r' || in[l - 1] == '\n')) l--;
  if (l == 0 || l >= sizeof(tmp)) return false;
  memcpy(tmp, in, l);
  tmp[l] = 0;
  const char* scheme = "http://";
  const char* rest = tmp;
  if (strncmp(tmp, "http://", 7) == 0) { rest = tmp + 7; }
  else if (strncmp(tmp, "https://", 8) == 0) { scheme = "https://"; rest = tmp + 8; }
  else if (strstr(tmp, "://")) return false;
  size_t hl = strcspn(rest, "/?#");
  if (hl == 0) return false;
  for (size_t i = 0; i < hl; i++) {
    unsigned char ch = (unsigned char)rest[i];
    if (ch <= ' ' || ch == '@' || ch == '\\') return false;
  }
  if (strlen(scheme) + hl + 1 > n) return false;
  snprintf(out, n, "%s%.*s", scheme, (int)hl, rest);
  return true;
}

static inline bool haUrlIsHttps(const char* url) { return url && strncmp(url, "https://", 8) == 0; }

// Host of a base URL ("http://ha.local:8123" -> "ha.local")
static inline void haUrlHost(const char* url, char* out, size_t n) {
  if (n == 0) return;
  out[0] = 0;
  const char* p = strstr(url ? url : "", "://");
  if (!p) return;
  p += 3;
  size_t hl = strcspn(p, ":/?#");
  if (hl >= n) hl = n - 1;
  memcpy(out, p, hl);
  out[hl] = 0;
}

// A host that only exists inside a home network: a private or link-local
// IPv4 address, or an mDNS ".local" name. HTTPS to such a host usually has
// a self-signed certificate; it is the only place the deck accepts one.
static inline bool haHostIsLocal(const char* host) {
  size_t l = strlen(host);
  if (l > 6 && strcmp(host + l - 6, ".local") == 0) return true;
  unsigned a, b, c, d;
  char tail;
  if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
  if (a > 255 || b > 255 || c > 255 || d > 255) return false;
  return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
         (a == 169 && b == 254) || a == 127;
}

// Where the webhook goes, in the order to try. At home the address the deck
// paired with and HA's internal URL come first (no trip through the
// internet, no TLS); elsewhere Nabu Casa's cloudhook, the remote UI and the
// external URL, the local ones last. Empty entries and repeats are left out.
// Returns how many of the n slots of `out` were filled.
static inline int haWebhookBases(bool atHome, const char* paired, const char* internal,
                                 const char* external, const char* cloudhook, const char* remoteUi,
                                 const char** out, int n) {
  const char* homeOrder[5] = { internal, paired, cloudhook, remoteUi, external };
  const char* awayOrder[5] = { cloudhook, remoteUi, external, paired, internal };
  const char** order = atHome ? homeOrder : awayOrder;
  int k = 0;
  for (int i = 0; i < 5 && k < n; i++) {
    const char* u = order[i];
    if (!u || !u[0]) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (strcmp(out[j], u) == 0) dup = true;
    if (!dup) out[k++] = u;
  }
  return k;
}

// The cloudhook is a full webhook URL; the others are base URLs that get
// "/api/webhook/<id>"
static inline void haWebhookUrl(char* out, size_t n, const char* base, const char* cloudhook,
                                const char* webhookId) {
  if (cloudhook && cloudhook[0] && strcmp(base, cloudhook) == 0) snprintf(out, n, "%s", base);
  else snprintf(out, n, "%s/api/webhook/%s", base, webhookId);
}

// "abcd…" for logs: enough to tell two secrets apart, not enough to use one
static inline void haMask(char* out, size_t n, const char* secret) {
  if (!secret || !secret[0]) snprintf(out, n, "(none)");
  else snprintf(out, n, "%.4s\xE2\x80\xA6", secret);
}

// --- Base64url (PKCE) -----------------------------------------------------------

static inline size_t haBase64Url(char* out, size_t n, const uint8_t* in, size_t len) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    int chars = (len - i >= 3) ? 4 : (int)(len - i) + 1;
    for (int k = 0; k < chars; k++) {
      if (o + 1 >= n) { out[o] = 0; return o; }
      out[o++] = tbl[(v >> (18 - 6 * k)) & 63];
    }
  }
  out[o] = 0;
  return o;
}

// --- Sensors -------------------------------------------------------------------

// What the deck reports; filled by main.cpp
struct HaSensorValues {
  int battPct;            // usable percent, -1 = unknown
  int battMv;             // battery voltage, 0 = unknown
  uint8_t chargeState;    // charger: 0 none, 1 pre-charge, 2 fast charge, 3 done
  bool usb;               // USB power present
  int timeLeftMin;        // minutes to the shutdown, -1 = unknown or charging
  int contacts;           // mesh contacts
  int unread;             // unread mesh messages
  uint32_t rxPackets;     // LoRa packets received since boot
  uint32_t lastRxEpoch;   // UTC time of the last LoRa packet, 0 = none / no clock
};

enum HaSensorId : uint8_t {
  HA_S_BATT_LEVEL, HA_S_BATT_STATE, HA_S_CHARGER, HA_S_BATT_VOLTAGE, HA_S_TIME_LEFT,
  HA_S_CONTACTS, HA_S_UNREAD, HA_S_RX_PACKETS, HA_S_LAST_RX,
  HA_SENSOR_COUNT
};

struct HaSensorDef {
  const char* uniqueId;
  const char* name;
  bool binary;
  const char* deviceClass;   // nullptr = none
  const char* unit;
  const char* stateClass;
  bool diagnostic;
  const char* icon;          // nullptr = computed (battery)
};

static const HaSensorDef kHaSensors[HA_SENSOR_COUNT] = {
  { "battery_level",     "Battery level",      false, "battery",   "%",   "measurement",      false, nullptr },
  { "battery_state",     "Battery state",      false, nullptr,     nullptr, nullptr,          false, nullptr },
  { "charger",           "Charger",            true,  "plug",      nullptr, nullptr,          false, "mdi:power-plug" },
  { "battery_voltage",   "Battery voltage",    false, "voltage",   "V",   "measurement",      true,  "mdi:flash" },
  { "battery_time_left", "Battery time left",  false, "duration",  "min", "measurement",      true,  "mdi:timer-sand" },
  { "mesh_contacts",     "Mesh contacts",      false, nullptr,     nullptr, "measurement",    false, "mdi:account-multiple" },
  { "mesh_unread",       "Unread messages",    false, nullptr,     nullptr, "measurement",    false, "mdi:message-badge" },
  { "lora_rx_packets",   "LoRa packets received", false, nullptr,  nullptr, "total_increasing", true, "mdi:radio-tower" },
  { "lora_last_rx",      "Last LoRa packet",   false, "timestamp", nullptr, nullptr,          true,  "mdi:radio-tower" },
};

static inline bool haCharging(const HaSensorValues& v) { return v.chargeState == 1 || v.chargeState == 2; }

// mdi:battery-50, mdi:battery-charging-80 ... as the phone apps do
static inline void haBatteryIcon(char* out, size_t n, int pct, bool charging) {
  if (pct < 0) { snprintf(out, n, "mdi:battery-unknown"); return; }
  int step = ((pct + 5) / 10) * 10;
  if (step >= 100) snprintf(out, n, charging ? "mdi:battery-charging-100" : "mdi:battery");
  else if (step <= 0) snprintf(out, n, charging ? "mdi:battery-charging-outline" : "mdi:battery-outline");
  else snprintf(out, n, charging ? "mdi:battery-charging-%d" : "mdi:battery-%d", step);
}

static inline void haEpochIso(char* out, size_t n, uint32_t t) {
  // days-from-civil inverse (Howard Hinnant), UTC
  uint32_t days = t / 86400, secs = t % 86400;
  int64_t z = (int64_t)days + 719468;
  int64_t era = z / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1;
  unsigned m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  snprintf(out, n, "%04d-%02u-%02uT%02u:%02u:%02u+00:00", (int)y, m, d,
           (unsigned)(secs / 3600), (unsigned)(secs / 60 % 60), (unsigned)(secs % 60));
}

// The "state" and "icon" members of one sensor, as JSON ("\"state\":42,\"icon\":\"...\"")
static inline void haSensorStateJson(char* out, size_t n, int id, const HaSensorValues& v) {
  char st[40], ic[40];
  const HaSensorDef& d = kHaSensors[id];
  bool str = false;
  switch (id) {
    case HA_S_BATT_LEVEL:
      if (v.battPct < 0) { snprintf(st, sizeof(st), "unavailable"); str = true; }
      else snprintf(st, sizeof(st), "%d", v.battPct);
      break;
    case HA_S_BATT_STATE:
      snprintf(st, sizeof(st), "%s", v.chargeState == 3 ? "full" : haCharging(v) ? "charging"
                                     : v.usb ? "not_charging" : "discharging");
      str = true;
      break;
    case HA_S_CHARGER: snprintf(st, sizeof(st), "%s", v.usb ? "true" : "false"); break;
    case HA_S_BATT_VOLTAGE:
      if (v.battMv <= 0) { snprintf(st, sizeof(st), "unavailable"); str = true; }
      else { int cv = (v.battMv + 5) / 10; snprintf(st, sizeof(st), "%d.%02d", cv / 100, cv % 100); }
      break;
    case HA_S_TIME_LEFT:
      if (v.timeLeftMin < 0) { snprintf(st, sizeof(st), "unavailable"); str = true; }
      else snprintf(st, sizeof(st), "%d", v.timeLeftMin);
      break;
    case HA_S_CONTACTS:   snprintf(st, sizeof(st), "%d", v.contacts); break;
    case HA_S_UNREAD:     snprintf(st, sizeof(st), "%d", v.unread); break;
    case HA_S_RX_PACKETS: snprintf(st, sizeof(st), "%lu", (unsigned long)v.rxPackets); break;
    case HA_S_LAST_RX:
      if (v.lastRxEpoch == 0) snprintf(st, sizeof(st), "unavailable");
      else haEpochIso(st, sizeof(st), v.lastRxEpoch);
      str = true;
      break;
    default: st[0] = 0; break;
  }
  if (d.icon) snprintf(ic, sizeof(ic), "%s", d.icon);
  else if (id == HA_S_BATT_LEVEL) haBatteryIcon(ic, sizeof(ic), v.battPct, haCharging(v));
  else snprintf(ic, sizeof(ic), "%s", v.chargeState == 3 ? "mdi:battery" : haCharging(v)
                                      ? "mdi:battery-charging" : "mdi:battery-minus");
  snprintf(out, n, str ? "\"state\":\"%s\",\"icon\":\"%s\"" : "\"state\":%s,\"icon\":\"%s\"", st, ic);
}

// {"type":"register_sensor","data":{...}} for one sensor; false when n is too small
static inline bool haBuildRegister(char* out, size_t n, int id, const HaSensorValues& v) {
  const HaSensorDef& d = kHaSensors[id];
  char state[96];
  haSensorStateJson(state, sizeof(state), id, v);
  int w = snprintf(out, n, "{\"type\":\"register_sensor\",\"data\":{\"type\":\"%s\",\"unique_id\":\"%s\","
                           "\"name\":\"%s\",%s",
                   d.binary ? "binary_sensor" : "sensor", d.uniqueId, d.name, state);
  if (w < 0 || (size_t)w >= n) return false;
  if (d.deviceClass) w += snprintf(out + w, n - w, ",\"device_class\":\"%s\"", d.deviceClass);
  if ((size_t)w < n && d.unit) w += snprintf(out + w, n - w, ",\"unit_of_measurement\":\"%s\"", d.unit);
  if ((size_t)w < n && d.stateClass) w += snprintf(out + w, n - w, ",\"state_class\":\"%s\"", d.stateClass);
  if ((size_t)w < n && d.diagnostic) w += snprintf(out + w, n - w, ",\"entity_category\":\"diagnostic\"");
  if ((size_t)w < n) w += snprintf(out + w, n - w, "}}");
  return w > 0 && (size_t)w < n;
}

// {"type":"update_sensor_states","data":[...all sensors...]}; false when n is too small
static inline bool haBuildUpdate(char* out, size_t n, const HaSensorValues& v) {
  int w = snprintf(out, n, "{\"type\":\"update_sensor_states\",\"data\":[");
  for (int id = 0; id < HA_SENSOR_COUNT && w > 0 && (size_t)w < n; id++) {
    char state[96];
    haSensorStateJson(state, sizeof(state), id, v);
    w += snprintf(out + w, n - w, "%s{\"type\":\"%s\",\"unique_id\":\"%s\",%s}", id ? "," : "",
                  kHaSensors[id].binary ? "binary_sensor" : "sensor", kHaSensors[id].uniqueId, state);
  }
  if (w > 0 && (size_t)w < n) w += snprintf(out + w, n - w, "]}");
  return w > 0 && (size_t)w < n;
}
