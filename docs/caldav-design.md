# CalDAV on the deck: design proposal

Status: proposal, nothing implemented. Target: T-Deck MAX (8 MB PSRAM, UI v2),
WiFi or 4G through `MeckNet`. Radicale first, iCloud second. The DAV core is
written once and CardDAV (phone contacts, see `SMSContacts`) reuses it.

## Goals and non-goals

Goals
- Read calendars (events) from a CalDAV server, offline from an SD cache.
- Create, edit and delete events on the deck; changes reach the server.
- Event reminders on the deck (silent haptic on the MAX, like the alarm app).
- Works with any standard server: Radicale, Nextcloud, Baikal, iCloud.

Non-goals (first versions)
- Editing single instances of a recurring event (series are read-only on the
  deck at first).
- Attendees, invitations (iTIP), free/busy, attachments.
- Reminders/tasks (VTODO). iOS keeps Reminders in a form CalDAV cannot reach.

## Layers

    CalendarScreen (UI v2)        month, agenda, event page, edit page
    CalService                    sync task, outbox, reminder scheduler
    CalStore                      SD cache + RAM window of expanded instances
    ICal                          VEVENT parser/serializer, RRULE, time zones
    Dav                           discovery, PROPFIND/REPORT/PUT/DELETE, XML
    MeckNet + HTTPClient/TLS      existing; new app id DATA_APP_DAV
    DavVault                      credentials, encrypted

New files go next to `MeckHa.*` in `examples/companion_radio/ui-new/`:
`MeckDav.{h,cpp}`, `MeckICal.{h,cpp}`, `MeckCal.{h,cpp}`, `CalendarScreen.h`.
`MeckHa.cpp` is the model for the HTTP and token handling.

## Network

- Add `DATA_APP_DAV` to `DataApp` in `MeckDataUse.h` (also gets its own 4G
  switch and byte counter, like `DATA_APP_HA`).
- Every sync is `meckNetWaitUp(DATA_APP_DAV, ...)`, then a `MeckNetUse` scope
  around the requests, then `meckNetRelease`.
- Over 4G (`meckNetSaveData()`): only ask for the sync-token/ctag, and fetch
  changed events; a narrower time window; never the first full sync.
- Sync runs after unlock, then every N minutes while awake (setting), and on
  demand. Never in light sleep; on battery the deck would not wake for it.

## Discovery (RFC 6764, 4791)

1. `GET/PROPFIND <base>/.well-known/caldav` (iCloud: `https://caldav.icloud.com`).
2. `PROPFIND` `current-user-principal`, then `calendar-home-set`. iCloud
   answers from a partition host (`pNN-caldav.icloud.com`): follow it and
   store the resulting URL.
3. `PROPFIND` depth 1 on the home set: `displayname`, `resourcetype`,
   `supported-calendar-component-set` (keep only calendars with VEVENT),
   `calendar-color`, `getctag`/`sync-token`.
4. The user picks which calendars to show. Result is stored once; discovery
   runs again only when a request fails with 404.

## Sync algorithm

Per calendar, pull:
1. If the server supports it (Radicale and iCloud do): `REPORT sync-collection`
   (RFC 6578) with the stored sync-token. Returns changed and removed hrefs.
2. Otherwise compare `getctag`; if changed, `PROPFIND` depth 1 for
   `getetag` and diff against the cache index.
3. Fetch changed events with `calendar-multiget` in batches of ~20 (bounded
   response size), restricted to a window: -30 days to +180 days via
   `calendar-query` + `time-range` on the first sync. Recurring events with no
   end are always included.
4. Stream each response straight to a small SAX-style XML parser; the body of
   `calendar-data` is written to SD, never held whole in RAM.

Push, per local change (outbox in `/cal/outbox/`, one file per operation, so
it survives a reboot and works offline):
- new: `PUT <calendar>/<uid>.ics` with `If-None-Match: *`
- edit: `PUT` with `If-Match: <etag>`
- delete: `DELETE` with `If-Match: <etag>`
- `412 Precondition Failed`: re-fetch the event and ask the user which one wins
  (deck copy or server copy). Never overwrite silently.
- Push always runs before pull so a pull cannot undo a pending edit.

Preserve unknown data: an edit patches only `SUMMARY`, `DTSTART`, `DTEND`,
`LOCATION`, `DESCRIPTION` in the stored original ICS text and leaves every
other property, `VTIMEZONE` and `VALARM` as they were. The deck never
re-serializes an event from its own struct, which would drop what it does not
know.

## iCalendar subset

Parse: `UID`, `SUMMARY`, `DTSTART`, `DTEND`/`DURATION`, all-day (`VALUE=DATE`),
`LOCATION`, `DESCRIPTION`, `RRULE`, `EXDATE`, `RECURRENCE-ID` (overrides),
`STATUS:CANCELLED`, `VALARM` `TRIGGER`. Line unfolding and `\n`, `\,`, `\;`
unescaping. UTF-8 text must reach the display through `Utf8CP437`.

RRULE: `FREQ` DAILY/WEEKLY/MONTHLY/YEARLY, `INTERVAL`, `COUNT`, `UNTIL`,
`BYDAY` (with ordinal for monthly), `BYMONTHDAY`, `BYMONTH`. Anything else:
show the first occurrence and mark the event "complex recurrence". Instances
are expanded only inside the visible window plus the next 24 h of reminders.
(`calendar-query` can also ask the server to `expand`; support differs between
servers, so the client does it and treats server expansion as optional.)

Time zones: `NodePrefs` has `utc_offset_hours` as a whole number, so it cannot
express half-hour zones or DST. Plan: a small table of common `TZID` to POSIX
TZ strings (`Europe/Prague` is `CET-1CEST,M3.5.0,M10.5.0/3`) used with the
newlib `setenv("TZ")`/`localtime_r` path, `VTIMEZONE` offsets as the fallback,
"floating" times shown as they are. Needs a user "home time zone" setting.

## Storage

    /cal/accounts.dat            server URL, calendar list, sync tokens, ctags
    /cal/<cal-id>/index.dat      uid -> href, etag, start, end, flags (sorted)
    /cal/<cal-id>/<uid>.ics      original ICS text
    /cal/outbox/NNNNNN.op        pending PUT/DELETE
    RAM (PSRAM)                  expanded instances for the visible window

Index is binary and sorted by start so month and agenda views never open the
`.ics` files. Cache is rebuildable from the server; deleting `/cal` is safe
except for the outbox.

## Credentials and TLS

- `DavVault`: server URL in the clear, password encrypted with AES-GCM using a
  key from PBKDF2 over the unlock PIN. Stored in NVS namespace `dav`. Locked
  deck: no background sync. Never logged; masked in the debug CLI the way
  `haMask` does it.
- iCloud: app-specific password, revocable at appleid.apple.com. Radicale:
  dedicated user for the deck.
- TLS: public servers verify against the ESP32 certificate bundle (check that
  it contains Apple's chain). A local server with a self-signed certificate:
  pin its fingerprint, instead of the `setInsecure()` the Home Assistant code
  uses for local hosts. Plain HTTP only for a private address, with a warning
  in the UI.
- Heap: one TLS session costs tens of kB of internal RAM. Measure with the
  debug CLI before settling on batch sizes, and keep one connection open for a
  whole sync.

## UI (UI v2, e-ink)

- Home tile "Calendar": agenda list for the next days (default), month grid
  with dots on busy days (keys for next/previous month), event page.
- Edit page like the SMS contact page: fields Title, Start, End, All day,
  Place, Save. Calendar picker when several are writable.
- Settings, "Accounts": preset (iCloud, Radicale, Custom), URL, user,
  password, calendars on/off, sync interval, sync over 4G, "Sync now" with a
  result line (counts, last error). English strings go into `MeckLang.h`.
- E-ink: redraw only on a finished sync or key press, not on progress ticks.

## Reminders

`CalService` keeps the alarms of the next 24 h in a small sorted list (from
`VALARM` and an optional default lead time). The existing background check in
the main loop (every ~10 s, as `AlarmScreen`) fires them: on the MAX a silent
haptic buzz and a banner; sound only if the setting allows it. List is rebuilt
after each sync and at midnight.

## Phases

| # | Content | Done when |
|---|---------|-----------|
| 0 | `Dav` core, discovery, XML parser, Radicale test server | calendar list and one event fetched |
| 1 | `ICal` parse, RRULE, time zones, `CalStore`, agenda screen | a real calendar reads right, incl. DST |
| 2 | Month view, settings, vault, `DATA_APP_DAV` | usable read-only on WiFi and 4G |
| 3 | Write path: outbox, PUT/DELETE, conflicts | create/edit/delete round trip with iPhone |
| 4 | Reminders | haptic reminder on the deck |
| 5 | iCloud profile (partition host, ADP check) | same calendar via iCloud |
| 6 | CardDAV on the same core, `SMSContacts` limit raised | contacts sync |

Phases 0 to 2 are read-only and carry no risk for the user's data.

## Testing

- Host-side unit tests (pattern: `test/ha/test_ha.cpp`) for the ICS parser,
  RRULE expansion and the time zone table, with ICS files exported from iOS,
  Radicale and Nextcloud, including DST edges and all-day events.
- Integration against a Radicale in Docker (`tools/dav/`, with a script that
  fills it with sample events).
- Device: read-only first; no writes to the user's calendars until the test
  calendar on Radicale is proven. Follow the rules in `CLAUDE.md` (no
  sounds, no sends, device lock).

## Open questions

- Is the device in the "Latest build" pipeline (`next`) or on `testing` first?
- Does iCloud CardDAV/CalDAV work with Advanced Data Protection on? (I believe
  not; to be checked before phase 5.)
- Does the deck have NTP time over WiFi, or only the RTC/GPS/mesh clock? The
  `If-Match`/`DTSTAMP`/reminder logic needs correct time.
- Heap budget with BLE + WiFi + TLS together on `meck_max_ui2`.
