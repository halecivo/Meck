// Unit tests for the mobile data accounting (examples/companion_radio/ui-new/MeckDataUse.h).
// Plain C++, no Arduino or PlatformIO needed. On Windows through WSL, from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/datause/test_datause.cpp -o /tmp/test_datause && /tmp/test_datause"

#include "MeckDataUse.h"
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static void testOwner() {
  const uint8_t B = dataAppBit(DATA_APP_BROWSER), I = dataAppBit(DATA_APP_IRC),
                H = dataAppBit(DATA_APP_HA), O = dataAppBit(DATA_APP_OTHER);
  // A tag wins over the holders
  CHECK(dataUseOwner(DATA_APP_HA, B | I) == DATA_APP_HA);
  CHECK(dataUseOwner(DATA_APP_OTHER, H) == DATA_APP_OTHER);
  // One holder gets the untagged traffic, link setup included
  CHECK(dataUseOwner(DATA_APP_NONE, H) == DATA_APP_HA);
  CHECK(dataUseOwner(DATA_APP_NONE, B) == DATA_APP_BROWSER);
  CHECK(dataUseOwner(DATA_APP_NONE, H | O) == DATA_APP_HA);   // the CLI hold is not an app
  // Several holders: IRC is the one that gets data by itself
  CHECK(dataUseOwner(DATA_APP_NONE, B | I) == DATA_APP_IRC);
  CHECK(dataUseOwner(DATA_APP_NONE, B | I | H) == DATA_APP_IRC);
  CHECK(dataUseOwner(DATA_APP_NONE, B | H) == DATA_APP_OTHER);
  // Nobody, or only the CLI
  CHECK(dataUseOwner(DATA_APP_NONE, 0) == DATA_APP_OTHER);
  CHECK(dataUseOwner(DATA_APP_NONE, O) == DATA_APP_OTHER);
  // A bad tag is not trusted as an app
  CHECK(dataUseOwner(200, H) == DATA_APP_HA);
}

static void testUsage() {
  DataUsage u;
  u.clear();
  u.add(DATA_APP_HA, 500, 0);
  u.add(DATA_APP_HA, 700, 0);
  u.add(DATA_APP_BROWSER, 3000, 0);
  CHECK(u.last24h(DATA_APP_HA, 0) == 1200);
  CHECK(u.last24h(DATA_APP_BROWSER, 0) == 3000);
  CHECK(u.last24h(DATA_APP_IRC, 0) == 0);

  // Later hours add up while they are within 24 hours
  u.add(DATA_APP_HA, 100, 5);
  CHECK(u.last24h(DATA_APP_HA, 5) == 1300);
  CHECK(u.last24h(DATA_APP_HA, 23) == 1300);
  // Hour 0 drops out at hour 24, hour 5 at hour 29
  CHECK(u.last24h(DATA_APP_HA, 24) == 100);
  CHECK(u.last24h(DATA_APP_BROWSER, 24) == 0);
  CHECK(u.last24h(DATA_APP_HA, 28) == 100);
  CHECK(u.last24h(DATA_APP_HA, 29) == 0);

  // A long pause empties everything, and a bucket is reused cleanly
  u.add(DATA_APP_IRC, 42, 30);
  CHECK(u.last24h(DATA_APP_IRC, 30) == 42);
  CHECK(u.last24h(DATA_APP_IRC, 1000) == 0);
  u.add(DATA_APP_IRC, 7, 1000 + 24);   // same bucket index as hour 1000
  CHECK(u.last24h(DATA_APP_IRC, 1024) == 7);

  // An earlier hour (should not happen) counts into the newest bucket's hour
  u.add(DATA_APP_HA, 9, 3);
  CHECK(u.hour == 1024);
  CHECK(u.bytes[DATA_APP_HA][1024 % DataUsage::HOURS] == 9);
  CHECK(u.last24h(DATA_APP_HA, 1024) == 9);

  // Out of range app goes to OTHER; counts saturate
  u.clear();
  u.add(77, 10, 0);
  CHECK(u.last24h(DATA_APP_OTHER, 0) == 10);
  CHECK(u.last24h(77, 0) == 0);
  u.add(DATA_APP_HA, 0xFFFFFFF0u, 0);
  u.add(DATA_APP_HA, 0x100, 0);
  CHECK(u.bytes[DATA_APP_HA][0] == 0xFFFFFFFFu);
}

static void testConfig() {
  bool on;
  uint8_t mask;
  const uint8_t B = dataAppBit(DATA_APP_BROWSER), I = dataAppBit(DATA_APP_IRC), H = dataAppBit(DATA_APP_HA);
  // The old one-character file: every app allowed
  dataConfigParse("1", on, mask);   CHECK(on);  CHECK(mask == (B | I | H));
  dataConfigParse("0", on, mask);   CHECK(!on); CHECK(mask == (B | I | H));
  dataConfigParse("", on, mask);    CHECK(!on); CHECK(mask == (B | I | H));
  dataConfigParse(nullptr, on, mask); CHECK(!on); CHECK(mask == (B | I | H));
  dataConfigParse("1101", on, mask); CHECK(on); CHECK(mask == (B | H));
  dataConfigParse("1000", on, mask); CHECK(on); CHECK(mask == 0);
  dataConfigParse("10", on, mask);   CHECK(on); CHECK(mask == (I | H));   // later apps default on
  dataConfigParse("1110\n", on, mask); CHECK(mask == (B | I));

  char s[8];
  dataConfigFormat(s, sizeof(s), true, B | H);  CHECK_STR(s, "1101");
  dataConfigFormat(s, sizeof(s), false, 0);     CHECK_STR(s, "0000");
  dataConfigParse(s, on, mask);                 CHECK(!on); CHECK(mask == 0);
  dataConfigFormat(s, 3, true, B);              CHECK_STR(s, "");   // too small: nothing half-written
}

static void testFormat() {
  char s[16];
  dataFormatBytes(s, sizeof(s), 0);          CHECK_STR(s, "0 B");
  dataFormatBytes(s, sizeof(s), 999);        CHECK_STR(s, "999 B");
  dataFormatBytes(s, sizeof(s), 1000);       CHECK_STR(s, "1.0 kB");
  dataFormatBytes(s, sizeof(s), 1499);       CHECK_STR(s, "1.4 kB");
  dataFormatBytes(s, sizeof(s), 9999);       CHECK_STR(s, "9.9 kB");
  dataFormatBytes(s, sizeof(s), 10000);      CHECK_STR(s, "10 kB");
  dataFormatBytes(s, sizeof(s), 999999);     CHECK_STR(s, "999 kB");
  dataFormatBytes(s, sizeof(s), 1000000);    CHECK_STR(s, "1.0 MB");
  dataFormatBytes(s, sizeof(s), 2350000);    CHECK_STR(s, "2.3 MB");
  dataFormatBytes(s, sizeof(s), 15000000);   CHECK_STR(s, "15 MB");
  dataFormatBytes(s, sizeof(s), 0xFFFFFFFFu); CHECK_STR(s, "4294 MB");
}

int main() {
  testOwner();
  testUsage();
  testConfig();
  testFormat();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
