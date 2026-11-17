#pragma once
// =============================================================================
// Offline cell tower database: where a mobile network cell is, for a position
// without GPS (docs/position.md). Plain C++ (no Arduino), so test/position
// runs it on a PC.
//
// One file per country (mobile country code) on the SD card, /cells/<mcc>.bin,
// made on a computer by tools/cells/make_cell_db.py from an OpenCellID export
// (or anything in its CSV format). Records are sorted by radio, network, area,
// cell, so the deck finds one with a binary search over the file. A cell that
// is not in the file falls back to the middle of its area (LAC / TAC), with a
// radius that covers 9 in 10 of the area's known cells.
//
// File: a 32-byte header, then records of 24 bytes, little endian:
//   header  "MCDB", u16 version 1, u16 record size 24, u16 mcc, u16 0,
//           u32 records, u32 built (Unix seconds), char source[12]
//   record  u32 cell, u32 area, i32 lat, i32 lon (1e-6 degree), u16 mnc,
//           u16 range (10 m), u8 radio, 3 bytes 0
// =============================================================================

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint32_t CELLDB_MAGIC = 0x4244434Du;   // "MCDB"
static const uint16_t CELLDB_VERSION = 1;
static const uint32_t CELLDB_HEADER = 32;
static const uint32_t CELLDB_RECORD = 24;
// An area fallback reads at most this many cells (a TAC has a few hundred)
static const uint32_t CELLDB_AREA_MAX = 4000;
// Nothing is closer than this: the database only knows roughly where the
// antenna is, not where in its coverage the deck is
static const uint32_t CELLDB_MIN_RADIUS_M = 500;
static const uint32_t CELLDB_MIN_AREA_RADIUS_M = 2000;

enum CellRadio : uint8_t { CELL_NONE = 0, CELL_GSM = 1, CELL_UMTS = 2, CELL_LTE = 3 };

struct CellDbHeader {
  uint32_t magic;
  uint16_t version, recSize, mcc, pad;
  uint32_t count, built;
  char source[12];
};

struct CellDbRecord {
  uint32_t cell, area;
  int32_t lat, lon;     // 1e-6 degree
  uint16_t mnc, range10;
  uint8_t radio, pad[3];
};

static_assert(sizeof(CellDbHeader) == 32, "cell db header layout");
static_assert(sizeof(CellDbRecord) == 24, "cell db record layout");

// The serving cell, as the modem reports it
struct CellId {
  uint8_t radio;       // CellRadio
  uint16_t mcc, mnc;
  uint32_t area;       // LAC (GSM, UMTS) or TAC (LTE)
  uint32_t cell;       // CI (GSM), UTRAN cell id (UMTS), ECI (LTE)
};

struct CellFix {
  double lat, lon;
  uint32_t radiusM;
  bool exact;          // the cell itself, not the middle of its area
  uint32_t cells;      // how many cells the area fallback used
};

// Reads the file; the deck's one wraps an SD File, the tests a FILE*
struct CellDbIo {
  virtual ~CellDbIo() {}
  virtual bool read(uint32_t off, void* buf, uint32_t n) = 0;
};

static inline bool cellDbHeader(CellDbIo& io, CellDbHeader& h) {
  if (!io.read(0, &h, sizeof(h))) return false;
  return h.magic == CELLDB_MAGIC && h.version == CELLDB_VERSION && h.recSize == CELLDB_RECORD;
}

// Order of the records: radio, mnc, area, cell
static inline int cellDbCompare(const CellDbRecord& r, uint8_t radio, uint16_t mnc, uint32_t area, uint32_t cell) {
  if (r.radio != radio) return r.radio < radio ? -1 : 1;
  if (r.mnc != mnc) return r.mnc < mnc ? -1 : 1;
  if (r.area != area) return r.area < area ? -1 : 1;
  if (r.cell != cell) return r.cell < cell ? -1 : 1;
  return 0;
}

static inline bool cellDbRecordAt(CellDbIo& io, uint32_t i, CellDbRecord& r) {
  return io.read(CELLDB_HEADER + i * CELLDB_RECORD, &r, sizeof(r));
}

// Index of the first record not before the key; count when there is none.
// Returns false on a read error.
static inline bool cellDbLowerBound(CellDbIo& io, const CellDbHeader& h, uint8_t radio, uint16_t mnc,
                                    uint32_t area, uint32_t cell, uint32_t& idx) {
  uint32_t lo = 0, hi = h.count;
  CellDbRecord r;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (!cellDbRecordAt(io, mid, r)) return false;
    if (cellDbCompare(r, radio, mnc, area, cell) < 0) lo = mid + 1;
    else hi = mid;
  }
  idx = lo;
  return true;
}

// Distance in metres, good enough over tens of kilometres
static inline double cellDbDistM(double lat1, double lon1, double lat2, double lon2) {
  const double k = 111195.0;   // metres per degree on the mean Earth radius
  double dx = (lon2 - lon1) * cos((lat1 + lat2) * 0.5 * M_PI / 180.0) * k;
  double dy = (lat2 - lat1) * k;
  return sqrt(dx * dx + dy * dy);
}

// Where the cell is. False: not in this file (wrong country, no such area),
// or the file could not be read.
static inline bool cellDbLocate(CellDbIo& io, const CellDbHeader& h, const CellId& id, CellFix& out) {
  if (id.mcc != h.mcc || id.radio == CELL_NONE) return false;
  uint32_t i;
  CellDbRecord r;
  if (!cellDbLowerBound(io, h, id.radio, id.mnc, id.area, id.cell, i)) return false;
  if (i < h.count && cellDbRecordAt(io, i, r) && cellDbCompare(r, id.radio, id.mnc, id.area, id.cell) == 0) {
    out.lat = r.lat / 1e6;
    out.lon = r.lon / 1e6;
    uint32_t range = (uint32_t)r.range10 * 10;
    out.radiusM = range < CELLDB_MIN_RADIUS_M ? CELLDB_MIN_RADIUS_M : range;
    out.exact = true;
    out.cells = 1;
    return true;
  }

  // Not known: the middle of the cells of its area, and a radius that takes
  // in all of them and their reach
  uint32_t first;
  if (!cellDbLowerBound(io, h, id.radio, id.mnc, id.area, 0, first)) return false;
  double sLat = 0, sLon = 0;
  uint32_t n = 0;
  for (uint32_t k = first; k < h.count && n < CELLDB_AREA_MAX; k++, n++) {
    if (!cellDbRecordAt(io, k, r)) return false;
    if (r.radio != id.radio || r.mnc != id.mnc || r.area != id.area) break;
    sLat += r.lat / 1e6;
    sLon += r.lon / 1e6;
  }
  if (n == 0) return false;
  out.lat = sLat / n;
  out.lon = sLon / n;
  // Radius: 9 in 10 of the area's cells (with their reach) are within it.
  // Not the farthest one: the data has the odd cell 100 km off its area.
  // Kilometre buckets, the last one for everything beyond.
  static const int BUCKETS = 256;
  uint16_t hist[BUCKETS];
  memset(hist, 0, sizeof(hist));
  for (uint32_t k = first; k < first + n; k++) {
    if (!cellDbRecordAt(io, k, r)) return false;
    double d = cellDbDistM(out.lat, out.lon, r.lat / 1e6, r.lon / 1e6) + (double)r.range10 * 10;
    uint32_t b = (uint32_t)(d / 1000);
    hist[b < BUCKETS ? b : BUCKETS - 1]++;
  }
  uint32_t want = (n * 9 + 9) / 10, seen = 0, km = BUCKETS;
  for (int b = 0; b < BUCKETS; b++) {
    seen += hist[b];
    if (seen >= want) { km = b + 1; break; }
  }
  uint32_t radius = km * 1000;
  out.radiusM = radius < CELLDB_MIN_AREA_RADIUS_M ? CELLDB_MIN_AREA_RADIUS_M : radius;
  out.exact = false;
  out.cells = n;
  return true;
}

// "+CPSI: LTE,Online,230-01,0x1A2B,26845953,..." (SIMCom A76xx) to a cell.
// GSM and WCDMA have the same first five fields (LAC instead of TAC);
// anything else ("NO SERVICE", NR) gives false.
static inline bool cellParseCpsi(const char* s, CellId& id) {
  const char* p = strstr(s, "+CPSI:");
  if (!p) return false;
  p += 6;
  while (*p == ' ') p++;
  memset(&id, 0, sizeof(id));
  if (strncmp(p, "LTE,", 4) == 0) id.radio = CELL_LTE;
  else if (strncmp(p, "GSM,", 4) == 0) id.radio = CELL_GSM;
  else if (strncmp(p, "WCDMA,", 6) == 0) id.radio = CELL_UMTS;
  else return false;
  // Fields: system, operation mode, "mcc-mnc", area (hex), cell (decimal)
  const char* f[5];
  int nf = 0;
  f[nf++] = p;
  for (const char* q = p; *q && *q != '\r' && *q != '\n' && nf < 5; q++)
    if (*q == ',') f[nf++] = q + 1;
  if (nf < 5) return false;
  char* e;
  unsigned long mcc = strtoul(f[2], &e, 10);
  if (*e != '-') return false;
  unsigned long mnc = strtoul(e + 1, &e, 10);
  if (*e != ',') return false;
  unsigned long area = strtoul(f[3], &e, 16);   // "0x1A2B"; strtoul takes the 0x
  if (*e != ',') return false;
  unsigned long cell = strtoul(f[4], &e, 10);
  if (*e != ',' && *e != '\r' && *e != '\n' && *e != 0) return false;
  if (mcc == 0 || mcc > 999 || mnc > 999 || cell == 0) return false;
  id.mcc = (uint16_t)mcc;
  id.mnc = (uint16_t)mnc;
  id.area = (uint32_t)area;
  id.cell = (uint32_t)cell;
  return true;
}

static inline bool cellIdEqual(const CellId& a, const CellId& b) {
  return a.radio == b.radio && a.mcc == b.mcc && a.mnc == b.mnc && a.area == b.area && a.cell == b.cell;
}
