# GPS while the deck sleeps (options)

Today, GPS on is a sleep blocker (`MECK_SLEEP_GPS` in `meckLockSleepBlocker()`):
with GPS switched on, the locked deck never enters light sleep. This page
lists the ways the GPS could keep working while the ESP32 sleeps and compares
their average current with today's locked sleep. Nothing here is implemented
yet. Figures marked "est." are computed from the datasheet and our own power
log, not measured with the GPS on.

## The hardware

From the schematic (T-Deck Pro Max V0.1 25-09-11, pages 1 and 3) and the
u-blox datasheet / integration manual in the LilyGo repository:

- **Receiver: u-blox MIA-M10Q** (M10, UBX protocol). VCC and V_IO both on
  `GPS_VDD` (3.3 V, VIO_SEL open).
- **`GPS_VDD` comes from an RT9080 LDO** enabled by `GPS_EN` (XL9555 IO02).
  The LDO is linear, so the battery sees the GPS current one to one. With
  `GPS_EN` low the LDO is off (well under 1 uA).
- **V_BCKP is on `VRTC`**: a rechargeable MS621FE coin cell, charged from
  `VDD3V3` through a 1N4148 and 1 kOhm. So the GPS has a working
  **hardware backup mode**: with `GPS_VDD` off it keeps its battery-backed
  RAM (ephemeris, almanac, last position, configuration saved to the BBR
  layer) for about 28 uA.
- **No 32 kHz RTC crystal** (RTC_I open, RTC_O to GND, the "RTC not used"
  layout). Without it the receiver loses time in backup, and the manual says:
  - power save **on/off mode (PSMOO) is not supported**,
  - **software standby with a timed wake is not supported**,
  - time has to come from the host at each start (coarse time, accurate to
    seconds, via `UBX-MGA-INI-TIME_UTC`) to use the stored orbits.
- **EXTINT is not connected**, so no external control of the power save
  mode and no accurate (sub-second) time aiding.
- **TIMEPULSE goes to IO1** (`PIN_GPS_PPS`), UART on IO2 / IO16.

Side note: the `$PCAS04` and `$PMTK869` sentences sent at boot are commands
for CASIC and MediaTek receivers; the MIA-M10Q ignores them. Its default
constellations are GPS + Galileo + BeiDou B1I (+ QZSS, SBAS). Changing them
needs `UBX-CFG-VALSET` (`CFG-SIGNAL-*`).

### GPS currents (datasheet, 3.0 V, default constellations)

| State | VCC | V_IO | Total |
|---|---|---|---|
| Acquisition (start until first fix) | 13 mA | 2.4 mA | about 15.4 mA |
| Tracking, continuous, 1 Hz | 10.5 mA | 2.4 mA | about 12.9 mA |
| Tracking, power save cyclic (PSMCT), 1 Hz | 5.5 mA | 2.1 mA | about 7.6 mA |
| Software standby (UART wake) | 0.1 uA | 46 uA | about 46 uA |
| Hardware backup (`GPS_VDD` off, V_BCKP only) | - | - | about 28 uA |

Time to first fix: cold start 27 s; hot start 1 s (needs accurate time).
With orbits kept in BBR and only coarse time from the host, expect a few
seconds to about 10 s under open sky (the receiver still has to decode the
exact time from the satellites, about 6-7 s). Ephemeris is good for about
4 hours for hot starts, a few days for warm starts.

## Reference points (measured)

| State | Current | Note |
|---|---|---|
| Locked, asleep, GPS off | 17.4 mA | night power log, 97 % asleep |
| Light sleep floor | about 13 mA | between wakes |
| Locked and awake | about 35 mA | so a wake costs about +22 mA |

Runtime below assumes about 1350 mAh usable (17.4 mA gave 75-80 h).

## Options

### A. Today: GPS on blocks sleep

ESP32 awake (35 mA) plus continuous GPS (12.9 mA): **about 48 mA (est.)**,
about 28 h. No code needed, but this is what the lock warning (blocker GPS)
tells the user about.

### B. GPS runs continuously, the ESP32 sleeps

Keep `GPS_VDD` on and drop GPS from the sleep blockers. The receiver tracks
on its own; the ESP32 does not get the NMEA stream while asleep (the UART is
clock-gated and its FIFO is small), so on the minute wake it stays up about
1.2 s to read one fresh RMC + GGA pair.

- Current: 17.4 + 12.9 + about 0.4 (longer minute wake) = **about 31 mA
  (est.)**, about 44 h.
- Position is never older than a minute; at unlock the fix is there at once.
- Effort: small. Remove the `MECK_SLEEP_GPS` blocker (keep it as a warning
  icon), extend the timer wake while GPS is on, make sure UART RX is not a
  light sleep wake source.
- Optional: output only RMC and GGA (`CFG-MSGOUT-*`) to cut parsing and V_IO
  load a little.

### C. Cyclic tracking (PSMCT) plus ESP32 sleep

Like B, but the receiver is switched to power save cyclic tracking with
`UBX-CFG-VALSET` (`CFG-PM-OPERATEMODE` = PSMCT, SBAS off as the manual
recommends; BeiDou B1C is not supported in this mode, B1I is fine). It
tracks in short bursts and idles between fixes; update periods of 1-10 s.

- Current: 17.4 + 7.6 + 0.4 = **about 25 mA (est.)** at 1 Hz, about 53 h.
  A longer PSMCT period (up to 10 s) should be lower; the datasheet gives
  only the 1 Hz figure, so it needs a measurement.
- Under poor signal (pocket, indoors) the receiver falls back to tracking or
  acquisition, i.e. towards B.
- Effort: medium. A small UBX writer (frame + checksum) and the config keys,
  sent after each power-on (RAM layer) or once to the BBR layer.
- Fits a "tracking" use (trail logging, live position) better than plain
  position updates.

### D. Duty-cycled GPS: power on every N minutes (recommended)

Between fixes `GPS_VDD` is off and the receiver sits in hardware backup on
the coin cell / `VDD3V3` (28 uA), keeping its orbits. Every N minutes (on a
minute wake):

1. `gpsPowerOn()` (LDO on, UART pins back),
2. send coarse time from the deck clock (`UBX-MGA-INI-TIME_UTC`, accuracy
   a few seconds) and optionally the last position (`UBX-MGA-INI-POS_LLH`),
3. wait for a valid fix (plus a couple of seconds to settle), store it,
   sync the clock,
4. `gpsPowerOff()` (LDO off, UART pins to inputs: the existing back-powering
   fix stays essential).

The deck clock is corrected at every fix, so it stays within a couple of
seconds for the next start even with the RC clock drift.

Keeping the ephemeris fresh: short on-times do not always download complete
ephemeris (a full set takes about 18-30 s of continuous tracking). So once
the last long fix is more than about 2 hours old, stay on 30-40 s longer.
Without that the starts slowly degrade towards 25-30 s after about 4 hours.

Cost per fix, ESP32 awake the whole time (10 s at 15.4 + 22 mA): about
374 mAs. Ephemeris refresh: about 30 s x 37 mA every 2 h = +0.16 mA.

| Interval | Average (est.) | Runtime |
|---|---|---|
| 30 min | about 17.8 mA | about 76 h |
| 10 min | about 18.2 mA | about 74 h |
| 5 min | about 18.8 mA | about 72 h |
| 2 min | about 20.7 mA | about 65 h |
| 1 min | about 24 mA | about 56 h (here C is as good and fresher) |

Refinement **D2**: the ESP32 does not need to watch the start. Power the GPS
on, send the aiding, go back to light sleep for about 8 s (a timer wake on
top of the LoRa and button wakes; the XL9555 keeps `GPS_EN` high), then wake
and read up to a few seconds. Awake time drops to about 2.5 s per fix: about
210 mAs, i.e. **about 17.8 mA at 10 min, 19.2 mA at 2 min** (the ephemeris refresh can run with the ESP32 asleep too).

No sky (deck in a bag, indoors): the start fails. Give up after about 45 s
(about 1700 mAs, +2.8 mA at a 10 min interval) and back off (double the
interval after each failure, up to 60 min), so a deck in a drawer stays
near the 17.4 mA floor. Keep the last good fix and its age for the UI.

- Effort: medium. A small state machine in the locked loop next to
  `meckLockSleepStep()`, the UBX writer from C (only two MGA messages),
  a setting for the interval.
- Also the base for "fix on demand": power the GPS only before an advert or
  telemetry that carries the location, and while the map screen is open.

### E. Software standby instead of switching the LDO

The same cycle as D, but the GPS stays powered and goes to software standby
with `UBX-RXM-PMREQ` (force flag, wake on UART RX). The next byte sent on the
UART wakes it.

- Current: 46 uA instead of 28 uA, otherwise as D (time is still lost, as
  there is no RTC; a timed wake is not possible).
- Avoids switching the LDO and the 100 mA inrush at each start, but the
  ESP32 TX line has to stay high through light sleep (any edge wakes the
  GPS) and the UART pins stay connected to a powered chip.
- Worth it only if switching the rail turns out to cause trouble; D is
  simpler with the code we have.

### Not possible on this board

- Power save on/off mode (PSMOO) with the receiver waking itself every N
  minutes: needs the 32 kHz RTC crystal.
- Software standby with a timed wake: needs the RTC too.
- EXTINT-driven power save or accurate time aiding: EXTINT is not wired.

## Comparison

| Option | Locked average (est.) | Runtime | Position age | Effort |
|---|---|---|---|---|
| Today, GPS off | 17.4 mA (measured) | 75-80 h | no GPS | - |
| A. today, GPS on (no sleep) | about 48 mA | about 28 h | live | none |
| B. GPS continuous, ESP32 sleeps | about 31 mA | about 44 h | up to 1 min | small |
| C. PSMCT + ESP32 sleeps | about 25 mA | about 53 h | up to 1 min | medium |
| D. power on every 10 min | about 18.2 mA | about 74 h | up to 10 min | medium |
| D2. as D, ESP32 sleeps during the start | about 17.8 mA | about 76 h | up to 10 min | medium |
| D2 every 2 min | about 19.2 mA | about 70 h | up to 2 min | medium |
| E. software standby every 10 min | as D | as D | up to 10 min | medium |

## Suggested plan

1. **B first**: small and already a large win (48 to 31 mA), and it moves
   the GPS from the blockers into the normal sleep loop.
2. **D / D2 as the default**: a setting "GPS while locked: off / every
   30 min / every 10 min / every 2 min / continuous (B)". On unlock the GPS
   goes back to continuous; thanks to the stored orbits the first fix comes
   in seconds.
3. **C** only if a continuous trail (tracking) is wanted while locked.
4. Measure each with the power log (`pwrlog`; the `gps` column is already
   there) on battery, outdoors and indoors, and add the per-fix TTFF and
   the number of failed starts to the log so the estimates above can be
   replaced with measured numbers.
5. Side fix: replace the `$PCAS04` / `$PMTK869` boot sentences with UBX, or
   drop them.
