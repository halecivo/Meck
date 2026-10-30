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

### Measured (2026-10-30, iPhone, firmware 1a4425be, on battery)

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

Next:
1. "Stay connected": after a link drop while locked, keep advertising for
   a reconnect window, then fall back to windows (e.g. every 5 min) rather
   than staying dark until unlock.
2. Why iOS drops the link soon after locking: test "Stay connected"
   without the relaxed request (this branch now), and watch the BLE
   disconnect reason on the serial console with the deck locked on USB.
3. Shorter windows: find what keeps the link busy for a minute after the
   app reconnects (`sync_s` per window now that it is logged).

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
