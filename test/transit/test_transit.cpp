// Unit tests for the lock screen departures (examples/companion_radio/ui-new/MeckTransitUtil.h).
// Plain C++, no Arduino or PlatformIO needed. On Windows through WSL, from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/transit/test_transit.cpp -o /tmp/test_transit && /tmp/test_transit"

#include "MeckTransitUtil.h"
#include <stdio.h>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long _a = (long)(a), _b = (long)(b); if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %ld, want %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: %s = \"%s\", want \"%s\"\n", __FILE__, __LINE__, #a, (a), (b)); } } while (0)

static const uint32_t NOW = 1794723855;   // 2026-11-16 07:24:15 local (UTC+1)
static const int32_t OFF = 3600;

static bool parse(const std::string& s, TrBoard& b, uint32_t now = NOW) {
  return trParse(s.data(), s.size(), now, OFF, b);
}

// The mock the firmware was written against (docs/transit-widget.md)
static const char* kMock =
  "{\"v\":1,\"ts\":1794723855,\"now\":\"07:24\",\"kind\":\"board\","
  "\"hdr\":{\"t\":\"\xC5\x98\xC3\xAD\xC4\x8D" "any, n\xC3\xA1" "dra\xC5\xBE\xC3\xAD\",\"r\":\"then\"},"
  "\"rows\":["
  "{\"ln\":\"S9\",\"tx\":\"Praha hl.n.\",\"pl\":\"1\",\"tm\":\"07:41\",\"then\":[\"07:45\",{\"t\":\"08:15\",\"late\":1}]},"
  "{\"ln\":\"381\",\"tx\":\"H\xC3\xA1je\",\"dly\":2,\"pl\":\"B\",\"tm\":\"07:28\",\"then\":[\"07:28\",\"07:43\"],\"x\":{\"a\":[1,2]}},"
  "{\"ln\":\"S9\",\"tx\":\"Bene\\u0161ov u Prahy\",\"dly\":75,\"pl\":\"3\",\"plc\":1,\"tm\":\"07:35\",\"late\":true,\"then\":[\"07:53\",\"08:23\"]},"
  "{\"ln\":\"17\",\"tx\":\"Bran\xC3\xADk \xE2\x86\x92 Palack\xC3\xA9ho\",\"p\":72,\"pl\":\"A\",\"tm\":\"07:04\"},"
  "{\"ln\":\"99\",\"tx\":\"fifth row is dropped\"}"
  "],\"validUntil\":1794724155,\"future\":{\"nested\":[{\"rows\":[]}]}}";

static void testMock() {
  TrBoard b;
  CHECK(parse(kMock, b));
  CHECK_EQ(b.v, 1);
  CHECK_STR(b.title, "\xC5\x98\xC3\x8D\xC4\x8C" "ANY, N\xC3\x81" "DRA\xC5\xBD\xC3\x8D");   // ŘÍČANY, NÁDRAŽÍ
  CHECK_STR(b.thenLabel, "then");
  CHECK_EQ(b.nRows, 5);   // all kept, the widget draws up to 4
  CHECK_STR(b.rows[0].line, "S9");
  CHECK_STR(b.rows[0].text, "Praha hl.n.");
  CHECK_STR(b.rows[0].badge, "");
  CHECK_STR(b.rows[0].plat, "1");
  CHECK(!b.rows[0].platChanged);
  CHECK_STR(b.rows[0].time.hm, "7:41");
  CHECK(!b.rows[0].time.late);
  CHECK_STR(b.rows[0].then[0].hm, "7:45");
  CHECK(!b.rows[0].then[0].late);
  CHECK_STR(b.rows[0].then[1].hm, "8:15");
  CHECK(b.rows[0].then[1].late);
  CHECK_STR(b.rows[1].badge, "+2");
  CHECK_STR(b.rows[1].text, "H\xC3\xA1je");
  CHECK_STR(b.rows[2].text, "Bene\xC5\xA1ov u Prahy");   // š -> š
  CHECK_STR(b.rows[2].badge, "+1 h");
  CHECK(b.rows[2].platChanged);
  CHECK(b.rows[2].time.late);
  CHECK_STR(b.rows[3].badge, "72 %");
  CHECK_STR(b.rows[3].then[0].hm, "");
  CHECK_EQ(b.validUntil, NOW + 300);
  CHECK_EQ(b.fetched, NOW);
}

static void testTolerance() {
  TrBoard b;
  // Not an object, no rows, unknown major version: refused
  CHECK(!parse("[]", b));
  CHECK(!parse("", b));
  CHECK(!parse("{\"v\":1}", b));
  CHECK(!parse("{\"v\":2,\"rows\":[]}", b));
  // No "v" is taken as 1; header as a plain string; empty rows
  CHECK(parse("{\"rows\":[],\"hdr\":\"Do pr\xC3\xA1" "ce 6:55\xE2\x86\x92" "7:30\",\"empty\":\"Dnes u\xC5\xBE nic\"}", b));
  CHECK_EQ(b.nRows, 0);
  CHECK_STR(b.title, "DO PR\xC3\x81" "CE 6:55\xC2\xBB" "7:30");   // the arrow drawn as »
  CHECK_STR(b.empty, "Dnes u\xC5\xBE nic");
  CHECK_EQ(b.validUntil, 0);
  // Whitespace, a ready badge, delay as text, 0 delay, numbers as strings
  CHECK(parse("{ \"v\" : \"1\" ,\n \"rows\" : [ { \"line\" : \"B\" , \"text\":\"\\\"Z\\\" \\/ x\", \"badge\":\"+8 h\" },"
              "{\"l\":\"T\",\"d\":\"+3\"},{\"l\":\"T\",\"dly\":0},{\"l\":\"T\",\"dly\":-1,\"dep\":\"23:59\"} ] }", b));
  CHECK_EQ(b.nRows, 4);
  CHECK_STR(b.rows[0].text, "\"Z\" / x");
  CHECK_STR(b.rows[0].badge, "+8 h");
  CHECK_STR(b.rows[1].badge, "+3");
  CHECK_STR(b.rows[2].badge, "");
  CHECK_STR(b.rows[3].badge, "");
  CHECK_STR(b.rows[3].time.hm, "23:59");
  // A "rows" key inside another object is not the board's
  CHECK(!parse("{\"v\":1,\"x\":{\"rows\":[{}]}}", b));
  // Cut-off reply: no crash, refused or partial
  std::string m = kMock;
  for (size_t n = 0; n < m.size(); n += 7) { TrBoard c; trParse(m.data(), n, NOW, OFF, c); }
  // Long text is cut at a whole UTF-8 character
  std::string longText = "{\"rows\":[{\"tx\":\"";
  for (int i = 0; i < 40; i++) longText += "\xC5\xA1";
  longText += "\"}]}";
  CHECK(parse(longText, b));
  CHECK_EQ(strlen(b.rows[0].text), 46);
  // Then-lateness as a separate array
  CHECK(parse("{\"rows\":[{\"nx\":[\"7:45\",\"8:15\",\"8:45\"],\"nxl\":[0,1,1]}]}", b));
  CHECK_STR(b.rows[0].then[0].hm, "7:45");
  CHECK(!b.rows[0].then[0].late);
  CHECK(b.rows[0].then[1].late);
}

static void testValidUntil() {
  TrBoard b;
  // Relative to the server's ts: a deck clock 100 s behind still waits 300 s
  CHECK(parse("{\"ts\":1794723855,\"rows\":[],\"validUntil\":1794724155}", b, NOW - 100));
  CHECK_EQ(b.validUntil, NOW - 100 + 300);
  // No ts: absolute
  CHECK(parse("{\"rows\":[],\"validUntil\":1794724155}", b));
  CHECK_EQ(b.validUntil, NOW + 300);
  // Small number: seconds from now
  CHECK(parse("{\"rows\":[],\"validUntil\":90}", b));
  CHECK_EQ(b.validUntil, NOW + 90);
  // "HH:MM" local, next occurrence: 07:30 today, 06:00 tomorrow
  CHECK(parse("{\"rows\":[],\"validUntil\":\"07:30\"}", b));
  CHECK_EQ(b.validUntil, NOW - 15 - 4 * 60 + 10 * 60);
  CHECK(parse("{\"rows\":[],\"validUntil\":\"06:00\"}", b));
  CHECK_EQ(b.validUntil, NOW - 15 - 24 * 60 - 60 * 60 + 86400);
}

static void testText() {
  char s[64];
  strcpy(s, "\xC5\x99\xC3\xAD\xC4\x8D" "any \xC5\xBE" "ernosek \xC4\x8F\xC5\xA5\xC5\x88\xC5\xAF\xC3\xBD\xC4\x9B");
  trUpper(s);
  CHECK_STR(s, "\xC5\x98\xC3\x8D\xC4\x8C" "ANY \xC5\xBD" "ERNOSEK \xC4\x8E\xC5\xA4\xC5\x87\xC5\xAE\xC3\x9D\xC4\x9A");
  strcpy(s, "\xC3\xB7 \xE2\x86\x92 ok");   // ÷ and the arrow stay
  trUpper(s);
  CHECK_STR(s, "\xC3\xB7 \xE2\x86\x92 OK");
  char c[8];
  trClock("07:45", c, sizeof(c)); CHECK_STR(c, "7:45");
  trClock("17:45", c, sizeof(c)); CHECK_STR(c, "17:45");
  trClock("00:05", c, sizeof(c)); CHECK_STR(c, "0:05");
  trClock("now", c, sizeof(c));   CHECK_STR(c, "now");
  trDelayChip(0, c, sizeof(c));   CHECK_STR(c, "");
  trDelayChip(12, c, sizeof(c));  CHECK_STR(c, "+12");
  trDelayChip(60, c, sizeof(c));  CHECK_STR(c, "+1 h");
  trDelayChip(489, c, sizeof(c)); CHECK_STR(c, "+8 h");
}

static void testUrl() {
  char u[200];
  trUrl(u, sizeof(u), "https://nj.example.org/", 3, false, 0, 0);
  CHECK_STR(u, "https://nj.example.org/compact/widget?rows=3");
  trUrl(u, sizeof(u), "nj.example.org", 2, true, trDeg1e4(49.99312), trDeg1e4(14.67049));
  CHECK_STR(u, "https://nj.example.org/compact/widget?rows=2&lat=49.9931&lon=14.6705");
  trUrl(u, sizeof(u), "http://10.0.0.2:8092/api", 4, true, trDeg1e4(-0.00004), trDeg1e4(-0.5));
  CHECK_STR(u, "http://10.0.0.2:8092/api/compact/widget?rows=4&lat=0.0000&lon=-0.5000");
  // Too small a buffer: cut, never past it
  char t[20];
  trUrl(t, sizeof(t), "https://nj.example.org", 3, true, 1, 1);
  CHECK(strlen(t) < sizeof(t));
}

static void testSchedule() {
  // NOW is 07:24:15 local; the minute starts at NOW - 15
  const uint32_t M0 = NOW - 15;
  TrBoard b;
  // Nothing leaves within 20 min: 15 min after this request
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:50\"}],\"validUntil\":60}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), NOW + 900);
  // A bus every 8 minutes: 5 min before the next one that is not yet within its 5 minutes
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:28\",\"then\":[{\"t\":\"07:36\"},{\"t\":\"07:44\"}]}]}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), M0 + (36 - 24 - 5) * 60);   // 7:31, for the 7:36
  CHECK_EQ(trNextRequest(b, 3, M0 + 7 * 60, OFF, false), M0 + 15 * 60);     // asked at 7:31: 7:39, for the 7:44
  // The delay counts: 7:33 + 4 = 7:37, asked at 7:32
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:33\",\"d\":4}]}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), M0 + 8 * 60);
  // Not sooner than 2 min (5 over 4G)
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:30\"}]}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), NOW + 120);
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, true), NOW + 300);
  // Only the shown rows count: the third row's 7:33 is not drawn with 2 rows
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:45\"},{\"at\":\"07:46\"},{\"at\":\"07:33\"}]}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), M0 + 4 * 60);   // sorted: the 7:33 is drawn first
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:45\"},{\"at\":\"07:46\"},{\"at\":\"07:50\"}]}", b));
  CHECK_EQ(trNextRequest(b, 2, NOW, OFF, false), NOW + 900);     // 7:40 for the 7:45 is after 7:39:15
  // A trip: only the moment to leave, not the later rides
  CHECK(parse("{\"kind\":\"trip\",\"rows\":[{\"at\":\"07:36\"},{\"at\":\"07:29\"}]}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), M0 + 7 * 60);
  // Nothing to show: the server's validUntil when sooner (a usual window starts)
  CHECK(parse("{\"kind\":\"none\",\"rows\":[],\"ts\":1794723855,\"validUntil\":1794724455}", b));
  CHECK_EQ(trNextRequest(b, 3, NOW, OFF, false), NOW + 600);
  CHECK_EQ(trBackoff(1), 120);
  CHECK_EQ(trBackoff(2), 240);
  CHECK_EQ(trBackoff(3), 480);
  CHECK_EQ(trBackoff(4), 900);
  CHECK_EQ(trBackoff(200), 900);
}

static void testView() {
  TrBoard b;
  CHECK(parse(kMock, b));
  TrView v = trViewAt(b, true, true, NOW + 60, OFF);
  CHECK_EQ(v.state, TR_ROWS);
  CHECK(!v.stale);
  CHECK_STR(v.staleAt, "7:24");
  // Past its validUntil is not stale: the deck asks less often than that
  v = trViewAt(b, true, true, NOW + 300 + 121, OFF);
  CHECK(!v.stale);
  // The next request is overdue (15 min plus a request's worth): stale, rows still shown
  v = trViewAt(b, true, true, NOW + 900 + 121, OFF);
  CHECK(v.stale);
  CHECK_EQ(v.state, TR_ROWS);
  // The last request failed
  v = trViewAt(b, true, false, NOW + 60, OFF);
  CHECK(v.stale);
  // Two hours on: the empty state, still marked stale with the time
  v = trViewAt(b, true, false, NOW + TR_MAX_AGE_S, OFF);
  CHECK_EQ(v.state, TR_EMPTY);
  CHECK(v.stale);
  CHECK_STR(v.staleAt, "7:24");
  // Never a reply: empty and stale without a time
  TrBoard none = {};
  v = trViewAt(none, false, false, NOW, OFF);
  CHECK_EQ(v.state, TR_EMPTY);
  CHECK(v.stale);
  CHECK_STR(v.staleAt, "");
  // An empty board that is fresh: empty, not stale
  CHECK(parse("{\"rows\":[],\"validUntil\":600}", b));
  v = trViewAt(b, true, true, NOW + 10, OFF);
  CHECK_EQ(v.state, TR_EMPTY);
  CHECK(!v.stale);
}

// The two examples of api/README.md "Compact widget (v1)", halecivo/neujede
// PR #24 (feature/compact-widget, ff5cf638), verbatim
static void testNeujedeExamples() {
  static const uint32_t TS = 1794819900;   // "now":"10:05"
  TrBoard b;
  CHECK(parse("{\"v\":1,\"ts\":1794819900,\"now\":\"10:05\",\"kind\":\"board\",\"h\":\"And\xC4\x9Bl\",\n"
              " \"rows\":[{\"m\":\"T\",\"l\":\"9\",\"t\":\"Spojovac\xC3\xAD\",\"pl\":\"3\",\"pc\":1,\"at\":\"10:09\"},\n"
              "         {\"m\":\"T\",\"l\":\"9\",\"t\":\"S\xC3\xAD" "dli\xC5\xA1t\xC4\x9B \xC5\x98" "epy\",\"d\":2,\"pl\":\"A\",\"at\":\"10:08\",\n"
              "          \"then\":[{\"t\":\"10:18\"},{\"t\":\"10:28\",\"late\":1}]}],\n"
              " \"validUntil\":1794820020,\"key\":\"5a0c3f1e\"}", b, TS));
  CHECK_STR(b.title, "AND\xC4\x9AL");
  CHECK_STR(b.kind, "board");
  CHECK_STR(b.key, "5a0c3f1e");
  CHECK_EQ(b.nRows, 2);
  CHECK_STR(b.rows[0].line, "9");
  CHECK_STR(b.rows[0].text, "Spojovac\xC3\xAD");
  CHECK_STR(b.rows[0].plat, "3");
  CHECK(b.rows[0].platChanged);
  CHECK_STR(b.rows[0].time.hm, "10:09");
  CHECK_STR(b.rows[0].badge, "");
  CHECK_STR(b.rows[1].badge, "+2");
  CHECK(!b.rows[1].platChanged);
  CHECK_STR(b.rows[1].then[0].hm, "10:18");
  CHECK(!b.rows[1].then[0].late);
  CHECK_STR(b.rows[1].then[1].hm, "10:28");
  CHECK(b.rows[1].then[1].late);
  CHECK_EQ(b.validUntil, TS + 120);

  CHECK(parse("{\"v\":1,\"ts\":1794819900,\"now\":\"10:05\",\"kind\":\"trip\",\"h\":\"Do pr\xC3\xA1" "ce\",\"dep\":\"10:10\",\"arr\":\"10:30\",\n"
              " \"rows\":[{\"m\":\"T\",\"l\":\"17\",\"t\":\"N\xC3\xA1" "dra\xC5\xBE\xC3\xAD Bran\xC3\xADk\xE2\x86\x92" "And\xC4\x9Bl\",\"d\":3,\"pl\":\"A\",\"at\":\"10:07\",\n"
              "          \"then\":[{\"t\":\"10:17\"},{\"t\":\"10:27\",\"late\":1}]},\n"
              "         {\"m\":\"M\",\"l\":\"B\",\"t\":\"And\xC4\x9Bl\xE2\x86\x92" "Florenc\",\"p\":72,\"pl\":\"1\",\"at\":\"10:23\",\"then\":[{\"t\":\"10:27\"}]}],\n"
              " \"validUntil\":1794820020,\"key\":\"744ad81a\"}", b, TS));
  CHECK_STR(b.title, "DO PR\xC3\x81" "CE 10:10\xC2\xBB" "10:30");   // the font has no arrow: »
  CHECK_EQ(b.nRows, 2);
  CHECK_STR(b.rows[0].text, "N\xC3\xA1" "dra\xC5\xBE\xC3\xAD Bran\xC3\xADk");   // a ride: where to board only
  CHECK_STR(b.rows[1].text, "And\xC4\x9Bl");
  CHECK_STR(b.rows[0].badge, "+3");
  CHECK_STR(b.rows[1].line, "B");
  CHECK_STR(b.rows[1].badge, "72 %");
  CHECK_STR(b.rows[1].then[0].hm, "10:27");
  CHECK_STR(b.rows[1].then[1].hm, "");

  // Alternatives, one per row: the arrival in its own field, the runs kept, the text as sent
  CHECK(parse("{\"v\":1,\"kind\":\"trip\",\"h\":\"Dom\xC5\xAF\",\"rows\":["
              "{\"l\":\"17\",\"t\":\"8\xE2\x86\x92" "B\xE2\x86\x92S9\",\"d\":1,\"pl\":\"A\",\"at\":\"07:28\",\"arr\":\"07:58\",\"then\":[{\"t\":\"07:36\"},{\"t\":\"07:44\"}]},"
              "{\"l\":\"S9\",\"t\":\"p\xC5\x99" "es Palack\xC3\xA9ho\",\"at\":\"07:34\",\"arr\":\"08:01\"}]}", b));
  CHECK_EQ(b.nRows, 2);
  CHECK_STR(b.rows[0].text, "8\xC2\xBB" "B\xC2\xBBS9");
  CHECK_STR(b.rows[0].arr, "\xC2\xBB" "7:58");
  CHECK_STR(b.rows[0].then[1].hm, "7:44");
  CHECK_STR(b.rows[1].arr, "\xC2\xBB" "8:01");
  CHECK_STR(b.rows[1].then[0].hm, "");
  {
    char o[8];
    trRowTime(b, 1, true, NOW, OFF, o, sizeof(o));   // every alternative counts down
    CHECK_STR(o, "10 m");
  }

  // Rides and an alternative after them are not mixed: the alternative is left out.
  // The ride you are on keeps its "» where to get off"; nothing to show
  CHECK(parse("{\"v\":1,\"ts\":1794819900,\"kind\":\"trip\",\"h\":\"Dom\xC5\xAF\",\"rows\":["
              "{\"m\":\"T\",\"l\":\"17\",\"t\":\"p\xC5\x99" "es Palack\xC3\xA9ho\",\"at\":\"07:04\",\"arr\":\"07:30\"},"
              "{\"m\":\"T\",\"l\":\"22\",\"t\":\"\xE2\x86\x92 Andel\",\"at\":\"07:15\",\"on\":1,\"then\":[{\"t\":\"07:20\"},{\"t\":\"07:30\"}]}],"
              "\"more\":2,\"validUntil\":1794820020,\"key\":\"x\"}", b, TS));
  CHECK_STR(b.title, "DOM\xC5\xAE");
  CHECK_EQ(b.nRows, 1);
  CHECK_STR(b.rows[0].line, "22");
  CHECK_STR(b.rows[0].text, "\xC2\xBB Andel");
  CHECK_STR(b.rows[0].then[1].hm, "7:30");
  CHECK_STR(b.rows[0].arr, "");
  CHECK_EQ(b.more, 2);
  CHECK(parse("{\"v\":1,\"ts\":1794819900,\"now\":\"10:05\",\"kind\":\"none\",\"h\":\"\",\"rows\":[],\"validUntil\":1794820800,\"key\":\"0\"}", b, TS));
  CHECK_EQ(b.nRows, 0);
  CHECK_EQ(trViewAt(b, true, true, TS, OFF).state, TR_EMPTY);
}

// The time column: minutes to go, or the clock time
static void testCountdown() {
  TrBoard b;
  char o[8];
  // NOW is 07:24:15 local
  CHECK(parse("{\"kind\":\"board\",\"rows\":[{\"at\":\"07:30\"},{\"at\":\"07:28\",\"d\":3},{\"at\":\"07:24\"},"
              "{\"at\":\"08:24\"}]}", b));
  CHECK_EQ(b.rows[1].delay, 3);
  trRowTime(b, 0, true, NOW, OFF, o, sizeof(o));
  CHECK_STR(o, "6 m");
  trRowTime(b, 0, false, NOW, OFF, o, sizeof(o));   // switched off: the clock time
  CHECK_STR(o, "7:30");
  trRowTime(b, 1, true, NOW, OFF, o, sizeof(o));    // the delay counts: 7:31 for real
  CHECK_STR(o, "7 m");
  trRowTime(b, 2, true, NOW, OFF, o, sizeof(o));    // within this minute
  CHECK_STR(o, "te\xC4\x8F");
  trRowTime(b, 3, true, NOW, OFF, o, sizeof(o));    // an hour or more: the clock time
  CHECK_STR(o, "8:24");
  trRowTime(b, 0, true, NOW + 45, OFF, o, sizeof(o));   // 07:25:00
  CHECK_STR(o, "5 m");
  trRowTime(b, 0, true, NOW + 600, OFF, o, sizeof(o));  // it has left
  CHECK_STR(o, "7:30");
  // Over midnight, and a run 8 hours late that comes in a minute
  CHECK(parse("{\"rows\":[{\"at\":\"00:05\"},{\"at\":\"23:12\",\"d\":489}]}", b));
  trRowTime(b, 0, true, NOW + 16 * 3600 + 30 * 60, OFF, o, sizeof(o));   // 23:54
  CHECK_STR(o, "11 m");
  CHECK_EQ(trMinutesTo(b.rows[1], NOW - 4 * 60, OFF), 1);                 // 07:20, real 07:21
  // A trip: only the first row counts down
  CHECK(parse("{\"kind\":\"trip\",\"rows\":[{\"at\":\"07:30\"},{\"at\":\"07:40\"}]}", b));
  trRowTime(b, 0, true, NOW, OFF, o, sizeof(o));
  CHECK_STR(o, "6 m");
  trRowTime(b, 1, true, NOW, OFF, o, sizeof(o));
  CHECK_STR(o, "7:40");
  // No time, or no clock time
  CHECK(parse("{\"rows\":[{\"l\":\"B\"},{\"at\":\"soon\"}]}", b));
  trRowTime(b, 0, true, NOW, OFF, o, sizeof(o));
  CHECK_STR(o, "");
  trRowTime(b, 1, true, NOW, OFF, o, sizeof(o));
  CHECK_STR(o, "soon");
}

// A board moves on by itself when runs leave
static void testBoardAt() {
  TrBoard b, s;
  // NOW is 07:24:15 local
  CHECK(parse("{\"kind\":\"board\",\"rows\":["
              "{\"l\":\"9\",\"t\":\"A\",\"d\":2,\"pl\":\"1\",\"pc\":1,\"at\":\"07:26\",\"then\":[{\"t\":\"07:36\",\"late\":1},{\"t\":\"07:46\"}]},"
              "{\"l\":\"22\",\"t\":\"B\",\"at\":\"07:30\"},"
              "{\"l\":\"17\",\"t\":\"C\",\"at\":\"07:33\",\"then\":[{\"t\":\"07:41\"}]},"
              "{\"l\":\"S9\",\"t\":\"D\",\"at\":\"soon\"}]}", b));
  trBoardAt(b, NOW, OFF, s);   // nothing has left yet
  CHECK_EQ(s.nRows, 4);
  CHECK_STR(s.rows[0].line, "9");
  CHECK_STR(s.rows[3].line, "S9");   // no clock time: last
  // 07:28:30: the 9 is still there (7:26 + 2 = 7:28, its minute is not over)
  trBoardAt(b, NOW + 255, OFF, s);
  CHECK_STR(s.rows[0].line, "9");
  CHECK_STR(s.rows[0].time.hm, "7:26");
  // 07:29:00: it has left; its next run (7:36, late) comes and goes after the 22 and the 17
  trBoardAt(b, NOW + 285, OFF, s);
  CHECK_EQ(s.nRows, 4);
  CHECK_STR(s.rows[0].line, "22");
  CHECK_STR(s.rows[1].line, "17");
  CHECK_STR(s.rows[2].line, "9");
  CHECK_STR(s.rows[2].time.hm, "7:36");
  CHECK(s.rows[2].time.late);
  CHECK_EQ(s.rows[2].delay, 0);
  CHECK_STR(s.rows[2].badge, "");
  CHECK(!s.rows[2].platChanged);
  CHECK_STR(s.rows[2].plat, "1");
  CHECK_STR(s.rows[2].then[0].hm, "7:46");
  CHECK_STR(s.rows[2].then[1].hm, "");
  // 07:34:00: the 22 had no next run and goes; the 17 moves to 7:41
  trBoardAt(b, NOW + 585, OFF, s);
  CHECK_EQ(s.nRows, 3);
  CHECK_STR(s.rows[0].line, "9");
  CHECK_STR(s.rows[1].line, "17");
  CHECK_STR(s.rows[1].time.hm, "7:41");
  // 07:50: only the row without a clock time is left
  trBoardAt(b, NOW + 1545, OFF, s);
  CHECK_EQ(s.nRows, 1);
  CHECK_STR(s.rows[0].line, "S9");
  // The reply itself is kept as it came
  CHECK_EQ(b.nRows, 4);
  CHECK_STR(b.rows[0].time.hm, "7:26");
  // A trip is not touched
  CHECK(parse("{\"kind\":\"trip\",\"rows\":[{\"l\":\"9\",\"at\":\"07:00\",\"then\":[{\"t\":\"07:10\"}]}]}", b));
  trBoardAt(b, NOW, OFF, s);
  CHECK_EQ(s.nRows, 1);
  CHECK_STR(s.rows[0].time.hm, "7:00");
  // Eight rows are kept
  std::string many = "{\"rows\":[";
  for (int i = 0; i < 10; i++) many += std::string(i ? "," : "") + "{\"l\":\"" + std::to_string(i) + "\"}";
  CHECK(parse(many + "]}", b));
  CHECK_EQ(b.nRows, TR_KEEP_ROWS);
}

int main() {
  testNeujedeExamples();
  testMock();
  testTolerance();
  testValidUntil();
  testText();
  testUrl();
  testSchedule();
  testView();
  testCountdown();
  testBoardAt();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
