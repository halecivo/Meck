# Thin apps — manifest format, draft v1

Status: **draft**, nothing of this is implemented yet. Field names may still
change until the first firmware that reads them ships.

A thin app is a declarative package: it says which HTTP endpoints to read,
how often, and how to show the data. The firmware does all the work —
fetching, parsing, scheduling the radios, drawing and notifying. An app
cannot run code: there is no HTML, no scripting and no way to send data
anywhere except the reads it declares.

Design goals:

- **Build on existing REST APIs.** An app can be written for any HTTP API
  that returns JSON (Home Assistant, Golemio, Open-Meteo...) without
  changing the server. There is no special server-side format.
- **Cheap on power.** Poll intervals are hints; the firmware merges the
  polls of all apps into as few radio wake-ups as it can, and the user
  decides per app which radios it may wake.
- **Open.** The format is meant for third-party apps as well. The UI v2 of
  Meck is the reference renderer, but the widget tree is semantic and
  another UI can draw it its own way.

Non-goals for v1: buttons or any other write to a server, push channels
(MQTT, UnifiedPush), app-to-app communication, OAuth flows.

Files in this directory:

| file | |
|---|---|
| `README.md` | this specification |
| `manifest.schema.json` | JSON Schema (draft 2020-12) of `app.json` |
| `examples/ha-dashboard/` | Home Assistant entities dashboard |
| `examples/pid-departures/` | Prague public transport departures (Golemio) |

`tools/thin-apps/pack.py` validates an app directory against the schema and
packs it into a `.zip`.

---

## 1. Package and installation

An app is a `.zip` with `app.json` at its root, plus optional assets
(icons) referenced from it by relative path.

```
weather.zip
├── app.json
└── icon.png        (optional, 1-bit, 24 x 24 px for the home tile)
```

Installation:

- **From the SD card:** a zip copied to `/apps/` is offered for install in
  Settings → Apps. On install it is unpacked into `/apps/<id>/`.
- **From a URL:** the user enters (or scans) the URL of the zip.

The installed app keeps its `update_url` (the zip it came from, or a newer
location). The firmware checks it at most once a day, only over WiFi, with
`If-None-Match`. An update that widens `permissions` is not applied
silently: the user confirms it like a new install.

User settings and fetched data are kept per app `id` and survive updates.
Values of `secret` settings are stored in NVS, never on the SD card.

## 2. Top-level fields

```json
{
  "manifest_version": 1,
  "id": "cz.example.weather",
  "version": "1.2.0",
  "name": "Mountain weather",
  "short_name": "Weather",
  "description": "Forecast and warnings for the current position",
  "author": "Example",
  "homepage": "https://example.cz/weather",
  "lang": "cs",
  "icon": "partly",
  "update_url": "https://example.cz/weather/weather.zip",
  "extensions_required": [],
  "requires": {},
  "permissions": {},
  "settings": [],
  "sources": [],
  "schedule": {},
  "notify": [],
  "ui": {}
}
```

| field | required | |
|---|---|---|
| `manifest_version` | yes | format version, see §3 |
| `id` | yes | reverse-DNS, `[a-z0-9.-]`, at most 64 chars. Never changes: settings, secrets and cache are bound to it |
| `version` | yes | semver of the app |
| `name` | yes | full name (settings, install dialog) |
| `short_name` | no | home tile label, at most 12 chars; defaults to `name` |
| `description`, `author`, `homepage`, `lang` | no | informative |
| `icon` | no | built-in icon name (§9.2) or a path in the zip |
| `update_url` | no | `https` URL of the zip |
| `extensions_required` | no | see §3 |
| `requires` | no | device features, §4 |
| `permissions` | yes | §5 |
| `settings` | no | user settings, §6 |
| `sources` | yes | data sources, §7 |
| `schedule` | yes | poll intervals, §10 |
| `notify` | no | notification rules, §11 |
| `ui` | yes | screens and home tile, §12 |

## 3. Versioning

- `manifest_version` is an integer and only goes up on a breaking change
  of the format. A firmware refuses a higher version than it knows.
- **Unknown fields are ignored.** New optional fields are added without a
  version bump.
- `extensions_required` lists features without which the app does not
  work (e.g. `"spark.v2"`). A firmware that does not know one of them
  refuses to install the app, instead of running it broken.
- `ui.schema` versions the widget set separately, since it will change
  more often than the rest.

## 4. Device requirements: `requires`

```json
"requires": {
  "features": { "network": "required", "gps": "optional", "cellular": "unused" }
}
```

Each feature is `required`, `optional` or `unused` (the default). An app
that marks a feature `optional` must work without it; templates can test
`{device/has/gps}`. Known features: `network` (any of WiFi or cellular),
`wifi`, `cellular`, `gps`, `keyboard`, `touch`, `audio`.

Display requirements live in `ui` (§12.1).

The firmware publishes its profile as the `device` variable (§8.2).

## 5. Permissions: `permissions`

```json
"permissions": {
  "origins": ["https://api.golemio.cz", "{settings.base_url}"],
  "location": "none",
  "background": false,
  "sleep": false,
  "notifications": true
}
```

| field | |
|---|---|
| `origins` | allowlist of `scheme://host[:port]`. A source whose URL is not on it is not fetched. An entry may be a `url` setting (`{settings.x}`); its origin is shown to the user when they save the setting |
| `location` | `none`, `coarse` (rounded to 0.01°, about 1 km) or `precise`. Controls the `gps` variable |
| `background` | may poll while another screen is open or the deck is idle but awake |
| `sleep` | may poll while the deck is locked and sleeping |
| `notifications` | may notify (§11) |

`http://` origins are allowed only for private addresses (RFC 1918,
link-local) and `.local` names, for home servers on the LAN. Everything
else must be `https://`.

The user sees all permissions on install and can switch each of them off
afterwards.

## 6. Settings: `settings`

```json
"settings": [
  { "key": "base_url", "type": "url", "label": "Address", "default": "http://homeassistant.local:8123" },
  { "key": "token", "type": "secret", "label": "Token", "required": true },
  { "key": "units", "type": "enum", "label": "Units", "options": ["metric", "imperial"], "default": "metric" },
  { "key": "entities", "type": "list", "label": "Entities", "default": ["sensor.temperature"] }
]
```

Types: `string`, `secret` (masked, stored in NVS), `url`, `number`
(`min`, `max`), `bool`, `enum` (`options`), `list` (list of strings, at
most 32 items). `required: true` keeps the app from polling until the
setting has a value; the app shows a "set up first" state.

Next to the app's own settings, the firmware shows the same per-app power
settings for every app (§10.2). They are not declared in the manifest.

## 7. Data sources: `sources`

```json
"sources": [
  {
    "id": "dep",
    "url": "https://api.golemio.cz/v2/pid/departureboards{?ids,minutesAfter}",
    "vars": { "ids": "{settings.stop}", "minutesAfter": 60 },
    "method": "GET",
    "headers": { "Accept-Language": "cs" },
    "auth": { "type": "header", "name": "X-Access-Token", "value": "{settings.token}" },
    "route": "any",
    "max_bytes": 24576,
    "timeout": 15,
    "hints": { "next_poll": "/meta/refresh" }
  }
]
```

| field | |
|---|---|
| `id` | `[a-z][a-z0-9_]*`, unique. Not one of the reserved names `device`, `settings`, `gps`, `app`, `now` |
| `url` | RFC 6570 URI Template (levels 1–3). Variables come from `vars`, then from the global variables (§8.2) |
| `vars` | values for the template; each is a literal or a template string |
| `method` | `GET` (default) or `POST`. `POST` is for read-only query endpoints (GraphQL, Home Assistant `/api/template`); it must not change server state |
| `body` | `POST` only: a JSON value whose strings are templates |
| `headers` | extra request headers (templates allowed) |
| `auth` | `{ "type": "none" }` (default), `{ "type": "bearer", "value" }`, `{ "type": "basic", "user", "password" }`, `{ "type": "header", "name", "value" }`, `{ "type": "query", "name", "value" }`. Values are normally `{settings.<secret>}` |
| `each` | a `list` setting; the source is fetched once per item, `{$}` in the URL being the item. The result is an array, in the order of the list |
| `route` | `any` (default), `wifi` (never over cellular, e.g. a LAN-only server), `cellular` |
| `max_bytes` | the body is dropped (and the fetch fails) above this size. Default 16384, maximum 65536 |
| `timeout` | seconds, default 15 |
| `hints.next_poll` | JSON Pointer into the response to a number of seconds until the next poll (§10.3) |

Response handling:

- The firmware always sends `If-None-Match` with the last `ETag`; a `304`
  keeps the stored data and counts as success.
- The body must be JSON (`application/json` or any `+json` type). A
  firmware may ask for CBOR with `Accept` if the server offers it; the
  data model is the same.
- The parser is streaming and keeps only the paths the manifest refers to
  (the firmware derives the filter from the templates at install time), so
  a large response costs network time but not memory.
- The last good response is kept (on SD, so it survives a reboot) and
  shown while a new fetch fails, marked as stale (§12.3).
- Requests to the same origin within one wake-up share one connection
  (keep-alive, TLS session resumption). This is what makes `each` and
  several sources per app cheap.

## 8. References and templates

### 8.1 References

A **reference** points to a value. Its first segment names the root, the
rest is a JSON Pointer (RFC 6901) into it:

| reference | |
|---|---|
| `dep/departures/0/route/short_name` | source `dep`, pointer `/departures/0/route/short_name` |
| `dep` | the whole response of `dep` |
| `$/state` | the current item (inside `list`, `each` notifications, or a detail screen) |
| `$` | the current item itself (in a source with `each`: the list item) |
| `@dep/age` | metadata of source `dep`: `age` (seconds since the last success), `ok` (last fetch succeeded), `updated` (epoch of the last success), `status` (HTTP status) |
| `settings.token` | a setting (dot instead of slash: settings are flat) |
| `device/width`, `gps/lat`, `now`, `app/version` | global variables, §8.2 |

A missing path is not an error; it yields *null*.

### 8.2 Global variables

| variable | |
|---|---|
| `device/width`, `device/height` | display size in px |
| `device/color` | `mono`, `gray` or `color` |
| `device/technology` | `eink` or `lcd` |
| `device/has/<feature>` | `true` / `false`, features as in §4 |
| `device/battery` | percent |
| `device/rssi` | dBm of the network in use |
| `device/net` | `wifi` or `cellular` |
| `device/firmware`, `device/ui` | firmware version, UI name (`meck.ui2`) |
| `gps/lat`, `gps/lon`, `gps/alt`, `gps/age` | last fix; null without the `location` permission or without a fix |
| `now` | current time, epoch seconds; null while the clock is not set |
| `app/version`, `app/id` | |

Variables are only sent to a server when a template puts them into a URL,
header or body: the device does not announce itself on its own.

### 8.3 Templates

A **template** is a string with `{reference | filter | filter(args)}`
placeholders. Text outside braces is literal; `{{` and `}}` are literal
braces.

When the whole string is one placeholder, the value keeps its type (number,
array, object). Otherwise everything is converted to text; *null* becomes
an empty string.

Filters (fixed set, no user functions):

| filter | in → out | |
|---|---|---|
| `number(n)` | number → text | `n` decimals, locale decimal mark |
| `round` | number → number | |
| `time('fmt')` | ISO 8601 text or epoch → text | `HH`, `mm`, `ss`, `d`, `dd`, `M`, `MM`, `yyyy`, `EEE` (weekday); local time zone |
| `ago` | time → text | `45s`, `14m`, `3h`, `2d` |
| `until` | time → number | whole minutes until that time (negative when past) |
| `map(k:'v', …, _:'v')` | any → text | value lookup; `_` is the fallback, without one an unknown value passes through |
| `before('s')`, `after('s')` | text → text | part before / after the first `s` |
| `trim(n)` | text → text | at most `n` characters, with an ellipsis |
| `upper`, `lower` | text → text | |
| `default('v')` / `default(ref)` | any → any | replaces *null* and empty text |
| `prefix('s')`, `suffix('s')` | text → text | added only to a non-empty value |
| `gt(n)`, `lt(n)`, `ne(v)` | any → any | passes the value when the test holds, *null* otherwise (used to hide a badge) |
| `count` | array → number | |
| `join('s')` | array → text | |

Filter arguments are quoted with single quotes, so templates do not need
escaped double quotes in JSON. Time filters return an empty value while
the clock is not set.

### 8.4 Conditions

Conditions (visibility, emphasis, notification rules) are objects, so
there is no expression language to parse:

```json
{ "field": "$/delay/minutes", "op": ">=", "value": 3 }
{ "all": [ cond, cond ] }
{ "any": [ cond, cond ] }
{ "not": cond }
```

`op`: `==`, `!=`, `>`, `>=`, `<`, `<=`, `in` (value is an array),
`contains` (text or array), `exists`, `empty` (null, empty text or empty
array), `changed` (differs from the previous successful poll). `field` is
a reference (§8.1); `value` a literal or `{ "ref": "<reference>" }`.

## 9. Icons

### 9.1 App icon

`icon` at the top level: a built-in name (§9.2) or a path to a 1-bit PNG
in the zip, 24 × 24 px.

### 9.2 Built-in icons

The names of the UI v2 icon set (`Ui2Icons.h`, without the `UI2_IC_`
prefix, lower case): `msg`, `contact`, `sensor`, `map`, `pin`, `phone`,
`notes`, `book`, `audio`, `alarm`, `settings`, `gps`, `wifi`, `sim`,
`lock`, `home`, `room`, `light`, `switch`, `dimmer`, `thermo`, `blind`,
`scene`, `calendar`, `mail`, `warn`, `check`, `cross`, `refresh`,
`search`, `globe`, `bell`, `sun`, `moon`, `cloud`, `partly`, `rain`,
`heavy`, `snow`, `storm`, `fog`, `wind`, `up`, `down`, `bolt`, `clock`.

A row icon may also be a path to a 16 × 16 px 1-bit PNG in the zip. An
unknown name draws no icon.

## 10. Scheduling: `schedule`

```json
"schedule": {
  "foreground": { "interval": 30,   "flex": 10 },
  "background": { "interval": 900,  "flex": 300 },
  "sleep":      { "interval": 3600, "flex": 1200, "gps": "if_stale", "gps_max_age": 7200 },
  "bounds": { "min": 20, "max": 86400 },
  "on_open": 60
}
```

Modes:

- `foreground` — the app's screen is open.
- `background` — the deck is awake but another screen is shown. Needs
  `permissions.background`.
- `sleep` — the deck is locked and sleeping. Needs `permissions.sleep`.

| field | |
|---|---|
| `interval` | seconds; `0` = do not poll in this mode |
| `flex` | how much earlier than `interval` the poll may run (default 10 % of `interval`). The poll is due in the window `[last + interval − flex, last + interval]` |
| `gps` | `sleep` and `background` only: `never` (default), `if_stale` (switch the GPS on before the poll when the last fix is older than `gps_max_age` s), `always` |
| `bounds` | clamp for server hints (§10.3); defaults `min` 10, `max` 86400 |
| `on_open` | when the screen opens, poll right away if the data is older than this many seconds (default: `foreground.interval`) |

### 10.1 Coalescing

The firmware keeps one due window per app and source. When a wake-up
happens — because one window closes, or for any other reason (a mesh
packet, the user) — it runs every poll whose window is already open. So
`flex` decides how well an app shares wake-ups with others: the wider, the
cheaper.

### 10.2 User power policy

Every app gets the same power settings in Settings → Apps → *app*:

| setting | values | |
|---|---|---|
| WiFi | `auto` / `no_wake` / `never` | `no_wake`: use WiFi when it is already on, never switch it on for this app |
| Cellular | `auto` / `no_wake` / `never` | e.g. `never` without a SIM, `no_wake` to save data |
| GPS | `auto` / `no_wake` / `never` | |
| Polling while asleep | on / off | |
| Interval | as app / ×2 / ×4 / custom per mode | |

`auto` follows the app and the device-wide radio settings. A source with
`route: "wifi"` is skipped (not failed) when only cellular is available.

### 10.3 Effective interval

```
base = server_hint ?? schedule[mode].interval
base = clamp(base, bounds.min, bounds.max)
eff  = max(base, host_floor(mode, battery))
eff  = user_override(mode) ?? eff        // but never below host_floor
```

`server_hint` is taken from, in this order: `hints.next_poll` of the
source, then `Cache-Control: max-age` of the response. With several
sources, the app is due when the first of them is. `Retry-After` on a 429
or 503 is a backoff for that source only; other errors back off
exponentially (×2 up to `bounds.max`).

`host_floor` is a firmware policy, for example: foreground 10 s,
background 60 s, sleep 900 s, doubled below 20 % battery.

## 11. Notifications: `notify`

Rules are evaluated on the device after every successful poll; there is no
push channel.

```json
"notify": [
  {
    "id": "door",
    "each": "ent",
    "where": { "field": "$/entity_id", "op": "==", "value": "binary_sensor.front_door" },
    "when": { "field": "$/state", "op": "==", "value": "on" },
    "trigger": "rising_edge",
    "key": "$/entity_id",
    "cooldown": 600,
    "title": "{$/attributes/friendly_name} open",
    "body": "since {$/last_changed | time('HH:mm')}",
    "priority": "normal",
    "in_sleep": true
  }
]
```

| field | |
|---|---|
| `id` | unique within the app |
| `each` | optional: a reference to an array; the rule is evaluated per item (`$`) |
| `where` | optional: with `each`, only items matching this condition |
| `when` | the condition |
| `trigger` | `rising_edge` (default): fire when `when` becomes true; `while_true`: on every poll while it holds (still limited by `cooldown`) |
| `key` | template that identifies the event (per item with `each`); the edge and the cooldown are tracked per key |
| `cooldown` | seconds before the same key may fire again, default 3600 |
| `title`, `body` | templates; `title` at most 64 chars after expansion, `body` 160 |
| `priority` | `silent` (badge only), `normal` (notification in the list, toast), `high` (also wakes the screen; sound and vibration as the device settings allow) |
| `in_sleep` | may fire while asleep; default `false` (queued until wake-up) |
| `open` | screen to open when the notification is opened, with `$` as above |

The first poll after install or after a setting change only records the
state; it never fires.

## 12. UI: `ui`

```json
"ui": {
  "schema": 1,
  "designed_for": "meck.ui2",
  "min_width": 240,
  "min_height": 320,
  "color": "mono",
  "tile": { "icon": "home", "label": "Home", "value": "{hero/state | number(1)}°" },
  "screens": [ { "id": "main", "title": "Home", "body": [] } ]
}
```

### 12.1 Compatibility

| field | |
|---|---|
| `schema` | version of the widget set; this document describes `1` |
| `designed_for` | informative: the UI the app was laid out for. Not a requirement |
| `min_width`, `min_height` | smallest display the layout works on |
| `color` | minimum: `mono` (works everywhere), `gray`, `color` |

Layouts are not in pixels. Widgets are semantic, the renderer picks
fonts, spacing and paging. A renderer refuses an app whose `schema` it
does not know or whose minimum display it does not meet.

When one layout does not fit every size, a screen can have `variants`:

```json
{ "id": "main", "variants": [
  { "when": { "min_height": 320 }, "body": [] },
  { "body": [] }
] }
```

The first variant whose `when` matches (`min_width`, `min_height`,
`technology`) is used.

### 12.2 Screens

```json
{ "id": "main", "title": "{settings.name}", "title_right": "{@dep/age | ago}", "body": [ widgets ] }
```

The first screen is the one the app opens with. Other screens are reached
with `open` on a row; they get the selected item as `$`. Q goes back.
Navigation never fetches anything except the normal `on_open` refresh.

### 12.3 Chrome (drawn by the firmware)

In UI v2 the app only provides the body; the renderer adds:

- the standard status header;
- the title bar: `title` on the left; on the right the page (`1 / 3`)
  when the body has more than one page, otherwise `title_right`; a
  `stale` chip when the newest data is older than twice the interval or
  the last fetch failed;
- the footer, from what the screen offers: `Q Back`, `W/S` (when there
  is something to select or more pages), `Enter Open` (when the selected
  row has `open`), `R Refresh`;
- loading, error and "set up first" states, as the UI v2 empty state with
  `R Retry` where it applies.

R polls all sources of the app now (subject to the power policy).

### 12.4 Layout

The body is a vertical flow of widgets. The renderer splits it into pages
of the body height (248 px in UI v2: below the title bar, above the
footer) and never splits a widget across pages, except a `list`, which
breaks between rows. W/S move the selection between rows that have
`open`; when no row has one, they move by page.

Every widget accepts `visible_if` (a condition). A hidden widget takes no
space.

### 12.5 Widgets (schema 1)

| widget | fields | UI v2 primitive |
|---|---|---|
| `row` | `icon`, `title`, `sub`, `right`, `right2`, `badge`, `toggle`, `dot`, `emphasis`, `open` | `Ui2List::drawRow`, 40 px |
| `list` | `each`, `where`, `limit`, `row`, `empty` | rows of `row` |
| `section` | `text` | section header, 16 px |
| `kv` | `label`, `value` | `Ui2List::kvRow`, 20 px |
| `hero` | `value`, `unit`, `caption`, `icon` | large value (Montserrat 40) |
| `spark` | `values`, `labels`, `min`, `max`, `height`, `style` | small chart |
| `text` | `text`, `max_lines`, `size` | `Ui2RichText` |
| `empty` | `icon`, `line`, `detail` | `Ui2List::emptyState` |

All text fields are templates. Details:

- **`row`** — `title` is required. `toggle` shows a read-only switch:
  `{ "field": "$/state", "on": "on", "off": "off" }` draws it on when the
  value equals `on`, off when it equals `off`, and no switch otherwise.
  `dot` is a condition (an unread-style dot). `emphasis` is a condition;
  an emphasized row is drawn bold / with a dashed outline (never
  inverted: inversion means selection). `open` is a screen id. An empty
  `badge`, `sub` or `right` is not drawn.
- **`list`** — `each` is a reference to an array. `where` filters items
  (a condition on `$`), `limit` caps them (default 50). `row` is a `row`
  widget evaluated per item. `empty` is an optional `empty` widget shown
  when no items remain.
- **`hero`** — one big value with its unit and a caption line below. Up to
  about 6 characters fit in the large font; longer values use a smaller
  one.
- **`spark`** — `values` is a reference to an array of numbers (at most
  48 are drawn, the rest is cut off). `style`: `bars` (default) or
  `line`. `labels` is an optional array of the same length; the first and
  last label are drawn under the chart. `height` in rows of 8 px, default
  5 (40 px).
- **`text`** — `size`: `small`, `body` (default) or `title`;
  `max_lines` default 6.

### 12.6 Home tile

```json
"tile": { "icon": "home", "label": "Home", "value": "{hero/state | number(1)}°" }
```

The app gets a tile on the home pages, by default after the built-in ones.
`icon` defaults to the app icon, `label` to `short_name`. The optional
`value` is a short live text (at most 8 characters) shown on the tile from
the stored data; it does not cause polls of its own.

E-ink: a tile value or an open screen is redrawn only when the data its
templates read has changed, and at most once per `foreground.interval`.

## 13. Limits (v1)

| | |
|---|---|
| `app.json` | 32 kB |
| zip | 256 kB |
| sources | 8 per app; `each` lists at most 32 items |
| screens | 8; widgets 64 per screen; nesting: `list` → `row` only |
| notification rules | 16 |
| template | 256 chars; at most 8 filters per placeholder |
| stored data | the paths referenced by the manifest, at most 32 kB per app |

A firmware may allow more; apps that need more should say so in
`extensions_required`.

## 14. Open points

- Signing packages (who may publish an update for an `id`).
- OAuth 2.0 device flow for APIs without long-lived tokens.
- Buttons and writes (service calls in Home Assistant) — deliberately out
  of v1.
- Locale of the `number` and `time` filters: from the device language or
  from `lang`?
