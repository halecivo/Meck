# Map on the lock screen

Settings > Experimental > **Map on lock screen** (UI v2, boards with GPS).

While the deck is locked, GPS is on and its fix is recent, the map takes all
the room the lock screen has for widgets: agenda and departures are left out,
the clock group stays above it. The map is centred on the deck, at the best
zoom the SD card has (the highest `/tiles/<z>` directory), and the own position
is marked in the middle. It is drawn by `MapScreen::renderInRect`, so it is the
same tiles, decoder and marker as the map screen.

## When it shows

- the switch is on, GPS is enabled and the SD card is ready;
- `meckPosition` has a GPS fix whose radius, aged by the drift rule, still fits
  the LOCAL class (2 km, a couple of minutes) -- the same window the departures
  use for the position they send, so nothing here asks for more GPS time and
  nothing switches the GPS on;
- there is a tile under the position at that zoom (otherwise the lock screen
  stays as it was, rather than showing a blank map).

`meckLockMapTick()` (main.cpp) checks this once on locking and every 5 s after,
and asks the lock screen for a redraw when the answer changes. The map itself is
drawn with the normal minute redraw, so it follows the deck once a minute.

## Layout

`Ui2LockWidget::fillsLock()` marks a widget that takes the whole stack area:
the lock layout gives it all the room under the clock group and sets the other
widgets' heights to 0. With less than 60 px of room it is left out.

## Cost

A redraw decodes up to six PNG tiles from the SD card at boosted CPU, a second
or two, once a minute while the map shows. Not measured on battery yet.

## Settings

Everything about the lock screen's content is in **Settings > Lock screen**
(`docs/lock-screen-settings.png`), no longer in Experimental: compact date,
agenda, weather, departures (and their size and minutes), and the map.

**Compact date** shows the date as a calendar tile right of the clock (weekday
in the black strip, the day below) instead of a line under it. The tile shares
the row with the clock and the weather group, also with the widest weather
(rain ending); the block moves left of the
radio column if it must. The notifications keep a row of their own. When the
row is too narrow the date stays a line. The saved row goes to the widgets.

## Debug

`lockmap demo [lat lon]` (serial CLI, `meck_max_ui2`) shows the map on the lock
screen without a GPS fix, at the advert position or the given one; `lockmap off`
ends it. `lockdemo notif <messages> <sms> <missed>` fakes notification counts
(`lockdemo off` clears them). Tested on the MAX 2026-11-21: SD tiles z12-15, four
tiles drawn at z15, marker in the middle, agenda and departures left out.
