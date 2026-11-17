// Unit tests for the position sources and the offline cell database
// (examples/companion_radio/ui-new/MeckPosition.h, MeckCellDb.h). Plain C++,
// no Arduino or PlatformIO needed. On Windows through WSL, from the
// repository root:
//
//   wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/position/test_position.cpp -o /tmp/test_position && /tmp/test_position"
//
// With a file made by tools/cells/make_cell_db.py as the argument, it also
// looks up every cell of the CSV given second ("radio,mcc,net,area,cell,...").
// The cells below are made up.

#include "MeckCellDb.h"
#include "MeckPosition.h"
#include <stdio.h>
#include <algorithm>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { checks++; long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { checks++; double _a = (a), _b = (b); if (fabs(_a - _b) > (eps)) { failures++; printf("FAIL %s:%d: %s = %f, want %f\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

// --- Position sources --------------------------------------------------------

static MeckFix fix(PosSource s, double lat, double lon, uint32_t r, uint32_t at) {
  MeckFix f;
  f.lat = lat; f.lon = lon; f.radiusM = r; f.at = at; f.src = s;
  f.holdS = 0;
  return f;
}

static void testClasses() {
  CHECK_EQ(posClassOf(5), POS_FINE);
  CHECK_EQ(posClassOf(100), POS_FINE);
  CHECK_EQ(posClassOf(101), POS_LOCAL);
  CHECK_EQ(posClassOf(50000), POS_AREA);
  CHECK_EQ(posClassOf(500000), POS_REGION);
  CHECK_EQ(posClassOf(500001), POS_CLASS_COUNT);
}

static void testBest() {
  const uint32_t T = 1800000000;
  MeckPosition p;
  MeckFix f;
  CHECK(!p.best(POS_REGION, T, f));

  // Only the stored position: good for everything, as before
  p.set(fix(POS_SRC_STORED, 6, 0, MeckPosition::STORED_RADIUS_M, 0));
  CHECK(p.best(POS_FINE, T, f));
  CHECK_EQ(f.src, POS_SRC_STORED);
  CHECK(!p.best(POS_FINE, T, f, false));

  // A cell: the weather takes it over the stored one, the map does not
  p.set(fix(POS_SRC_CELL, 50.08, 14.42, 3000, T));
  CHECK(p.best(POS_AREA, T + 60, f));
  CHECK_EQ(f.src, POS_SRC_CELL);
  CHECK_EQ(f.radiusM, 3000 + 60 * MeckPosition::DRIFT_M_PER_S);
  CHECK(p.best(POS_FINE, T + 60, f));
  CHECK_EQ(f.src, POS_SRC_STORED);
  CHECK(!p.best(POS_LOCAL, T, f, false));   // 3 km is not LOCAL

  // A GPS fix wins while fresh, and stays the best for the weather a while
  p.set(fix(POS_SRC_GPS, 50.1, 14.5, 20, T));
  CHECK(p.best(POS_FINE, T + 2, f));
  CHECK_EQ(f.src, POS_SRC_GPS);
  CHECK(p.best(POS_AREA, T + 60, f));
  CHECK_EQ(f.src, POS_SRC_GPS);              // 20 + 900 m < 3 km + 900 m
  // A minute old it is no longer FINE: the stored position again
  CHECK(p.best(POS_FINE, T + 60, f));
  CHECK_EQ(f.src, POS_SRC_STORED);

  // Aged out of AREA after 50 km / 15 m/s = 3332 s
  p.clear(POS_SRC_CELL);
  CHECK(p.best(POS_AREA, T + 3330, f, false));
  CHECK(!p.best(POS_AREA, T + 3335, f, false));
  CHECK(p.best(POS_REGION, T + 3335, f, false));

  // A cell the modem keeps confirming does not age until its hold ends
  MeckFix c = fix(POS_SRC_CELL, 50.0, 14.0, 40000, T);
  c.holdS = 900;
  p.set(c);
  CHECK_EQ(p.radiusNow(p.fix(POS_SRC_CELL), T + 900), 40000);
  CHECK_EQ(p.radiusNow(p.fix(POS_SRC_CELL), T + 1000), 40000 + 100 * MeckPosition::DRIFT_M_PER_S);

  // A clock that went back does not make a fix better than it was
  CHECK_EQ(p.radiusNow(p.fix(POS_SRC_GPS), T - 100), 20);
}

// --- CPSI ----------------------------------------------------------------------

static void testCpsi() {
  CellId id;
  CHECK(cellParseCpsi("\r\n+CPSI: LTE,Online,230-01,0x1A2B,26845953,123,EUTRAN-BAND20,6300,5,5,-110,-1150,-830,10\r\n\r\nOK\r\n", id));
  CHECK_EQ(id.radio, CELL_LTE);
  CHECK_EQ(id.mcc, 230);
  CHECK_EQ(id.mnc, 1);
  CHECK_EQ(id.area, 0x1A2B);
  CHECK_EQ(id.cell, 26845953);
  CHECK(cellParseCpsi("+CPSI: GSM,Online,226-10,0x00f1,4021,40 EGSM 900,-77,0,40-40", id));
  CHECK_EQ(id.radio, CELL_GSM);
  CHECK_EQ(id.mcc, 226);
  CHECK_EQ(id.mnc, 10);
  CHECK_EQ(id.area, 0xF1);
  CHECK_EQ(id.cell, 4021);
  CHECK(cellParseCpsi("+CPSI: WCDMA,Online,230-03,0x0BB8,1234567,WCDMA IMT 2000,10737,0,0", id));
  CHECK_EQ(id.radio, CELL_UMTS);
  CHECK(!cellParseCpsi("+CPSI: NO SERVICE,Online", id));
  CHECK(!cellParseCpsi("+CPSI: LTE,Online,230-01,0x1A2B", id));          // cut short
  CHECK(!cellParseCpsi("+CPSI: LTE,Online,23001,0x1A2B,268,1", id));     // no dash
  CHECK(!cellParseCpsi("ERROR", id));
}

// --- Database ------------------------------------------------------------------

struct MemIo : CellDbIo {
  std::vector<uint8_t> d;
  bool read(uint32_t off, void* buf, uint32_t n) override {
    if ((uint64_t)off + n > d.size()) return false;
    memcpy(buf, &d[off], n);
    return true;
  }
};

struct FileIo : CellDbIo {
  FILE* f;
  bool read(uint32_t off, void* buf, uint32_t n) override {
    return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
  }
};

static CellDbRecord rec(uint8_t radio, uint16_t mnc, uint32_t area, uint32_t cell, double lat, double lon, uint32_t range) {
  CellDbRecord r;
  memset(&r, 0, sizeof(r));
  r.radio = radio; r.mnc = mnc; r.area = area; r.cell = cell;
  r.lat = (int32_t)lround(lat * 1e6); r.lon = (int32_t)lround(lon * 1e6);
  r.range10 = (uint16_t)(range / 10);
  return r;
}

static MemIo makeDb(std::vector<CellDbRecord> v, uint16_t mcc) {
  std::sort(v.begin(), v.end(), [](const CellDbRecord& a, const CellDbRecord& b) {
    return cellDbCompare(a, b.radio, b.mnc, b.area, b.cell) < 0;
  });
  CellDbHeader h;
  memset(&h, 0, sizeof(h));
  h.magic = CELLDB_MAGIC; h.version = CELLDB_VERSION; h.recSize = CELLDB_RECORD;
  h.mcc = mcc; h.count = (uint32_t)v.size();
  MemIo io;
  io.d.resize(sizeof(h) + v.size() * sizeof(CellDbRecord));
  memcpy(&io.d[0], &h, sizeof(h));
  if (!v.empty()) memcpy(&io.d[sizeof(h)], &v[0], v.size() * sizeof(CellDbRecord));
  return io;
}

static CellId cid(uint8_t radio, uint16_t mcc, uint16_t mnc, uint32_t area, uint32_t cell) {
  CellId c;
  c.radio = radio; c.mcc = mcc; c.mnc = mnc; c.area = area; c.cell = cell;
  return c;
}

static void testDb() {
  std::vector<CellDbRecord> v;
  // Area 100 of network 1: four cells around 50.0 / 14.0, 0.1 degree apart
  v.push_back(rec(CELL_LTE, 1, 100, 11, 50.05, 14.0, 1200));
  v.push_back(rec(CELL_LTE, 1, 100, 12, 49.95, 14.0, 800));
  v.push_back(rec(CELL_LTE, 1, 100, 13, 50.0, 14.05, 300));
  v.push_back(rec(CELL_LTE, 1, 100, 14, 50.0, 13.95, 1000));
  // Neighbours in the sort order that must not leak in
  v.push_back(rec(CELL_LTE, 1, 99, 15, 40.0, 10.0, 1000));
  v.push_back(rec(CELL_LTE, 1, 101, 11, 60.0, 20.0, 1000));
  v.push_back(rec(CELL_LTE, 2, 100, 11, 45.0, 15.0, 1000));
  v.push_back(rec(CELL_GSM, 1, 100, 11, 48.0, 16.0, 5000));
  MemIo io = makeDb(v, 230);
  CellDbHeader h;
  CHECK(cellDbHeader(io, h));
  CHECK_EQ(h.count, 8);

  CellFix f;
  CHECK(cellDbLocate(io, h, cid(CELL_LTE, 230, 1, 100, 13), f));
  CHECK(f.exact);
  CHECK_NEAR(f.lat, 50.0, 1e-6);
  CHECK_NEAR(f.lon, 14.05, 1e-6);
  CHECK_EQ(f.radiusM, CELLDB_MIN_RADIUS_M);   // 300 m in the file: never closer than 500
  CHECK(cellDbLocate(io, h, cid(CELL_LTE, 230, 1, 100, 11), f));
  CHECK_EQ(f.radiusM, 1200);
  CHECK(cellDbLocate(io, h, cid(CELL_GSM, 230, 1, 100, 11), f));
  CHECK_NEAR(f.lat, 48.0, 1e-6);              // same numbers, other radio

  // Unknown cell of a known area: the middle of the four, covering them
  CHECK(cellDbLocate(io, h, cid(CELL_LTE, 230, 1, 100, 77), f));
  CHECK(!f.exact);
  CHECK_EQ(f.cells, 4);
  CHECK_NEAR(f.lat, 50.0, 1e-6);
  CHECK_NEAR(f.lon, 14.0, 1e-6);
  CHECK_EQ(f.radiusM, 7000);   // 9 in 10 of the four: 5.56 km + 1.2 km range, rounded up to the km

  // Unknown area, other country, other network
  CHECK(!cellDbLocate(io, h, cid(CELL_LTE, 230, 1, 102, 11), f));
  CHECK(!cellDbLocate(io, h, cid(CELL_LTE, 231, 1, 100, 11), f));
  CHECK(!cellDbLocate(io, h, cid(CELL_LTE, 230, 3, 100, 11), f));
  CHECK(!cellDbLocate(io, h, cid(CELL_UMTS, 230, 1, 100, 11), f));

  // The first and the last record are found too
  CHECK(cellDbLocate(io, h, cid(CELL_GSM, 230, 1, 100, 11), f) && f.exact);
  CHECK(cellDbLocate(io, h, cid(CELL_LTE, 230, 2, 100, 11), f) && f.exact);

  // Broken files
  MemIo bad = io;
  bad.d[0] = 'X';
  CHECK(!cellDbHeader(bad, h));
  MemIo cut = io;
  cut.d.resize(cut.d.size() - 10);
  CHECK(cellDbHeader(cut, h));
  CHECK(!cellDbLocate(cut, h, cid(CELL_LTE, 230, 2, 100, 11), f));   // last record unreadable
  MemIo empty = makeDb(std::vector<CellDbRecord>(), 230);
  CHECK(cellDbHeader(empty, h));
  CHECK(!cellDbLocate(empty, h, cid(CELL_LTE, 230, 1, 100, 11), f));
}

// A real file from tools/cells/make_cell_db.py and the CSV it came from
static void testFile(const char* db, const char* csvPath) {
  FileIo io;
  io.f = fopen(db, "rb");
  CHECK(io.f != nullptr);
  if (!io.f) return;
  CellDbHeader h;
  CHECK(cellDbHeader(io, h));
  FILE* c = fopen(csvPath, "r");
  CHECK(c != nullptr);
  char line[512];
  int n = 0, found = 0;
  while (c && fgets(line, sizeof(line), c)) {
    char radio[8];
    unsigned mcc, mnc, area, cell;
    double lon, lat;
    if (sscanf(line, "%7[^,],%u,%u,%u,%u,%*[^,],%lf,%lf", radio, &mcc, &mnc, &area, &cell, &lon, &lat) != 7) continue;
    uint8_t r = !strcmp(radio, "LTE") ? CELL_LTE : !strcmp(radio, "GSM") ? CELL_GSM : !strcmp(radio, "UMTS") ? CELL_UMTS : 0;
    if (!r || mcc != h.mcc || cell == 0) continue;   // the tool leaves out cell 0
    n++;
    CellFix f;
    if (cellDbLocate(io, h, cid(r, mcc, mnc, area, cell), f) && f.exact && fabs(f.lat - lat) < 1e-5 && fabs(f.lon - lon) < 1e-5) found++;
  }
  if (c) fclose(c);
  fclose(io.f);
  printf("file: %d of %d cells found\n", found, n);
  CHECK(n > 0 && found == n);
}

int main(int argc, char** argv) {
  testClasses();
  testBest();
  testCpsi();
  testDb();
  if (argc > 2) testFile(argv[1], argv[2]);
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
