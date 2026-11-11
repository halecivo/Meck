// Unit tests for the CalDAV helpers (examples/companion_radio/ui-new/MeckDavUtil.h).
// Plain C++, no Arduino or PlatformIO needed. From the repository root:
//
//   g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/dav/test_dav.cpp -o /tmp/test_dav && /tmp/test_dav
//
// The multistatus replies below were captured from a Radicale 3.9.0 filled by
// tools/dav/seed.py (user "deck"); the iCloud and Nextcloud shapes are written
// by hand from their documentation and still to be checked against the real ones.

#include "MeckDavUtil.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static const char FX_PRINCIPAL[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:"><response><href>/</href><propstat><prop><current-user-principal><href>/deck/</href></current-user-principal></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

static const char FX_HOME[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><response><href>/deck/</href><propstat><prop><C:calendar-home-set><href>/deck/</href></C:calendar-home-set></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

static const char FX_LIST[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:CS="http://calendarserver.org/ns/" xmlns:ICAL="http://apple.com/ns/ical/"><response><href>/deck/</href><propstat><prop><resourcetype><principal /><collection /></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat><propstat><prop><displayname /><ICAL:calendar-color /><CS:getctag /><sync-token /><C:supported-calendar-component-set /></prop><status>HTTP/1.1 404 Not Found</status></propstat></response><response><href>/deck/test/</href><propstat><prop><resourcetype><C:calendar /><collection /></resourcetype><displayname>test</displayname><CS:getctag>"bad05e98c22e5005f3a47c65ef6a9b9123c1e2fec8025aee2992e58f3e4c77b8"</CS:getctag><sync-token>http://radicale.org/ns/sync/e82b2be853b5734a0b0345811ded38d343849478b57ee656ca1f11398759a4fd</sync-token><C:supported-calendar-component-set><C:comp name="VEVENT" /></C:supported-calendar-component-set></prop><status>HTTP/1.1 200 OK</status></propstat><propstat><prop><ICAL:calendar-color /></prop><status>HTTP/1.1 404 Not Found</status></propstat></response></multistatus>)FX";

static const char FX_SYNC[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:"><sync-token>http://radicale.org/ns/sync/e82b2be853b5734a0b0345811ded38d343849478b57ee656ca1f11398759a4fd</sync-token><response><href>/deck/test/weekly.ics</href><propstat><prop><getetag>"cdc10afb539ea41de1b240c5b8a487a427fc3c2e8cd6a136d51752bfc7226681"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/timed.ics</href><propstat><prop><getetag>"525bca5f2999a080399d122e4ed04c047013c6f304d63b767e5e4780a09245d3"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/utc.ics</href><propstat><prop><getetag>"0423cfab62e7bdc3b67f2fa8a0ea24419085c85a30f114c0f808b91d09a15f97"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/monthly.ics</href><propstat><prop><getetag>"eefd55dea1b5cc59e176f1bb7a01dcbf0ad7dc23607598c3ca09603a91d271d8"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/allday.ics</href><propstat><prop><getetag>"992df706a0214110a23c32f2bff2eee1e1006cc3dd4d5046ed5a5da43127df91"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/dst.ics</href><propstat><prop><getetag>"f9ad61607b8e2ef1f264efc2ea40b6ff1a1deb7f3039f4f4102d9803ff08fade"</getetag></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";


static std::vector<DavResponse> parse(const char* xml, char* top = nullptr, size_t topN = 0, int* rc = nullptr) {
  std::vector<DavResponse> v;
  auto fn = [](const DavResponse& r, void* c) { ((std::vector<DavResponse>*)c)->push_back(r); };
  int n = davParseMultistatus(xml, strlen(xml), fn, &v, top, topN);
  if (rc) *rc = n;
  return v;
}

static void testUrls() {
  char o[128];
  CHECK(davNormalizeUrl("  dav.example.com  ", o, sizeof(o)));            CHECK_STR(o, "https://dav.example.com");
  CHECK(davNormalizeUrl("http://192.168.1.20:5232/", o, sizeof(o)));      CHECK_STR(o, "http://192.168.1.20:5232");
  CHECK(davNormalizeUrl("https://cloud.example.com/remote.php/dav/", o, sizeof(o)));
  CHECK_STR(o, "https://cloud.example.com/remote.php/dav");
  CHECK(davNormalizeUrl("https://dav.example.com/x?y=1#z", o, sizeof(o))); CHECK_STR(o, "https://dav.example.com/x");
  CHECK(!davNormalizeUrl("", o, sizeof(o)));
  CHECK(!davNormalizeUrl("ftp://dav.example.com", o, sizeof(o)));
  CHECK(!davNormalizeUrl("https://user:pw@dav.example.com", o, sizeof(o)));  // credentials go in the fields
  CHECK(!davNormalizeUrl("https://", o, sizeof(o)));
  CHECK(!davNormalizeUrl("https://exa mple.com", o, sizeof(o)));
  CHECK(!davNormalizeUrl("https://dav.example.com/a b", o, sizeof(o)));
  char tiny[10];
  CHECK(!davNormalizeUrl("https://dav.example.com", tiny, sizeof(tiny)));

  CHECK(davUrlOrigin("https://dav.example.com:8443/a/b", o, sizeof(o)));  CHECK_STR(o, "https://dav.example.com:8443");
  CHECK(!davUrlOrigin("dav.example.com", o, sizeof(o)));
  davUrlHost("https://dav.example.com:8443/a", o, sizeof(o));              CHECK_STR(o, "dav.example.com");
  davUrlHost("http://192.168.1.20/", o, sizeof(o));                        CHECK_STR(o, "192.168.1.20");

  char r[200];
  CHECK(davResolve("https://dav.example.com/dav", "/deck/test/", r, sizeof(r)));
  CHECK_STR(r, "https://dav.example.com/deck/test/");
  CHECK(davResolve("https://dav.example.com/deck/", "test/", r, sizeof(r)));
  CHECK_STR(r, "https://dav.example.com/deck/test/");
  CHECK(davResolve("https://dav.example.com", "test/", r, sizeof(r)));
  CHECK_STR(r, "https://dav.example.com/test/");
  CHECK(davResolve("https://dav.example.com", "https://p12.example.com/x/", r, sizeof(r)));
  CHECK_STR(r, "https://p12.example.com/x/");
  char t[16];
  CHECK(!davResolve("https://dav.example.com", "/a/very/long/path/that/does/not/fit", t, sizeof(t)));

  CHECK(davSameOrigin("https://dav.example.com/a", "https://DAV.example.com/b"));
  CHECK(!davSameOrigin("https://dav.example.com/a", "https://evil.example.com/a"));
  CHECK(!davSameOrigin("https://dav.example.com/a", "http://dav.example.com/a"));   // downgrade
  CHECK(!davSameOrigin("https://dav.example.com", "https://dav.example.com:8443"));
  CHECK(davSameSite("https://caldav.icloud.com", "https://p12-caldav.icloud.com:443/x"));
  CHECK(davSameSite("https://icloud.com", "https://p12-caldav.icloud.com"));
  CHECK(!davSameSite("http://caldav.icloud.com", "https://p12-caldav.icloud.com"));   // no http
  CHECK(!davSameSite("https://caldav.icloud.com", "https://p12-caldav.icloud.com.evil.com"));
  CHECK(!davSameSite("https://caldav.icloud.com", "https://evilicloud.com"));
  CHECK(!davSameSite("https://dav.example.co.uk", "https://evil.co.uk"));          // suffix match is not enough
  CHECK_STR(davUrlPath("https://dav.example.com"), "/");
  CHECK_STR(davUrlPath("https://dav.example.com/x/y"), "/x/y");
}

static void testBasic() {
  char a[64];
  CHECK(davBasicAuth(a, sizeof(a), "Aladdin", "open sesame"));            // RFC 7617 example
  CHECK_STR(a, "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
  CHECK(davBasicAuth(a, sizeof(a), "deck", "deckpass"));                  CHECK_STR(a, "Basic ZGVjazpkZWNrcGFzcw==");
  CHECK(davBasicAuth(a, sizeof(a), "a", "b"));                            CHECK_STR(a, "Basic YTpi");
  CHECK(davBasicAuth(a, sizeof(a), "ab", "c"));                           CHECK_STR(a, "Basic YWI6Yw==");
  CHECK(davBasicAuth(a, sizeof(a), "\xC4\x8D" "esky", "h\xC3\xA9slo"));   // UTF-8 bytes as they are
  CHECK(!davBasicAuth(a, sizeof(a), "", ""));
  char tiny[8];
  CHECK(!davBasicAuth(tiny, sizeof(tiny), "deck", "deckpass"));
  char m[24];
  davMask(m, sizeof(m), "deckpass");                                      CHECK_STR(m, "(8 chars)");
  davMask(m, sizeof(m), "");                                              CHECK_STR(m, "(none)");
}

static void testText() {
  char o[64];
  { const char* x = "  a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;  "; davXmlText(x, strlen(x), o, sizeof(o)); }
  CHECK_STR(o, "a & b <c> \"d\" 'e'");
  { const char* x = "&#269;esky &#x10D; &#128512;"; davXmlText(x, strlen(x), o, sizeof(o)); }          // c-caron twice, an emoji
  CHECK_STR(o, "\xC4\x8D" "esky \xC4\x8D \xF0\x9F\x98\x80");
  { const char* x = "<![CDATA[a < b & c]]>"; davXmlText(x, strlen(x), o, sizeof(o)); }                 CHECK_STR(o, "a < b & c");
  { const char* x = "&unknown; &#0; &#;"; davXmlText(x, strlen(x), o, sizeof(o)); }                    CHECK_STR(o, "&unknown; &#0; &#;");
  char s[6];
  davXmlText("abcdefghij", 10, s, sizeof(s));                            CHECK(strlen(s) < sizeof(s));
  CHECK(davStatusCode("HTTP/1.1 200 OK") == 200);
  CHECK(davStatusCode("HTTP/1.1 404 Not Found") == 404);
  CHECK(davStatusCode("garbage") == 0);
}

static void testTrimUtf8() {
  char a[8] = "ab\xC4";            CHECK((davTrimUtf8(a), strcmp(a, "ab") == 0));          // half of a 2-byte letter
  char b[8] = "ab\xC4\x8D";       CHECK((davTrimUtf8(b), strcmp(b, "ab\xC4\x8D") == 0)); // whole
  char c[8] = "a\xE2\x82";        CHECK((davTrimUtf8(c), strcmp(c, "a") == 0));           // 2 of 3
  char d[8] = "a\xF0\x9F\x98";   CHECK((davTrimUtf8(d), strcmp(d, "a") == 0));           // 3 of 4
  char e[8] = "";                  CHECK((davTrimUtf8(e), e[0] == 0));
  char f[8] = "plain";             CHECK((davTrimUtf8(f), strcmp(f, "plain") == 0));
}

static void testPrincipalAndHome() {
  auto v = parse(FX_PRINCIPAL);
  CHECK(v.size() == 1);
  CHECK_STR(v[0].href, "/");
  CHECK_STR(v[0].principal, "/deck/");
  v = parse(FX_HOME);
  CHECK(v.size() == 1);
  CHECK_STR(v[0].home, "/deck/");
}

static void testCalendarList() {
  auto v = parse(FX_LIST);
  CHECK(v.size() == 2);
  // The home set itself: a collection, not a calendar; its 404 block is ignored
  CHECK_STR(v[0].href, "/deck/");
  CHECK(v[0].isCollection);
  CHECK(!v[0].isCalendar);
  CHECK_STR(v[0].name, "");
  CHECK_STR(v[0].syncToken, "");
  // The calendar
  CHECK_STR(v[1].href, "/deck/test/");
  CHECK(v[1].isCalendar && v[1].isCollection);
  CHECK(!v[1].isAddressbook);
  CHECK_STR(v[1].name, "test");
  CHECK(v[1].hasComps && v[1].hasVevent && !v[1].hasVtodo);
  CHECK_STR(v[1].ctag, "\"bad05e98c22e5005f3a47c65ef6a9b9123c1e2fec8025aee2992e58f3e4c77b8\"");
  CHECK(!strncmp(v[1].syncToken, "http://radicale.org/ns/sync/e82b", 32));
  CHECK_STR(v[1].color, "");   // its 404 block carried the colour
}

static void testSyncCollection() {
  char top[160];
  int rc = 0;
  auto v = parse(FX_SYNC, top, sizeof(top), &rc);
  CHECK(rc == 6 && v.size() == 6);
  CHECK(!strncmp(top, "http://radicale.org/ns/sync/e82b", 32));
  CHECK_STR(v[0].href, "/deck/test/weekly.ics");
  CHECK(v[0].etag[0] == '"');
  CHECK(strlen(v[0].etag) == 66);
  CHECK_STR(v[5].href, "/deck/test/dst.ics");
  CHECK(!v[0].isCalendar);
  // The sync token inside a response is not the top-level one
  CHECK_STR(v[0].syncToken, "");
}

static void testDeletedAndOtherServers() {
  // sync-collection: a deleted resource is a response with only href and status
  const char* del =
    "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">"
    "<d:response><d:href>/cal/gone.ics</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>"
    "<d:response><d:href>/cal/new.ics</d:href><d:propstat><d:prop><d:getetag>&quot;e1&quot;</d:getetag></d:prop>"
    "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
    "<d:sync-token>http://x/token/7</d:sync-token></d:multistatus>";
  char top[64];
  auto v = parse(del, top, sizeof(top));
  CHECK(v.size() == 2);
  CHECK(v[0].status == 404);
  CHECK_STR(v[0].href, "/cal/gone.ics");
  CHECK(v[1].status == 0);
  CHECK_STR(v[1].etag, "\"e1\"");
  CHECK_STR(top, "http://x/token/7");              // after the responses

  // iCloud-style: upper case prefix, absolute href to a partition host, colour with alpha,
  // a calendar that only holds VTODO, and a CardDAV collection
  const char* ic =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<multistatus xmlns=\"DAV:\">"
    "<response><href>/1234567890/calendars/home/</href><propstat><prop>"
    "<resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
    "<displayname>Dom&#367; &amp; pr&#225;ce</displayname>"
    "<calendar-color xmlns=\"http://apple.com/ns/ical/\">#1BADF8FF</calendar-color>"
    "<supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\">"
    "<comp name=\"VEVENT\"/><comp name=\"VTODO\"/></supported-calendar-component-set>"
    "</prop><status>HTTP/1.1 200 OK</status></propstat></response>"
    "<response><href>https://p12-caldav.icloud.com:443/1234567890/calendars/reminders/</href><propstat><prop>"
    "<resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
    "<displayname>Reminders</displayname><supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\">"
    "<comp name='VTODO'/></supported-calendar-component-set></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
    "<response><href>/1234567890/carddavhome/card/</href><propstat><prop>"
    "<resourcetype><collection/><addressbook xmlns=\"urn:ietf:params:xml:ns:carddav\"/></resourcetype>"
    "</prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>";
  v = parse(ic);
  CHECK(v.size() == 3);
  CHECK(v[0].isCalendar && v[0].hasVevent && v[0].hasVtodo);
  CHECK_STR(v[0].name, "Dom\xC5\xAF & pr\xC3\xA1" "ce");
  CHECK_STR(v[0].color, "#1BADF8");
  CHECK(v[1].isCalendar && !v[1].hasVevent && v[1].hasVtodo);
  CHECK(!strncmp(v[1].href, "https://p12-caldav.icloud.com:443/", 34));
  CHECK(v[2].isAddressbook && !v[2].isCalendar);

  // Nextcloud-style: lower case d: prefix, an attribute with '>' in a quoted value, comments
  const char* nc =
    "<?xml version=\"1.0\"?><!-- c --><d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:response><d:href>/remote.php/dav/calendars/u/personal/</d:href><d:propstat><d:prop>"
    "<d:resourcetype><d:collection/><cal:calendar/></d:resourcetype><d:displayname>Personal</d:displayname>"
    "<cal:supported-calendar-component-set><cal:comp name=\"VEVENT\" x=\"a>b\"/></cal:supported-calendar-component-set>"
    "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>";
  v = parse(nc);
  CHECK(v.size() == 1 && v[0].isCalendar && v[0].hasVevent);
  CHECK_STR(v[0].name, "Personal");
}

static void testMalformed() {
  int rc = 0;
  parse("", nullptr, 0, &rc);                                         CHECK(rc == -1);
  parse("<html><body>401</body></html>", nullptr, 0, &rc);            CHECK(rc == -1);   // a login page
  parse("not xml at all", nullptr, 0, &rc);                           CHECK(rc == -1);
  parse("<multistatus xmlns=\"DAV:\"></multistatus>", nullptr, 0, &rc); CHECK(rc == 0);
  // Cut off in the middle of a response: what was complete counts, nothing crashes
  std::string cut = FX_SYNC;
  for (size_t n = 0; n < cut.size(); n += 7) {
    auto v = parse(cut.substr(0, n).c_str(), nullptr, 0, &rc);
    CHECK(rc >= -1 && rc <= 6);
    CHECK((int)v.size() == (rc < 0 ? (int)v.size() : rc));
  }
  // Nesting beyond the limit
  std::string deep = "<multistatus>";
  for (int i = 0; i < 40; i++) deep += "<a>";
  parse(deep.c_str(), nullptr, 0, &rc);                               CHECK(rc == -1);
  // Overlong values are cut, not overflowed (ASan checks)
  std::string big = "<multistatus><response><href>" + std::string(5000, 'x') + "</href></response></multistatus>";
  auto v = parse(big.c_str());
  CHECK(v.size() == 1 && strlen(v[0].href) < sizeof(v[0].href));
}

int main() {
  testUrls();
  testBasic();
  testText();
  testTrimUtf8();
  testPrincipalAndHome();
  testCalendarList();
  testSyncCollection();
  testDeletedAndOtherServers();
  testMalformed();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
