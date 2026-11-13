# Weather on the lock screen

Settings > Experimental > **Weather on lock screen** (UI v2 builds with WiFi,
off by default) shows the forecast for where the deck is next to the lock
screen clock, as in "Lock screen v3" of the e-ink design system:

- a 32 px icon of the condition (sun / moon, partly cloudy, cloudy, fog,
  rain, heavy rain, snow, storm) with the temperature in a badge on its
  corner;
- under it, only when rain is expected within the next 12 hours, the time it
  starts (`15:20`), or while it rains an arrow and the time it ends
  (`→18:40`). A dry forecast, or rain with no end in the next 12 hours,
  leaves the line out.

## Data

The forecast comes from [Open-Meteo](https://open-meteo.com/): free, no key
and no account. One HTTPS request brings

- `current`: temperature, weather code and day/night for the quarter hour
  the request came in;
- `hourly` for 24 hours: temperature, weather code, day/night;
- `minutely_15` for 12 hours: precipitation per quarter hour.

A reply is about 2 kB. Times are asked as Unix seconds (UTC), so the deck's
own UTC offset turns them into local time.

The position is the best one of class `POS_AREA` (50 km, `docs/position.md`):
a GPS fix, else the serving cell from the offline cell database, else the
advert position. It is rounded to 1/100 of a degree (about a kilometre): that
is all a forecast needs and all Open-Meteo gets. Without a position nothing is
requested and nothing is shown.

## When it is fetched

- About **every hour on WiFi**, every **2 hours over mobile data**, and 10
  minutes after a request that failed.
- **WiFi** when it is connected (also while the deck is locked and sleeps).
  Without WiFi, over **4G** only when mobile data is on and the app's own
  switch (Settings > Mobile data > Weather) allows it. The link is brought up
  for the request and let go right after, as for Home Assistant.
- **Never over Bluetooth**: the phone link carries no internet.
- At once when the setting is switched on, and when the deck has moved about
  10 km from the place the forecast is for (at most every 10 minutes).
- No light sleep during the request (it holds a PM lock for the few seconds of
  the TLS session).

Data over 4G: a request with its TLS handshake is roughly 8-10 kB, so about
100 kB a day at the 2-hour interval.

## What the lock screen shows between requests

The forecast is kept in RAM and on the SD card (`/weather/forecast.bin`), so
it is there after a reboot. Every minute the deck works out from it what to
show now (`wxNowAt()` in `ui-new/MeckWeatherUtil.h`):

- For 30 minutes after the request, the `current` values; then the hourly
  ones: the temperature between the two hours around now, the condition and
  day/night of the nearer hour.
- Rain: a quarter hour with at least 0.1 mm is wet. A quarter's value is the
  precipitation of the 15 minutes before its time. If the quarter hour now is
  wet, the rain line shows when the last wet quarter in a row ends; if not,
  when the next wet quarter starts.
- Data older than **6 hours** is not shown: the clock stands alone until a
  request works again.

The lock screen only redraws when what it shows changes; that check runs in
the main loop before the UI, so a change falls on the minute redraw.

## Code

| File | What |
|---|---|
| `ui-new/MeckWeatherUtil.h` | plain C++: the request URL, reading the reply, the icon for a WMO code, what to show at a time |
| `ui-new/MeckWeather.{h,cpp}` | when to fetch, WiFi / 4G, HTTPS, the SD copy, debug CLI |
| `main.cpp` `meckWeatherTick()` | position, and the lock screen weather group (`UITask::setLockWeather`) |
| `test/weather/test_weather.cpp` | host tests of `MeckWeatherUtil.h` |

Build flag `MECK_WEATHER=1` (MAX WiFi builds); the drawing is in the UI v2
lock screen (`docs/lock-screen.md`).

Host tests (WSL):

    wsl -d Ubuntu -- bash -c "g++ -std=c++11 -Wall -fsanitize=address,undefined -I examples/companion_radio/ui-new test/weather/test_weather.cpp -o /tmp/test_weather && /tmp/test_weather"

## Debug CLI (meck_max_ui2)

    weather           forecast age, place, what the lock screen shows now,
                      last request, next one, WiFi / 4G
    weather fetch     the next request now (needs the setting on and a position)
    weather rain      precipitation per quarter hour (0.1 mm), local time
    weather clear     forget the forecast (RAM and SD)
