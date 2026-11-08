// Unit tests for the Home Assistant helpers (examples/companion_radio/ui-new/MeckHaUtil.h).
// Plain C++, no Arduino or PlatformIO needed. On Windows through WSL, from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/ha/test_ha.cpp -o /tmp/test_ha && /tmp/test_ha"
//
// The addresses below are placeholders, not anyone's Home Assistant.

#include "MeckHaUtil.h"
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static void testJsonGetStr() {
  char v[64];
  const char* tok = "{\"access_token\": \"abc.def\", \"expires_in\": 1800, \"refresh_token\":\"r\\\"1\\/2\","
                    " \"token_type\": \"Bearer\"}";
  CHECK(haJsonGetStr(tok, "access_token", v, sizeof(v)));   CHECK_STR(v, "abc.def");
  CHECK(haJsonGetStr(tok, "refresh_token", v, sizeof(v)));  CHECK_STR(v, "r\"1/2");
  CHECK(!haJsonGetStr(tok, "expires_in", v, sizeof(v)));    // a number, not a string
  CHECK(!haJsonGetStr(tok, "missing", v, sizeof(v)));
  CHECK(!haJsonGetStr(tok, "token", v, sizeof(v)));         // no partial key match

  // A key name that also appears as a value
  const char* cfg = "{\"location_name\":\"internal_url\",\"internal_url\":\"http://homeassistant.local:8123\","
                    "\"external_url\":null}";
  CHECK(haJsonGetStr(cfg, "internal_url", v, sizeof(v)));   CHECK_STR(v, "http://homeassistant.local:8123");
  CHECK(!haJsonGetStr(cfg, "external_url", v, sizeof(v)));  // null
  CHECK_STR(v, "");

  // Too small a buffer: false, empty
  char s[4];
  CHECK(!haJsonGetStr(tok, "access_token", s, sizeof(s)));
  CHECK_STR(s, "");
  // Unterminated
  CHECK(!haJsonGetStr("{\"a\":\"xyz", "a", v, sizeof(v)));
}

static void testJsonEsc() {
  char o[32];
  haJsonEsc(o, sizeof(o), "T-Deck \"x\"\\");
  CHECK_STR(o, "T-Deck \\\"x\\\"\\\\");
  haJsonEsc(o, sizeof(o), "a\nb");
  CHECK_STR(o, "a\\u000ab");
  char t[6];
  haJsonEsc(t, sizeof(t), "ab\"cd");     // "ab\"c" would be 5 + NUL; the escape is not cut
  CHECK_STR(t, "ab\\\"c");
  haJsonEsc(t, 3, "a\"b");               // the escape does not fit: left out whole
  CHECK_STR(t, "a");
}

static void testUrlEncode() {
  char o[64];
  CHECK(haUrlEncode(o, sizeof(o), "http://192.168.1.20/ha/cb"));
  CHECK_STR(o, "http%3A%2F%2F192.168.1.20%2Fha%2Fcb");
  CHECK(!haUrlEncode(o, 5, "a b"));
  CHECK(haUrlEncode(o, 6, "a b"));
  CHECK_STR(o, "a%20b");
}

static void testNormalize() {
  char o[96];
  CHECK(haNormalizeBaseUrl("homeassistant.local:8123", o, sizeof(o)));   CHECK_STR(o, "http://homeassistant.local:8123");
  CHECK(haNormalizeBaseUrl(" https://ha.example.com/ ", o, sizeof(o)));  CHECK_STR(o, "https://ha.example.com");
  CHECK(haNormalizeBaseUrl("http://192.168.1.10:8123/lovelace/0?x=1", o, sizeof(o)));
  CHECK_STR(o, "http://192.168.1.10:8123");
  CHECK(!haNormalizeBaseUrl("", o, sizeof(o)));
  CHECK(!haNormalizeBaseUrl("ftp://x", o, sizeof(o)));
  CHECK(!haNormalizeBaseUrl("http:///path", o, sizeof(o)));
  CHECK(!haNormalizeBaseUrl("http://user@host", o, sizeof(o)));
  CHECK(!haNormalizeBaseUrl("http://a b", o, sizeof(o)));
}

static void testHost() {
  char h[40];
  haUrlHost("http://homeassistant.local:8123", h, sizeof(h)); CHECK_STR(h, "homeassistant.local");
  haUrlHost("https://ha.example.com", h, sizeof(h));          CHECK_STR(h, "ha.example.com");
  CHECK(haHostIsLocal("homeassistant.local"));
  CHECK(haHostIsLocal("192.168.1.10"));
  CHECK(haHostIsLocal("10.20.30.40"));
  CHECK(haHostIsLocal("172.20.1.1"));
  CHECK(!haHostIsLocal("172.32.1.1"));
  CHECK(!haHostIsLocal("8.8.8.8"));
  CHECK(!haHostIsLocal("ha.example.com"));
  CHECK(!haHostIsLocal("192.168.1.10.example.com"));
}

static void testWebhookOrder() {
  const char* out[5];
  const char* paired = "http://homeassistant.local:8123";
  const char* internal = "http://homeassistant.local:8123";   // the same as paired: once
  const char* external = "https://ha.example.com";
  int n = haWebhookBases(true, paired, internal, external, "", "", out, 5);
  CHECK(n == 2);
  CHECK_STR(out[0], internal);
  CHECK_STR(out[1], external);
  n = haWebhookBases(false, paired, internal, external, "https://hooks.example.net/x", "", out, 5);
  CHECK(n == 3);
  CHECK_STR(out[0], "https://hooks.example.net/x");
  CHECK_STR(out[1], external);
  CHECK_STR(out[2], paired);
  n = haWebhookBases(false, paired, nullptr, nullptr, nullptr, nullptr, out, 5);
  CHECK(n == 1);

  char url[128];
  haWebhookUrl(url, sizeof(url), external, "https://hooks.example.net/x", "abc");
  CHECK_STR(url, "https://ha.example.com/api/webhook/abc");
  haWebhookUrl(url, sizeof(url), "https://hooks.example.net/x", "https://hooks.example.net/x", "abc");
  CHECK_STR(url, "https://hooks.example.net/x");
}

static void testBase64Url() {
  char o[64];
  const uint8_t a[] = { 0xfb, 0xff };
  haBase64Url(o, sizeof(o), a, 2);
  CHECK_STR(o, "-_8");
  const uint8_t b[] = { 'M', 'a', 'n' };
  haBase64Url(o, sizeof(o), b, 3);
  CHECK_STR(o, "TWFu");
  haBase64Url(o, sizeof(o), b, 1);
  CHECK_STR(o, "TQ");
}

static HaSensorValues sample() {
  HaSensorValues v;
  v.battPct = 47; v.battMv = 3712; v.chargeState = 0; v.usb = false; v.timeLeftMin = 900;
  v.contacts = 12; v.unread = 3; v.rxPackets = 5000; v.lastRxEpoch = 1762600000;   // 2025-11-08T11:06:40Z
  return v;
}

static void testSensors() {
  char o[1400];
  HaSensorValues v = sample();
  haSensorStateJson(o, sizeof(o), HA_S_BATT_LEVEL, v);
  CHECK_STR(o, "\"state\":47,\"icon\":\"mdi:battery-50\"");
  haSensorStateJson(o, sizeof(o), HA_S_BATT_VOLTAGE, v);
  CHECK_STR(o, "\"state\":3.71,\"icon\":\"mdi:flash\"");
  haSensorStateJson(o, sizeof(o), HA_S_BATT_STATE, v);
  CHECK_STR(o, "\"state\":\"discharging\",\"icon\":\"mdi:battery-minus\"");
  haSensorStateJson(o, sizeof(o), HA_S_LAST_RX, v);
  CHECK_STR(o, "\"state\":\"2025-11-08T11:06:40+00:00\",\"icon\":\"mdi:radio-tower\"");
  haSensorStateJson(o, sizeof(o), HA_S_CHARGER, v);
  CHECK_STR(o, "\"state\":false,\"icon\":\"mdi:power-plug\"");

  v.chargeState = 2; v.usb = true; v.timeLeftMin = -1; v.battPct = 100; v.lastRxEpoch = 0;
  haSensorStateJson(o, sizeof(o), HA_S_BATT_LEVEL, v);
  CHECK_STR(o, "\"state\":100,\"icon\":\"mdi:battery-charging-100\"");
  haSensorStateJson(o, sizeof(o), HA_S_TIME_LEFT, v);
  CHECK_STR(o, "\"state\":\"unavailable\",\"icon\":\"mdi:timer-sand\"");
  haSensorStateJson(o, sizeof(o), HA_S_BATT_STATE, v);
  CHECK_STR(o, "\"state\":\"charging\",\"icon\":\"mdi:battery-charging\"");
  v.chargeState = 3;
  haSensorStateJson(o, sizeof(o), HA_S_BATT_STATE, v);
  CHECK_STR(o, "\"state\":\"full\",\"icon\":\"mdi:battery\"");
  v.battPct = 4; v.chargeState = 0; v.usb = false;
  haSensorStateJson(o, sizeof(o), HA_S_BATT_LEVEL, v);
  CHECK_STR(o, "\"state\":4,\"icon\":\"mdi:battery-outline\"");

  v = sample();
  CHECK(haBuildRegister(o, sizeof(o), HA_S_BATT_LEVEL, v));
  CHECK_STR(o, "{\"type\":\"register_sensor\",\"data\":{\"type\":\"sensor\",\"unique_id\":\"battery_level\","
               "\"name\":\"Battery level\",\"state\":47,\"icon\":\"mdi:battery-50\",\"device_class\":\"battery\","
               "\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\"}}");
  CHECK(haBuildRegister(o, sizeof(o), HA_S_CHARGER, v));
  CHECK(strstr(o, "\"type\":\"binary_sensor\"") != nullptr);
  CHECK(strstr(o, "state_class") == nullptr);
  CHECK(haBuildRegister(o, sizeof(o), HA_S_RX_PACKETS, v));
  CHECK(strstr(o, "\"entity_category\":\"diagnostic\"") != nullptr);
  CHECK(!haBuildRegister(o, 40, HA_S_BATT_LEVEL, v));

  CHECK(haBuildUpdate(o, sizeof(o), v));
  const char* head = "{\"type\":\"update_sensor_states\",\"data\":[{\"type\":\"sensor\",\"unique_id\":\"battery_level\",";
  CHECK(strncmp(o, head, strlen(head)) == 0);
  CHECK(strstr(o, "\"name\"") == nullptr);    // update_sensor_states refuses extra keys
  CHECK(strcmp(o + strlen(o) - 3, "}]}") == 0);
  CHECK(!haBuildUpdate(o, 200, v));
  printf("update payload: %u bytes\n", (unsigned)strlen(o));
}

static void testIso() {
  char o[32];
  haEpochIso(o, sizeof(o), 0);           CHECK_STR(o, "1970-01-01T00:00:00+00:00");
  haEpochIso(o, sizeof(o), 951782400);   CHECK_STR(o, "2000-02-29T00:00:00+00:00");
  haEpochIso(o, sizeof(o), 1735689599);  CHECK_STR(o, "2024-12-31T23:59:59+00:00");
}

static void testMask() {
  char o[16];
  haMask(o, sizeof(o), "abcdefgh");  CHECK_STR(o, "abcd\xE2\x80\xA6");
  haMask(o, sizeof(o), "");          CHECK_STR(o, "(none)");
}

int main() {
  testJsonGetStr();
  testJsonEsc();
  testUrlEncode();
  testNormalize();
  testHost();
  testWebhookOrder();
  testBase64Url();
  testSensors();
  testIso();
  testMask();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
