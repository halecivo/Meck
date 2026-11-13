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

// --- CardDAV ------------------------------------------------------------------------
// Written by hand in the shape Radicale 3 answers (to be checked against a
// capture); the names and numbers are made up.

static const char FX_HOMES[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:CR="urn:ietf:params:xml:ns:carddav"><response><href>/deck/</href><propstat><prop><C:calendar-home-set><href>/deck/</href></C:calendar-home-set><CR:addressbook-home-set><href>/deck/</href></CR:addressbook-home-set></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

// Contacts only (no calendar home): the 404 block must not break the rest
static const char FX_HOMES_AB_ONLY[] = R"FX(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:card="urn:ietf:params:xml:ns:carddav" xmlns:cal="urn:ietf:params:xml:ns:caldav"><d:response><d:href>/remote.php/dav/principals/users/deck/</d:href><d:propstat><d:prop><card:addressbook-home-set><d:href>/remote.php/dav/addressbooks/users/deck/</d:href></card:addressbook-home-set></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat><d:propstat><d:prop><cal:calendar-home-set/></d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response></d:multistatus>)FX";

static const char FX_BOOKS[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:" xmlns:CR="urn:ietf:params:xml:ns:carddav" xmlns:CS="http://calendarserver.org/ns/"><response><href>/deck/</href><propstat><prop><resourcetype><principal /><collection /></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/contacts/</href><propstat><prop><resourcetype><CR:addressbook /><collection /></resourcetype><displayname>Contacts</displayname><CS:getctag>"abc123"</CS:getctag><sync-token>http://radicale.org/ns/sync/abc123</sync-token></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

static const char FX_CARD_LIST[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:"><response><href>/deck/contacts/</href><propstat><prop><resourcetype><collection /></resourcetype></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/contacts/a1.vcf</href><propstat><prop><resourcetype /><getetag>"e1"</getetag><getcontentlength>312</getcontentlength></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/contacts/b%202.vcf</href><propstat><prop><resourcetype /><getetag>"e2"</getetag><getcontentlength>48210</getcontentlength></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

// multiget reply: an escaped card, a CDATA card, a card without numbers
static const char FX_MULTIGET[] = "<?xml version='1.0' encoding='utf-8'?>\n"
  "<multistatus xmlns=\"DAV:\" xmlns:CR=\"urn:ietf:params:xml:ns:carddav\">"
  "<response><href>/deck/contacts/a1.vcf</href><propstat><prop><getetag>\"e1\"</getetag><CR:address-data>"
  "BEGIN:VCARD&#13;\nVERSION:3.0&#13;\nFN:Jan Nov\xC3\xA1k &amp; syn&#13;\nN:Nov\xC3\xA1k;Jan;;;&#13;\n"
  "TEL;TYPE=CELL:+420 777 123 456&#13;\nitem1.TEL;TYPE=\"voice,home\":00420 212 345 678&#13;\n"
  "TEL:+420777123456&#13;\nEND:VCARD&#13;\n</CR:address-data></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
  "<response><href>/deck/contacts/b%202.vcf</href><propstat><prop><getetag>\"e2\"</getetag><CR:address-data><![CDATA[BEGIN:VCARD\r\n"
  "VERSION:3.0\r\nN:Dvo\xC5\x99\xC3\xA1k;Petr;;;\r\nPHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDQ\r\n"
  " gyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjI\r\n"
  "TEL;TYPE=WORK:(02) 9876-5432 ext. 12\r\nEND:VCARD\r\n]]></CR:address-data></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
  "<response><href>/deck/contacts/c.vcf</href><propstat><prop><getetag>\"e3\"</getetag><CR:address-data>"
  "BEGIN:VCARD&#13;\nVERSION:3.0&#13;\nFN:No Number&#13;\nEMAIL:a@example.com&#13;\nEND:VCARD&#13;\n"
  "</CR:address-data></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
  "<response><href>/deck/contacts/gone.vcf</href><status>HTTP/1.1 404 Not Found</status></response></multistatus>";

struct Got { std::vector<std::string> names, phones; };
static void gotFn(const char* n, const char* p, void* c) { ((Got*)c)->names.push_back(n); ((Got*)c)->phones.push_back(p); }

static void testPhones() {
  char o[24];
  CHECK(davPhoneNorm("+420 777-12 34 56", o, sizeof(o)));   CHECK_STR(o, "+420777123456");
  CHECK(davPhoneNorm("tel:00420777123456", o, sizeof(o)));  CHECK_STR(o, "+420777123456");
  CHECK(davPhoneNorm("(02) 9876 5432 x12", o, sizeof(o)));  CHECK_STR(o, "0298765432");
  CHECK(davPhoneNorm("777123456;ext=5", o, sizeof(o)));     CHECK_STR(o, "777123456");
  CHECK(!davPhoneNorm("12", o, sizeof(o)));
  CHECK(!davPhoneNorm("+1 234 567 890 123 456 789 012 345 678", o, sizeof(o)));   // does not fit
  CHECK(!davPhoneNorm("", o, sizeof(o)));
  CHECK(davPhoneSame("+420777123456", "777 123 456"));
  CHECK(davPhoneSame("+420777123456", "00420777123456"));
  CHECK(!davPhoneSame("+420777123456", "+420777123457"));
  CHECK(!davPhoneSame("1234", "+421234"));           // short numbers only whole
  CHECK(davPhoneSame("1234", "1234"));
  CHECK(!davPhoneSame("", ""));
}

static void testHomes() {
  auto v = parse(FX_HOMES);
  CHECK(v.size() == 1);
  CHECK_STR(v[0].home, "/deck/");
  CHECK_STR(v[0].abHome, "/deck/");
  v = parse(FX_HOMES_AB_ONLY);
  CHECK(v.size() == 1);
  CHECK_STR(v[0].home, "");
  CHECK_STR(v[0].abHome, "/remote.php/dav/addressbooks/users/deck/");
}

static void testBooks() {
  auto v = parse(FX_BOOKS);
  CHECK(v.size() == 2);
  CHECK(!v[0].isAddressbook);
  CHECK(v[1].isAddressbook && v[1].isCollection && !v[1].isCalendar);
  CHECK_STR(v[1].name, "Contacts");
  CHECK_STR(v[1].ctag, "\"abc123\"");
  CHECK_STR(v[1].syncToken, "http://radicale.org/ns/sync/abc123");
  v = parse(FX_CARD_LIST);
  CHECK(v.size() == 3);
  CHECK(v[0].isCollection && v[0].contentLength == -1);
  CHECK(!v[1].isCollection && v[1].contentLength == 312);
  CHECK_STR(v[2].href, "/deck/contacts/b%202.vcf");
  CHECK(v[2].contentLength == 48210);
}

static void testMultiget() {
  auto v = parse(FX_MULTIGET);
  CHECK(v.size() == 4);
  Got g;
  int cards = 0;
  for (auto& r : v) cards += davVcardParse(r.data, r.dataLen, gotFn, &g);
  CHECK(cards == 3);
  CHECK(v[3].status == 404 && v[3].data == nullptr);
  CHECK(g.phones.size() == 3);
  if (g.phones.size() == 3) {
    CHECK_STR(g.names[0].c_str(), "Jan Nov\xC3\xA1k & syn");
    CHECK_STR(g.phones[0].c_str(), "+420777123456");     // the duplicate TEL dropped
    CHECK_STR(g.phones[1].c_str(), "+420212345678");     // grouped, quoted TYPE with a comma
    CHECK_STR(g.names[2].c_str(), "Petr Dvo\xC5\x99\xC3\xA1k");   // from N, no FN
    CHECK_STR(g.phones[2].c_str(), "0298765432");
  }
}

static void testVcardOddities() {
  // vCard 2.1 quoted-printable with a soft break, LF only, escapes, ORG name
  const char* c1 =
    "BEGIN:VCARD\nVERSION:2.1\nN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:=C5=A0imek;Pavel=\n;;;\n"
    "TEL;CELL:+420 603 000 111\nEND:VCARD\n"
    "BEGIN:VCARD\nVERSION:3.0\nFN:Smith\\, John\nTEL:+44 20 7946 0000\nEND:VCARD\n"
    "BEGIN:VCARD\nVERSION:4.0\nORG:Pizza Roma;Delivery\nTEL;VALUE=uri:tel:+420-222-333-444\nEND:VCARD\n"
    "BEGIN:VCARD\nVERSION:4.0\nTEL:+420 111 222 333\nEND:VCARD\n"
    "BEGIN:VCARD\nFN:Folded\n Name\nTEL:123 456\n 789\nEND:VCARD";   // no newline at the end
  Got g;
  int cards = davVcardParse(c1, strlen(c1), gotFn, &g);
  CHECK(cards == 5);
  CHECK(g.phones.size() == 5);
  if (g.phones.size() == 5) {
    CHECK_STR(g.names[0].c_str(), "Pavel \xC5\xA0imek");
    CHECK_STR(g.phones[0].c_str(), "+420603000111");
    CHECK_STR(g.names[1].c_str(), "Smith, John");
    CHECK_STR(g.names[2].c_str(), "Pizza Roma");
    CHECK_STR(g.phones[2].c_str(), "+420222333444");
    CHECK_STR(g.names[3].c_str(), "+420111222333");     // no name at all: the number
    CHECK_STR(g.names[4].c_str(), "FoldedName");
    CHECK_STR(g.phones[4].c_str(), "123456789");
  }
  // Garbage, a card that never ends, a huge line: nothing, no crash
  std::string big = "BEGIN:VCARD\nFN:";
  big.append(5000, 'x');
  big += "\nTEL:+420777000000\nEND:VCARD\n";
  Got g2;
  CHECK(davVcardParse(big.c_str(), big.size(), gotFn, &g2) == 1);
  CHECK(g2.phones.size() == 1 && g2.names[0].size() == 95);
  Got g3;
  CHECK(davVcardParse("BEGIN:VCARD\nTEL:777\n", 20, gotFn, &g3) == 0 && g3.phones.empty());
  CHECK(davVcardParse("&amp&#xZZ;&#99999999;<![CDATA[", 30, gotFn, &g3) == 0);
  CHECK(davVcardParse(nullptr, 0, gotFn, &g3) == 0);
}

static void testMultigetBody() {
  char b[700];
  const char* hrefs[] = { "/deck/contacts/a1.vcf", "/deck/contacts/x&y<z>.vcf" };
  size_t n = davMultigetBody(b, sizeof(b), hrefs, 2);
  CHECK(n == strlen(b) && n > 0);
  CHECK(strstr(b, "<d:href>/deck/contacts/a1.vcf</d:href>") != nullptr);
  CHECK(strstr(b, "<d:href>/deck/contacts/x&amp;y&lt;z&gt;.vcf</d:href></r:addressbook-multiget>") != nullptr);
  CHECK(davMultigetBody(b, 300, hrefs, 2) == 0);    // too small
  CHECK(davMultigetBody(b, sizeof(b), hrefs, 0) > 0);
}

// calendar-query with expand, shaped like Radicale's reply: an expanded weekly
// event (instances in UTC), an all-day one, a cancelled one, one with
// DURATION and a VALARM, a folded SUMMARY with escapes
static const char FX_EVENTS[] = R"FX(<?xml version='1.0' encoding='utf-8'?>
<multistatus xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><response><href>/deck/test/weekly.ics</href><propstat><prop><getetag>"a"</getetag><C:calendar-data>BEGIN:VCALENDAR
VERSION:2.0
BEGIN:VEVENT
UID:weekly
DTSTART:20261113T080000Z
DTEND:20261113T081500Z
RECURRENCE-ID:20261113T080000Z
SUMMARY:Standup
END:VEVENT
BEGIN:VEVENT
UID:weekly
DTSTART:20261114T080000Z
DTEND:20261114T081500Z
RECURRENCE-ID:20261114T080000Z
SUMMARY:Standup
END:VEVENT
END:VCALENDAR
</C:calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/allday.ics</href><propstat><prop><getetag>"b"</getetag><C:calendar-data>BEGIN:VCALENDAR&#13;
BEGIN:VEVENT&#13;
DTSTART;VALUE=DATE:20261114&#13;
DTEND;VALUE=DATE:20261116&#13;
SUMMARY:Trip to R&#xED;&#x10D;any&#13;
END:VEVENT&#13;
END:VCALENDAR&#13;
</C:calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response><response><href>/deck/test/x.ics</href><propstat><prop><C:calendar-data><![CDATA[BEGIN:VCALENDAR
BEGIN:VTIMEZONE
TZID:Europe/Prague
BEGIN:STANDARD
DTSTART:19701025T030000
END:STANDARD
END:VTIMEZONE
BEGIN:VEVENT
DTSTART;TZID=Europe/Prague:20261113T150000
DURATION:PT1H30M
SUMMARY:PI planning\, Z
 ürich call\nroom 2
BEGIN:VALARM
TRIGGER:-PT15M
DESCRIPTION:not this
SUMMARY:not this either
END:VALARM
END:VEVENT
BEGIN:VEVENT
DTSTART:20261113T170000Z
STATUS:CANCELLED
SUMMARY:Gone
END:VEVENT
BEGIN:VEVENT
SUMMARY:No start
END:VEVENT
END:VCALENDAR]]></C:calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>)FX";

static void testIcal() {
  char s[20];
  davIcalUtcStamp(0, s, sizeof(s));
  CHECK_STR(s, "19700101T000000Z");
  davIcalUtcStamp(1794528000u, s, sizeof(s));       // 2026-11-13 00:00 UTC
  CHECK_STR(s, "20261113T000000Z");
  davIcalUtcStamp(951782400u + 86399, s, sizeof(s)); // 2000-02-29 23:59:59
  CHECK_STR(s, "20000229T235959Z");
  CHECK(davDaysFromCivil(2026, 11, 13) * 86400 == 1794528000);

  char b[700];
  size_t n = davCalQueryBody(b, sizeof(b), 1794528000u, 1794528000u + 7 * 86400);
  CHECK(n == strlen(b) && n > 0);
  CHECK(strstr(b, "<c:expand start=\"20261113T000000Z\" end=\"20261120T000000Z\"/>") != nullptr);
  CHECK(strstr(b, "<c:time-range start=\"20261113T000000Z\" end=\"20261120T000000Z\"/>") != nullptr);
  CHECK(davCalQueryBody(b, 100, 0, 1) == 0);

  CHECK(davIcalDuration("PT1H30M") == 5400);
  CHECK(davIcalDuration("P1D") == 86400);
  CHECK(davIcalDuration("P1W") == 7 * 86400);
  CHECK(davIcalDuration("P1DT2H") == 93600);
  CHECK(davIcalDuration("-PT5M") == -1);
  CHECK(davIcalDuration("P1H") == -1);

  std::vector<DavEvent> ev;
  auto fn = [](const DavEvent& e, void* c) { ((std::vector<DavEvent>*)c)->push_back(e); };
  std::vector<DavResponse> v = parse(FX_EVENTS);
  CHECK(v.size() == 3);
  const int32_t off = 3600;   // the deck at UTC+1
  int total = 0;
  for (auto& r : v) total += davIcalParse(r.data, r.dataLen, off, fn, &ev);
  CHECK(total == 4 && ev.size() == 4);
  if (ev.size() == 4) {
    const uint32_t day = 1794528000u;
    CHECK(ev[0].start == day + 8 * 3600 && ev[0].end == day + 8 * 3600 + 900 && !ev[0].allDay);
    CHECK_STR(ev[0].title, "Standup");
    CHECK(ev[1].start == day + 86400 + 8 * 3600);
    CHECK(ev[2].allDay && ev[2].start == day + 86400 - 3600 && ev[2].end == day + 3 * 86400 - 3600);
    CHECK_STR(ev[2].title, "Trip to R\xC3\xAD\xC4\x8D" "any");
    CHECK(!ev[3].allDay && ev[3].start == day + 14 * 3600 && ev[3].end == day + 14 * 3600 + 5400);
    CHECK_STR(ev[3].title, "PI planning, Z\xC3\xBCrich call room 2");
  }

  // Odd input: no end (a date lasts a day), bad dates, nothing at all
  ev.clear();
  const char* odd = "BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261113\nEND:VEVENT\n"
                    "BEGIN:VEVENT\nDTSTART:2026111\nSUMMARY:bad\nEND:VEVENT\n"
                    "BEGIN:VEVENT\nDTSTART:20261313T000000Z\nEND:VEVENT\n"
                    "BEGIN:VEVENT\nDTSTART:20261113T0900Z\nEND:VEVENT\n"
                    "BEGIN:VEVENT\nDTSTART:20261113T090000Z\nDTEND:20261113T080000Z\nEND:VEVENT\n"
                    "BEGIN:VEVENT\nDTSTART:20261113T090000Z\n";
  CHECK(davIcalParse(odd, strlen(odd), 0, fn, &ev) == 2);
  if (ev.size() == 2) {
    CHECK(ev[0].allDay && ev[0].end - ev[0].start == 86400 && ev[0].title[0] == 0);
    CHECK(ev[1].end == ev[1].start);
  }
  std::string longTitle = "BEGIN:VEVENT\nDTSTART:20261113T090000Z\nSUMMARY:";
  for (int k = 0; k < 40; k++) longTitle += "\xC5\x98";   // Ř, two bytes
  longTitle += "\nEND:VEVENT\n";
  ev.clear();
  CHECK(davIcalParse(longTitle.c_str(), longTitle.size(), 0, fn, &ev) == 1);
  if (ev.size() == 1) CHECK(strlen(ev[0].title) == 46);
  CHECK(davIcalParse(nullptr, 0, 0, fn, &ev) == 0);
  CHECK(davIcalParse("&amp&#xZZ;<![CDATA[", 19, 0, fn, &ev) == 0);
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
  testPhones();
  testHomes();
  testBooks();
  testMultiget();
  testVcardOddities();
  testMultigetBody();
  testIcal();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
