# Calendar account (experimental)

Adds a CalDAV account to the deck from the phone, by QR code. For now only the
account is stored and its calendars are found; showing events, syncing and
writing come later (`docs/caldav-design.md`). Built into the MAX envs with
`-D MECK_DAV=1`.

## Adding the account

1. Join the deck to your home WiFi (Settings > WiFi).
2. Settings > Experimental > **Calendar account**, Enter.
3. Scan the QR code with the phone (same network as the deck). The page asks
   for the server address, user name and password.
4. The deck looks the account up and lists its calendars on the phone and on
   its screen.

The server can be any CalDAV server: Radicale, Nextcloud, Baikal, Fastmail,
iCloud with an app-specific password. Type just the host
(`https://dav.example.com`) and the deck finds the rest; if that fails, type the
full address of the calendar home (`https://dav.example.com/user/`).

Use a **user made for the deck**, one you can switch off on the server. The
password goes from the phone to the deck as plain HTTP over the home WiFi (the
deck has no certificate), and is kept in the deck's NVS without encryption, the
way the Home Assistant token is. It is never written to the SD card or a log.
The deck sends it only to the address you typed (iCloud's own partition hosts
excepted). The page is open for 5 minutes, five wrong tries at most, and its
address holds a random one-time token.

## Screen keys

| Key | Does |
|-----|------|
| Enter | Start pairing (no account yet, or after a failure) |
| P | Pair again (replaces the account) |
| X twice | Forget the account |
| Q | Back (stops a pairing that is still waiting) |

## Debug CLI (`meck_max_ui2`)

| Command | Does |
|---------|------|
| `dav` | Account, calendars, pairing state; the password only as a length |
| `dav pair` / `dav stop` | Start / stop pairing without the screen |
| `dav login <url> <user> <password>` | Find and store an account without the phone |
| `dav test` | Run the discovery for the stored account again |
| `dav ls <n>` | List calendar `n` (etags, first ten), read only |
| `dav forget` | Forget the account |
| `uigo dav` | Open the screen |

Nothing is written to a server. HTTPS to a LAN address (10.x, 192.168.x,
`.local`) accepts a self-signed certificate, as the Home Assistant link does;
everything else is checked against the CA bundle, which needs a correct clock.
WiFi only for now.

## Tests

    g++ -std=c++11 -Wall -Wextra -fsanitize=address,undefined \
        -I examples/companion_radio/ui-new test/dav/test_dav.cpp -o /tmp/test_dav && /tmp/test_dav

The discovery code was also run on a PC against a local Radicale (`tools/dav`),
through stand-ins for the Arduino WiFi/HTTP classes; the web server pages, the
QR code, the screen and NVS have not run anywhere yet.
