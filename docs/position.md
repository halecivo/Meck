# Position sources and accuracy classes

The deck can know where it is from more than one source, and they differ a
lot in how good they are. Apps do not pick a source; they ask for an
**accuracy class** and get the best position that is good enough, or none.

Code: `ui-new/MeckPosition.h` (plain C++), fed by `meckPositionTick()` in
`main.cpp`.

## Classes

| Class | Radius | For |
|---|---|---|
| `POS_FINE` | ≤ 100 m | the map's own marker, sharing the position (`[LOC]`), adverts, SOS |
| `POS_LOCAL` | ≤ 2 km | nearby things: stops, points of interest |
| `POS_AREA` | ≤ 50 km | weather |
| `POS_REGION` | ≤ 500 km | country, time zone |

A position belongs to the best class its radius fits in, and it can serve
any app that asks for that class or a worse one. A GPS fix serves the weather,
but the cell never serves the map.

## Sources

| Source | Radius | Time |
|---|---|---|
| **GPS** | 25 m | a fix while the GPS is on and has one |
| **Cell** (`HAS_4G_MODEM`) | the cell's range from the database, at least 500 m. A cell missing from the database gives the middle of its area (LAC / TAC), with a radius that takes in 9 in 10 of the area's cells and their reach, rounded up to the kilometre, at least 2 km | when the modem last read the serving cell |
| **Stored** | counts as 100 m | none: the advert position, set by hand or the last GPS fix |

**Ageing:** a position gets worse as it gets older, by 15 m/s (a car in town).

- A GPS fix is FINE only while new fixes keep coming. Ten minutes after the
  last one it is still good enough for the weather.
- A cell position does not age for 15 minutes after the modem read the cell:
  had the deck moved to another cell, the next read would show it. It is
  renewed every time the modem reads the same cell again.

**Which one wins** (`MeckPosition::best(class)`):
1. The live source (GPS, cell) with the smallest radius *now*, if that fits
   the class.
2. Otherwise the stored position, if the class allows 100 m. Every class does
   by default; an app can pass `allowStored = false`.

So the weather takes GPS, then the cell, and the stored position only as the
last resort. "Send my location" and adverts take GPS, then the stored
position, never the cell.

### Reading the cell

`AT+CPSI?` gives the serving cell's network (MCC, MNC), area and cell id. The
modem task reads it:

- every 5 minutes while the deck is unlocked;
- while locked, only when an app asks for a class worse than FINE and has no
  good GPS fix (`meckPositionWant()`), at most every 15 minutes. Each read
  wakes the modem from UART sleep for a moment.

GSM, UMTS and LTE are supported. The modem has no 5G.

## Offline cell database

One file per country (mobile country code) on the SD card: `/cells/<mcc>.bin`,
e.g. `/cells/230.bin` for Czechia and `/cells/226.bin` for Romania. When the
deck roams into a country it has no file for, the cell gives no position.

**Making the files** on a computer, from an [OpenCellID](https://opencellid.org)
export. A free account gives an API key and one CSV per country:

    py -3 tools/cells/make_cell_db.py --token <key> --mcc 230 --mcc 226 --out cells/
    py -3 tools/cells/make_cell_db.py 230.csv.gz --out cells/     # a file you already have

Then copy `cells/*.bin` to `/cells/` on the SD card. Any CSV in the OpenCellID
column order works (`radio,mcc,net,area,cell,unit,lon,lat,range,...`), so
another source can be used the same way. OpenCellID data is CC BY-SA 4.0.

**Format** (`ui-new/MeckCellDb.h`):
- a 32-byte header: `MCDB`, version 1, record size, MCC, count, build time,
  source name;
- then 24-byte records sorted by radio, MNC, area, cell, so the deck finds a
  cell with about 20 reads (binary search).

Czechia from OpenCellID (November 2026): 42,762 cells, 1.0 MB. A country of a few
hundred thousand cells is a few MB.

OpenCellID does not know every cell. On the test deck the serving LTE cell was
missing, and its TAC (335 known cells) gave a 31 km radius: AREA, so good
enough for the weather. 9 in 10 rather than all cells, because the data has
the odd cell placed 100 km away from the rest of its area.

## Debug CLI (meck_max_ui2)

    pos           every source with its radius now and class, the serving
                  cell and what the database said, the best source per class
    pos cell      read the serving cell now (then "pos" again)

## Tests

`test/position/test_position.cpp` (WSL): classes, ageing, which source wins,
`+CPSI` parsing, lookups and the area fallback. With a file from the tool and
its CSV as arguments it also checks that every cell of the CSV is found:

    wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/position/test_position.cpp -o /tmp/test_position && /tmp/test_position"
