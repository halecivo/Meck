# Lock screen (UI v2)

The UI v2 lock screen follows "Lock screen v3" of the e-ink design system:
the clock comes first and never shrinks; everything else is small blocks
that appear only when they have something to show.

Code: `LockScreen` in `examples/companion_radio/ui-new/UITask.cpp`, the
widget interface in `ui-new/ui2/Ui2LockWidgets.h`.

## Layout (240 x 320)

| Zone | Where | What |
|---|---|---|
| Top line | y 6–22 | lock icon and the node name (bold 10), centred |
| Radio column | right edge, from y 6, 16 px per icon | radios that stay on while the deck sleeps, top down: BLE (phone connected over BLE), WiFi, GPS, 4G, MeshCore (always). Off = not drawn |
| Top group | centred between the node line and the widgets (or the status line), at least 8 px from both | clock (Montserrat 40) with the weather group on its left, date, notifications 10 px under the date |
| Widgets | stacked upwards, 8 px apart, ending 6 px above the status line | agenda, departures, ... |
| Status line | y 298–316 | moon / bolt / warn icon, power-saving state, battery estimate, battery icon and % |

- **Clock and weather**: without weather the clock is centred alone. With
  weather the group (32 px icon, temperature badge on its corner, and the
  rain line under it: start time, or an arrow and the end time while it
  rains) sits 12 px left of the clock, and both are centred as one block.
- **Notifications** sit under the date: 16 px icons for unread messages,
  SMS and missed calls, 22 px apart, each with a count badge. A zero count
  is not drawn; with nothing unread the row is gone.
- **Status line**: "Power saving" when the deck sleeps (what stays on is the
  radio column's job), otherwise what keeps it awake. The estimate is left
  out when it does not fit next to a long status.
- **Widgets**: nothing is resized; fewer widgets only move the top group
  down into the middle. When the top group cannot keep its 8 px on both
  sides, the widget with the lowest priority is left out, then the next,
  until the rest fit.

## Adding a widget

A provider (calendar, weather, timetable) owns its data and its widget:

```cpp
struct AgendaWidget : Ui2LockWidget {
  uint8_t priority() const override { return 20; }
  int16_t height(int16_t w, uint32_t now) override;    // 0 = nothing to show
  void draw(Ui2Canvas& c, int16_t x, int16_t top, int16_t w, uint32_t now) override;
  uint32_t nextChange(uint32_t now) override;          // e.g. when the next event ends
};

static AgendaWidget agenda;
ui_task.addLockWidget(&agenda);     // once; order added = order on screen, top down
...
ui_task.lockContentChanged();       // after new data: redraws now if locked
```

- `draw()` starts with the widget's 1 px top rule at `top`.
- `height()` and `draw()` run on every lock screen redraw (once a minute),
  so they only read data the provider already has; no I/O there.
- The lock screen redraws at the next minute boundary, or at the earliest
  `nextChange()` of the widgets shown, whichever comes first.
- Weather is not a widget: `ui_task.setLockWeather(Ui2LockWeather)` with
  `valid = false` takes the group away again.

## Debug CLI (meck_max_ui2)

`lockdemo` puts made-up content on the lock screen to check the layout on
screenshots; it lives in RAM only and nothing is sent or stored.

    lockdemo weather          sun, 4°
    lockdemo rain             partly cloudy, 6°, rain from 15:20
    lockdemo raining          rain, -9°, until 18:40
    lockdemo block <h> [prio] one more dummy widget of h px (up to three)
    lockdemo off              all of it gone

Then `uigo lock` and `tools/ui2/grab_screen.py`.
