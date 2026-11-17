# Departures on the lock screen

Settings > Experimental > **Departures on lock screen** (UI v2 builds, off by
default) adds a block of departures or of the trip that fits now above the
status line, from a [Neujede](https://github.com/halecivo/neujede) server
(Prague public transport). The server chooses what to show (live trip,
template trip, favourite stop board) and sends ready rows; the deck only
draws them. Design: "Lock screen v3" of the e-ink design system, departures.

```
ŘÍČANY, NÁDRAŽÍ                         then
[S9]  Praha hl.n.             [1]  7:41    7:45 8:15.
[381] Háje               [+2] [B]  7:28    7:28 7:43
[S9]  Benešov u Prahy    [+5] [3]  7:35    7:53 8:23
```

## Setting it up

The server address and the API token (`cli user add`, `nj_…`) are set over
the serial settings CLI and kept in NVS, never in the firmware or on the SD
card; `get transit` never prints the token:

    set transit.url https://<neujede host>     (http:// for a server on the LAN)
    set transit.token <token>
    get transit
    set transit.url                            forget it again (same for the token)

Then switch it on in Settings > Experimental. **Departures size** under it
picks S / M / L = 2 / 3 / 4 rows, and **Minutes to departure** (on by
default) whether the time column counts down or shows clock times. Without an address and a token the widget is
not shown at all.

Over plain `http://` the token goes unencrypted; use it on the home network
only.

## What it shows

Each row is in one grammar (see `api/README.md`, "Compact widget (v1)", of
Neujede):

| Part | From | Drawn as |
|---|---|---|
| line chip | `l` | inverted, Montserrat 10, at least 24 px wide; none without a line (walking) |
| text | `t` | Noto 11, one line, ellipsis |
| delay chip | `d` (minutes) or `p` (transfer probability) | outlined, `+2`, from 60 min in hours `+8 h`, `72 %`; none on time |
| platform | `pl`, `pc` | 14 × 14 box, 2 px border when the platform changed |
| time | `at`, `d` | bold. Minutes to go (`6 m`, `teď` within the minute), counted from `at` plus the delay; clock time (`7:41`, the timetable's) when it is an hour or more away, has left, or with "Minutes to departure" off |
| then | `then[]` | up to two clock times; a 2 px dot after a time = that run is 5 min or more late. An option row's arrival `arr` follows as `»7:30` |

The header is `h` in capitals; for a trip with `dep` and `arr` too
(`DO PRÁCE 6:55»7:30`). The right column says "then".

- The UI v2 fonts have no arrow: `→` in the server's texts is drawn as `»`.
- The minutes are counted by the deck from its own clock, so they move
  without a request, on the lock screen's redraw each minute for the clock.
  On a stop board every row counts down. Of a trip only the first row does
  (when to leave; while riding, when to get off): the later rides keep
  their clock times.
- **Stale mark**: when the last request failed, or the rows are older than
  their `validUntil` (and a request's worth), the right slot of the header
  shows a refresh icon and the time of the last good reply instead of
  "then". With no reply at all yet: a warning icon.
- **Empty state**: "Nic k zobrazení" (or the server's `empty` text) when the
  server has nothing (`kind: none`, no rows), when no reply came yet, and
  when the last reply is more than 2 hours old.
- **Room**: rows go before the widget does. When the clock group would get
  less than 8 px above or below, the lowest priority widget that has rows
  gives one up (down to one), then widgets are left out by priority
  (departures 15, agenda 20). The next request asks for as many rows as the
  lock screen gave last time (2-4, the server's minimum the widget asks for
  is 2).

## Requests and battery

`GET <url>/compact/widget?rows=N[&lat=…&lon=…]`, `Authorization: Bearer <token>`.

- **Only while locked**: the widget is only on the lock screen. When the deck
  locks, a request goes at once (unless one went less than 2 minutes ago);
  then at the reply's `validUntil` (taken as `validUntil − ts` from the reply,
  so the deck's clock may be a bit off), but not more often than every
  **2 minutes on WiFi, 5 minutes over 4G**, and at the latest after 4 hours.
  A reply without `validUntil` is asked again after 10 minutes.
- **Errors**: back-off 2, 4, 8, then 15 minutes; the last rows stay with the
  stale mark.
- **Redraw** only when the rows change (`key` of the reply), the stale mark or
  the empty state comes or goes; an unchanged `key` redraws nothing. The rest
  of the lock screen keeps its minute redraw for the clock.
- **Network**: WiFi when it is connected (the widget never switches WiFi on or
  off). Without WiFi, over mobile data only when it is on and the app's own
  switch (Settings > Mobile data > Departures) allows it: the link is brought
  up for the request and let go right after. Never over Bluetooth. No light
  sleep during the request.
- **Position**: `lat`/`lon` (4 decimals) only from a GPS that is on anyway and
  has a fix of the last minutes (class LOCAL, `docs/position.md`). GPS is
  never switched on for this, and the cell or advert position is not sent:
  without a fix the server picks by its own hints (Home Assistant zone, usual
  hours).

A reply is below 2 kB; with the TLS handshake a request over 4G is roughly
8-10 kB.

## Code

| File | What |
|---|---|
| `ui-new/MeckTransitUtil.h` | plain C++: row model, tolerant JSON reader, URL, when to ask, stale / empty |
| `ui-new/MeckTransit.{h,cpp}` | requests over WiFi / 4G, NVS address and token, debug CLI |
| `ui-new/LockTransitWidget.h` | drawing the block (UI v2) |
| `main.cpp` `meckTransitTick()` | GPS position, lock state, redraw on a new board |
| `test/transit/test_transit.cpp` | host tests, including Neujede's documented examples |

Build flag `MECK_TRANSIT=1` (`meck_max_ui2`). The reader takes the fields of
Neujede PR #24 and some other names for them, skips unknown fields and
refuses a reply whose `v` is not 1.

Host tests (WSL):

    wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/transit/test_transit.cpp -o /tmp/test_transit && /tmp/test_transit"

## Debug CLI (meck_max_ui2)

    transit                 address (token: set or not), the board, last request, next one
    transit fetch           a request now (while locked; needs the setting, address and token)
    transit demo [1-4]      made-up stop board with 1-4 rows (RAM only, no requests until fetch)
    transit demo trip       made-up trip with an option row
    transit demo empty      the empty state
    transit demo stale      rows with the stale mark
    transit demo offline    never a reply: empty with the warning icon
    transit clear           forget the board

Then `uigo lock` and `tools/ui2/grab_screen.py`.
