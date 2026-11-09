# Mobile data over 4G (T-Deck MAX)

The web reader (and its IRC client) can use the A7682E modem when there is no
WiFi. The ESP32 gets a real IP interface over the modem, so `HTTPClient`,
TLS and DNS work exactly as they do over WiFi.

Status: **untested on a device**, branch `feature/data-4g`.

## Using it

- Settings → **Mobile data: ON** (shown while the 4G modem is on), or the
  **Mobile data** tile in quick settings (it took the keys light's place;
  both Shifts still switch that). Off by default: data costs money. The
  setting is stored in `/sms/data.cfg`.
- The setting only *allows* data. Nothing connects until the web reader
  needs a network: it tries saved WiFi first, then mobile data, then offers
  a WiFi scan.
- The link drops after 5 minutes without traffic (1 minute while locked),
  when the web reader is left, or when the setting is turned off.
- When WiFi comes up as well, traffic goes over WiFi (higher route
  priority) and the 4G link soon drops as idle.

Debug CLI (`meck_max_ui2`):

| Command    | What it does                                             |
|------------|----------------------------------------------------------|
| `data`     | State, IP, DNS, byte counters, mux frame errors (read-only) |
| `data on`  | Bring the link up (needs the setting; uses the SIM's data) |
| `data off` | Take it down                                             |

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
- No status-bar indicator yet; the web reader's title shows `4G`.
- T-Deck Pro 4G: not enabled (`MODEM_DATA` is MAX only).

## To test on the device

Each of these uses real mobile data, so only with the user's agreement:

1. `data on` with the setting on: log shows CMUX open, `CONNECT`, an IP and
   DNS; `data` shows the counters. `data off` returns to `UART sleep enabled`.
2. Web reader with WiFi off: a page loads over 4G.
3. While data is up: an incoming SMS arrives and a call rings (URCs on
   DLC 1). If not, the A7682E may route URCs to another channel
   (`AT+CATR`).
4. Idle timeout: `pwrmodem` shows UART sleep again afterwards.
5. WiFi + 4G: pages go over WiFi, 4G drops as idle.
6. Power log (`pwrlog`) during a session and after it.
