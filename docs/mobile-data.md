# Mobile data over 4G (T-Deck MAX)

Everything on the deck that uses the internet over WiFi can use the A7682E
modem when there is no WiFi: the web reader, its IRC client and Home
Assistant updates. The ESP32 gets a real IP interface over the modem, so
`HTTPClient`, TLS and DNS work exactly as they do over WiFi. What only works
inside the home network does not (the WiFi companion's app server, Home
Assistant pairing).

Status: tested on a T-Deck MAX: the link, the browser, SMS/calls beside
it, the per-app switches and usage screen, a Home Assistant update over 4G
and the status icon. Not yet: IRC over 4G, the browser's switch off.
Branch `feature/data-4g`.

## Using it

- Settings → **Mobile data: ON** (shown while the 4G modem is on), or the
  **Mobile data** tile in quick settings (it took the keys light's place;
  both Shifts still switch that). Off by default: data costs money.
- Settings → **Mobile data usage**: each app has its own 4G switch (on
  unless turned off) and shows what it used over 4G in the last 24 hours;
  **Other** is traffic that belongs to no app (debug CLI), **Total** all of
  it. WiFi traffic is not counted. The counts start at boot and are lost on
  a restart; "24 hours" is the current hour plus the 23 before it.
- Both are stored in `/sms/data.cfg`: the master switch, then one `0`/`1`
  per app (browser, IRC, Home Assistant); an older one-character file means
  every app allowed.
- The setting only *allows* data. Nothing connects until an app needs a
  network: the web reader tries saved WiFi first, then mobile data, then
  offers a WiFi scan; IRC asks when it connects; Home Assistant when an
  update is due.
- Each app holds the link while it needs it. The link drops when no app
  holds it any more, after 5 minutes without traffic (1 minute while
  locked), or when the setting is turned off. Leaving the web reader lets
  go of it for the browser and IRC; Home Assistant lets go after each update.
- When WiFi comes up as well, traffic goes over WiFi (higher route
  priority) and the 4G link soon drops as idle.

### For app code

`ui-new/MeckNet.h`: `meckNetUp(app)` (WiFi, or 4G allowed for the app and
up), `meckNetRequest(app)` / `meckNetRelease(app)` (hold the link, no
blocking), `meckNetWaitUp(app, ms)` (blocking, for a user action),
`MeckNetUse use(app)` around a request (counts its 4G bytes to the app) and
`meckNetSaveData()`: true while the traffic goes over 4G, so an app can
keep it small. A new app gets a `DataApp` value in `ui-new/MeckDataUse.h`
and a row in the settings screen.

Usage is counted per PPP frame in `ModemManager::noteDataTraffic()`: to the
app tagged by `MeckNetUse`, else the only app holding the link (so its link
setup counts to it), else IRC (the one connection that gets data by itself),
else Other. Unit tests: `test/datause`.

Debug CLI (`meck_max_ui2`):

| Command    | What it does                                             |
|------------|----------------------------------------------------------|
| `data`     | State, app switches and holders, 24 h usage per app, IP, DNS, byte counters, mux frame errors (read-only) |
| `data on`  | Bring the link up (needs the setting; uses the SIM's data) |
| `data off` | Take it down (lets go of every app's hold)               |

## How it works

```
ESP32 UART ── ModemMux (AT+CMUX, GSM 07.10 basic mode)
                ├─ DLC 0  control (open/close, modem status)
                ├─ DLC 1  AT commands + URCs  → ModemAtPort → ModemManager
                └─ DLC 2  ATD*99# → PPP       → esp_netif PPP → lwIP
```

- `ModemMux` (`examples/companion_radio/ui-new/ModemMux.*`) frames the UART.
  `ModemAtPort` is a `Stream` that is DLC 1 while the mux is on and the
  plain UART otherwise, so the SMS, call and tone code did not change.
- `ModemManager` (`MODEM_DATA`, MAX builds only) runs the data link as a
  state machine beside the call/SMS state (`DataState`):
  1. `AT+CSCLK=0`: no UART sleep while PPP runs (DTR stays low, the CPU
     holds a PM lock, so no light sleep either).
  2. `AT+CGATT?` (attach if needed), `AT+CMUX=0,0,5,127` (falls back to
     `AT+CMUX=0`, N1 = 31), open DLC 0–2, send modem status.
  3. DLC 1 is a new AT port: `ATE0`, `AT+CMGF=1`, `AT+CNMI`, `AT+CLIP`.
  4. `ATD*99#` on DLC 2, wait for `CONNECT`, start the esp_netif PPP
     interface; up when `IP_EVENT_PPP_GOT_IP` arrives.
  5. Down: PPP terminate, close DLC 2/1, CLD on DLC 0, check `AT` on the
     plain UART, `AT+CSCLK=1` again. If the modem does not answer it is
     power cycled.
- The APN is the one ModemManager already sets with `AT+CGDCONT`
  (user setting → network → IMSI database).
- The UART driver ring is 4 KB (about 350 ms at 115200 baud) and the modem
  task loops every 5 ms while data runs.

## Limits

- 115200 baud, no RTS/CTS on the MAX: about 10 kB/s at best.
- The main loop blocks for up to 60 s while the web reader waits for the
  link (as it already does for WiFi connects and fetches).
- Status header (UI v2): an up/down arrows icon next to the WiFi one while
  traffic goes over 4G (the link is up and WiFi not joined, even with the
  WiFi radio on). The classic UI has none; the web reader's title shows `4G`.
- T-Deck Pro 4G: not enabled (`MODEM_DATA` is MAX only).

## To test on the device

Each step uses real mobile data; they are ordered from the cheapest. The
`data` counters (`ppp rx/tx`) show what each step cost. Nothing on the deck
sends over PPP by itself (no LCP echo, SNTP or mDNS) except a paired Home
Assistant every 15 minutes when it has 4G allowed, so an idle link costs
nothing until the idle timeout drops it.
Carriers may round each session up (often to 1 kB or more), so keep to few
sessions.

1. Setting on, WiFi off, `data on`: the log shows CMUX open, `CONNECT`, an
   IP and DNS. Only PPP negotiation (well under 1 kB). `data`, then
   `data off`: back to `UART sleep enabled`, `pwrmodem` confirms.
2. `data on`, `data dns example.com`: one DNS lookup (~100 B).
3. `data get http://example.com/`: a 1.3 kB page, first 256 bytes read.
   `data off`.
4. While data is up (no traffic needed): an incoming SMS arrives and a call
   rings (URCs on DLC 1). If not, the A7682E may route URCs to another
   channel (`AT+CATR`).
5. Idle timeout: leave it up with nothing running, it drops after 5 min (1
   min locked); `pwrmodem` shows UART sleep again.
6. Only then the web reader with WiFi off, on a light page (e.g.
   `http://example.com/`; DuckDuckGo lite results are ~20 kB).
7. WiFi + 4G: pages go over WiFi, 4G drops as idle.
8. Mobile data usage screen with the setting off: switches flip and are kept
   after a restart (`data` shows `apps`), usage shows 0 B. No data used.
9. Browser's 4G switch off, WiFi off: the web reader does not bring the link
   up. No data used.
10. Home Assistant (paired, WiFi off, `ha post`): `HA: update due, bringing
    up mobile data`, `HA: update 200 via cloudhook (4G)`, the link closes
    right after; the usage screen shows the bytes under Home Assistant. One
    session; the TLS handshake is most of it. Measured: 10.5 kB per update
    to an external https address (7.9 kB in, 2.6 kB out, link up to closed
    in 8 s), so about 1 MB a day at the 15-minute interval.
