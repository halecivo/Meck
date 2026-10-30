# Lower power use on the locked screen (ideas)

Where the T-Deck MAX stands with "Power saving when locked" on, and what
could bring the draw down further. Nothing here is implemented yet; each
item says what it would need and what it might save.

## Where it stands

From the power log of one night on battery (10.7 h, 99 % to 85 %):

| State | Current | Note |
|---|---|---|
| Locked, asleep 97 % of the time | 17.4 mA | about 75-80 h standby |
| Locked, asleep, GPS on with a fix | 31.7 mA | 47 min by a window on battery (branch gps-lock-sleep): blocker "can sleep", asleep 59.3 s of 60, one timer wake a minute, awake time as without GPS; the GPS costs about 15 mA. Whether the position was updated is not confirmed: the log has no fix or position field |
| Light sleep floor | about 13 mA | between wakes |
| Locked and awake (a blocker holds it) | about 35 mA | e.g. on USB |
| Locked with a phone connected over BLE | about 100 mA | blocker PHONE, never sleeps; 97 mA with the relaxed interval |
| Locked, phone syncs "Every 10 min" | 23.4 mA | sync windows, see 2 below |
| Before power-fixes (locked, idle) | about 82 mA | for comparison |

About 470 LoRa wakes per hour. Each one keeps the ESP32 up for at least
two loop passes.

## 1. Shorter wake after a LoRa packet

After a packet the device stays awake for at least two loop passes, and
each locked pass ends in `delay(50)`, so a wake costs 100 ms or more at
about 35 mA. At 470 wakes per hour that is about 13 s per hour awake.

- Skip the `delay(50)` while `meckRxPasses >= 0` (a packet is being
  handled) and go back to sleep as soon as `meckRxHandled()` says so.
- Check `meckRxHandled()` right after `the_mesh.loop()` instead of on the
  next pass.

Expected saving: about 1-2 mA on average. Low effort, low risk (the 2 s
upper bound stays).

## 2. Phone connected over BLE while locked (the biggest item)

Today a connected phone is a sleep blocker: the Bluetooth controller cannot
sleep on this framework, so the ESP32 stays awake at about 100 mA. Most of
that is the CPU at 240 MHz looping and the controller servicing a 15-20 ms
connection interval that the deck requests after pairing.

Steps, in order of effort:

1. **Longer connection interval while locked.** On lock, request a relaxed
   interval from the phone, as the existing `MECK_BLE_SLOW_INTERVAL` watch
   build does at connect time (100-200 ms, slave latency 2, 6 s supervision
   timeout); on unlock, request 15-20 ms again for a fast sync.
   `esp_ble_gap_update_conn_params()` can be called at any time on an open
   link. The phone decides in the end: Android usually accepts, iOS keeps
   intervals in its own allowed range (15 ms multiples, latency limits) and
   may round the request. Saves radio time on both sides; on its own it
   saves little on the deck while the CPU still runs flat out.
2. **Slower CPU while locked with a phone.** `setCpuFrequencyMhz(80)` on
   lock (back to 240 on unlock). BLE works at 80 MHz; WiFi does too, but
   slower. Expected: tens of mA.
3. **Let the CPU idle instead of polling.** The locked loop with a phone
   runs `delay(50)` passes; with automatic light sleep (ESP-IDF power
   management, `esp_pm_configure` with `light_sleep_enable`) the CPU would
   sleep between BLE events. This needs the Bluetooth controller's modem
   sleep (`CONFIG_BTDM_CTRL_MODEM_SLEEP` / `CONFIG_BT_CTRL_MODEM_SLEEP`)
   and a low-power clock it can keep time with. The MAX has no 32 kHz
   crystal, so it would have to use the main crystal in sleep (supported on
   the S3, costs about 1 mA more than with a 32 kHz crystal) or the
   internal 150 kHz RC clock (too inaccurate for long intervals). Both need
   a custom sdkconfig, i.e. an ESP-IDF build of the Arduino core or
   pioarduino with `custom_sdkconfig`. Expected: the deck with a phone
   connected near the sleep floor, maybe 20-25 mA. High effort.
4. **NimBLE instead of Bluedroid.** Less RAM (helps WiFi + BLE too) and
   better tested with modem sleep. A large change (see the WiFi + BLE
   analysis).

Until step 3 exists, a phone connected while locked keeps the deck awake;
the lock warning says so (blocker PHONE).

### Implemented (branch `ble-locked-relaxed`)

- **Relaxed interval while locked (2.1)**, now on its own branch
  `ble-relaxed-interval`, to come back with step 3. With power saving on,
  locking asks the phone for 100-150 ms, slave latency 4, 6 s timeout;
  unlocking asks for 15-20 ms again. Measured below, it saved nothing
  while the deck stays awake, and the link dropped soon after locking a
  few times, so it is out of this branch until it can be tested on its
  own. `pwrlink` on the debug CLI and `link_ms` still show the interval the
  phone accepted. Step 2.2 was already in place: `CPUPowerManager` runs
  the CPU at 80 MHz while locked.
- **Sync windows.** Settings > "Phone when locked" (shown with power
  saving on): *Stay connected* (as before) or *Every 5 / 10 / 15 / 30 / 60 min*.
  With a window period set, a connected phone no longer keeps the locked
  deck awake. The deck sleeps with Bluetooth off and opens a window on
  every Nth clock minute, on each new message the user is notified of
  (muted channels don't count), and on locking while a phone is connected.
  A window advertises for up to 30 s. Once the app has reconnected, the
  window lasts while frames flow and ends 8 s after the link goes quiet,
  2 min at most. Then Bluetooth goes off again, which drops the link.
  Messages wait in the offline queue in the meantime. The blocker shows
  as "Phone sync" (power log blocker 9). The power log has the setting
  (`ble_win`), the windows per minute and how many the app reconnected in
  (`sync_win`, `sync_link`), the time in windows (`sync_s`) and the
  accepted interval (`link_ms`).
- **Reconnect windows for "Stay connected".** A connected phone still keeps
  the locked deck awake, but once the link is gone while locked (or no
  phone was connected at lock) the deck no longer stays dark until unlock.
  It opens a window at once, then at every minute wake for 5 min, every
  5 min up to an hour, then every 15 min, and on each new message as with
  sync windows. When the phone is back the windows stop and the link keeps
  the deck awake again. Only with Bluetooth switched on. A window that finds
  no phone costs 30 s awake with advertising, so an hour away is about 16
  windows, 1-2 mA on average. The power log has `app_rx`, frames the app
  sent in the minute (a window with `sync_link` but no `app_rx` is iOS
  reconnecting without the app talking), and `ble_drop`, why a link ended.

Expected: a window costs about 40 mA while it lasts. Every 5 min with a
reconnect in ~10 s is about +1.5 mA, a window without a reconnect is
+2.7 mA. That gives about 19-20 mA on average instead of 100 mA.

To check on the device:
1. Does the app reconnect on its own within the 30 s window, on Android
   and iOS, with the app in the background? If it does not, the windows
   only cost power.
2. `pwrlog`: the average with "Every 5 min" and "Every 15 min", with a
   phone that was connected at lock time.
3. `pwrlink` while locked with "Stay connected": the interval the phone
   accepted, and how much the relaxed link saves on its own.

### Measured (2026-10-30, iPhone, on battery)

Power log of one morning: "Stay connected" 09:35-10:37 and 12:15-12:20,
"Every 10 min" 11:29-12:13 (deck clock). The sync window columns and
`link_ms` came out as zeros: the firmware filled them in after the record
had been stored (fixed since), so windows are read from blocker 9 and the
phone flag instead.

| Locked, power saving on | Mean | Awake |
|---|---|---|
| Phone connected, "Stay connected" (35 min) | 97 mA | 100% |
| Phone gone, Bluetooth paused, asleep (68 min) | 13.7 mA | 1% |
| "Every 10 min" (45 min, 5 windows) | 23.4 mA | 13% |
| (the same between windows) | 14.7 mA | 4% |

- **The relaxed interval saves nothing that shows.** 97-99 mA locked with
  the phone against about 100 mA before it, and 100-110 mA unlocked. The
  deck stays awake with the controller running either way; only step 3
  (sleep between BLE events) would change that. Whether iOS accepted the
  100-150 ms is unknown (`link_ms` was not logged).
- **"Stay connected" loses the phone and does not get it back.** The link
  held 35 min after the first lock, but later dropped 45-60 s after
  locking (10:11, 10:34, 10:37) and once after 5 min (12:20). The reason is
  not in the log. Once the link is gone nothing blocks sleep, so the deck
  pauses Bluetooth (no advertising) until it is unlocked: the app cannot
  reconnect while the deck is locked, which is what the user saw. After
  each unlock the app was back within the minute.
- **Sync windows work with iOS.** The app reconnected in the windows at
  11:30, 11:50, 12:00 and 12:10, and in the window a received message
  opened at 11:52. At 11:39 the deck was awake only about 10 s, so the app
  either connected and had nothing to sync or did not come at all (the
  sync columns would say). A window with the app is about 60-70 s at
  ~98 mA, longer than the 10-20 s expected: about 1.6 mAh, or +1 mA on
  average at 10 min, +2 mA at 5 min. Messages arrive with up to 10 min
  delay, as the user reported.
- **Wakes.** Locked and asleep the deck woke once a minute for the clock
  and on the few LoRa packets; no button wakes it did not get from the
  user. No resets during the day. After the flash at 22:52 the clock read
  May 2024 until the GPS set it at 05:55 (the NVS clock backup does not
  cover a flash), so those records sit in `20240515.csv`.

Second run (2026-10-30 11:46-12:49, without the relaxed
request, "Stay connected"): the link held the whole hour, locked, with no
drop. iOS settled on a 30 ms interval (`link_ms`), not the 15-20 ms the
deck asks for. Current 97-101 mA, the same as with the relaxed request, so
that request bought nothing and probably caused the drops.

The battery fell 99 % to 93 % in 50 min (7.2 %/h), which is what 100 mA
takes from 1400 mAh (7.1 %/h); the gauge's percentage and current agree.
At 93 % it held 1301 of 1400 mAh: about 13 h at 100 mA. The Battery
page's "Remaining" is the gauge's TimeToEmpty, remaining capacity over its
average current, and the average rises while the deck is unlocked to look
at it (display, backlight, keyboard light), so it reads lower than the
locked drain gives.

Third run (2026-10-30 afternoon, iPhone, reconnect windows,
with `app_rx` and `ble_drop`):
- **The app does talk after iOS reconnects it in the background**: 6-85
  frames in the minute after a reconnect in a window.
- **A link with no app behind it.** Once, iOS reconnected but the app
  never talked, and opening the app did not help: the deck saw a phone,
  stopped advertising, and the app could not find it until Bluetooth was
  switched off and on. Now a link with no frame from the app
  within 30 s is dropped (shows as `ble_drop` 22) and advertising resumes.
- **The link drops every 1-3 minutes** after the phone has been out of
  range once, even next to the deck, unlocked and on USB: `ble_drop` 40
  (0x28, instant passed: the deck's controller missed a timed link change
  from the phone) and 8 (supervision timeout). iOS reconnects within about
  5 s, so it only shows in the new column. Right after Bluetooth was
  switched on, 23 min with no drop (debug log: 2M PHY, 251-byte data
  length, 30 ms interval). Not explained yet.

BLE debug log on USB (`-D BLE_DEBUG_LOGGING=1`, the same evening, walking
out of range and back):
- **Frames were lost during a contact sync.** The app asks for the
  contacts changed since its last sync after every reconnect, and Meck
  marks a contact changed whenever its advert is heard, so after a while
  that is most of them (650 here). The deck queued one every 7 ms; on the
  30 ms iOS link the stack's buffers filled, `notify()` failed (rc=-1,
  1811 times in 45 min) and the frames were gone, as was a contact that
  did not fit the send queue. Now the deck sends only while the controller
  has a free buffer for the link and reports busy until the queue has room
  for a batch: 650 contacts and the end frame in 5 s, no errors.
- **A sync cut by a drop carried on into the next link.** The contact
  iterator kept running; after an iOS background reconnect (no
  `CMD_APP_START`) the deck sent the rest of the list unasked and answered
  the app's own `CMD_GET_CONTACTS` with "busy", and the app hung until it
  was restarted. The iterator now stops when the link drops.
- Near the edge of range iOS opens the link with a 720 ms supervision
  timeout (the deck then asks for 4 s), so drops there are expected.
  Whether drops at close range remain with these fixes is not known yet.

Next:
1. If 0x28 drops at close range come back, catch one with the debug log
   and try without the deck's 15-20 ms / 2M PHY / data length requests at
   connect.
2. Messages: `CMD_SYNC_NEXT_MESSAGE` takes a message off the offline queue
   when it is sent; if the link drops before it arrives, the app never gets
   it (it stays in the deck's chat history). Remove it only when the app
   asks for the next one.
3. On battery: whether the app syncs in reconnect windows (`sync_link` /
   `app_rx`). An app closed from the app switcher is not woken by iOS.
4. Shorter windows: find what keeps the link busy for a minute after the
   app reconnects (`sync_s` per window).

## 3. LoRa RX duty cycle

The SX1262 can sleep between short listen windows and wake on a preamble
(`startReceiveDutyCycleAuto`, already behind `MECK_RX_DUTY_CYCLE` for the
watch build). The radio itself uses about 5-6 mA in continuous RX, so this
could save a few mA. The catch: packets with a short preamble can be missed,
the noise-floor sampling is skipped, and it does not reduce the ESP32 wakes.
Worth trying only while locked, and measured against missed packets.

## 4. The 13 mA sleep floor

LilyGo quotes about 0.34 mA for the MAX in deep sleep, while our light
sleep floor is about 13 mA, of which the SX1262 in RX is about 5-6 mA.
The rest (about 7 mA) is worth finding:

- the display controller and e-ink driver (should be idle after the
  redraw)
- the touch controller, keyboard backlight and keyboard MCU
- the GPS, 4G modem and audio codec power rails (checked off via the
  XL9555, but worth measuring one at a time)
- the speaker amplifier (XL9555 IO06) and the antenna switch (IO04)
- PSRAM in light sleep (kept powered; unavoidable while it holds data)
- pull-ups on lines into unpowered chips

Method: put the deck to sleep with the power log on, switch one rail or
peripheral at a time from the debug console, compare the logged average.

## 5. Fewer minute wakes

The lock screen redraws on each minute boundary. A redraw is short, but a
partial e-ink refresh plus the wake costs some energy every minute.
Optional setting: redraw the clock every 5 minutes, or show only the date
and battery. Small saving (well under 1 mA).

## 6. GPS when locked: a fix every few minutes (experimental)

With the GPS on, the locked device sleeps and the receiver keeps tracking
on its own (31.7 mA measured, the GPS about 15 mA of it). Settings >
Experimental Features > "GPS when locked" can instead switch the GPS off
and power it only for a fix every 2-60 minutes; the default ("Stays on")
keeps the behaviour above.

The MIA-M10Q's V_BCKP is fed from the MS621FE cell (VRTC), so with GPS_EN
off the receiver keeps its orbit data, but it has no RTC crystal and loses
the time: it has to get the time from the satellites first (about 7 s
with a good sky, by the integration manual) before the orbits help.
Expected per fix: about 10 s awake at roughly 50 mA (ESP32 35 mA plus the
GPS acquiring 15 mA). Every 10 min that is under 1 mA on top of the
17.4 mA sleep, so about 18-19 mA; every 2 min about 21-22 mA. Without a
sky each try runs into the 60 s timeout (about 50 mA for a minute), so
the interval doubles, then quadruples after misses.

To check on a battery run: the power log's awake seconds in the minutes
with a try (they show the time to the fix), the fix lane, and the average
current. Next steps if the starts are slow: coarse time aiding from the
deck's clock (UBX-MGA-INI-TIME_UTC, only once the clock is synced) and
real UBX configuration instead of the $PCAS/$PMTK lines, which the u-blox
module ignores.

## WiFi while locked

WiFi on is a sleep blocker (about 100 mA or more). Turning it off on lock
and back on at unlock would break an app connected over WiFi. WiFi
power save (`WiFi.setSleep(true)`, modem sleep with DTIM) is the default in
Arduino and already in use, but without automatic light sleep (see 2.3) the
CPU stays awake. So for now: WiFi off when not needed.

## Suggested order

1. Shorter wake after a packet (1): small, safe.
2. Relaxed BLE interval and 80 MHz CPU while locked with a phone (2.1, 2.2).
3. Find the rest of the 13 mA floor (4).
4. Automatic light sleep with BLE modem sleep (2.3): the big one, needs the
   custom sdkconfig.
