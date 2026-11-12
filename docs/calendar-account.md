# Calendar and contacts account (experimental)

Adds a CalDAV / CardDAV account to the deck from the phone, by QR code. The
deck finds the account's calendars and address books. The contacts' phone
numbers are synced to the deck (read only) and used by SMS and calls; the
calendars are only listed for now, showing events comes later
(`docs/caldav-design.md`). Built into the MAX envs with `-D MECK_DAV=1`.

## Adding the account

1. Join the deck to your home WiFi (Settings > WiFi).
2. Settings > Experimental > **Calendar account**, Enter.
3. Scan the QR code with the phone (same network as the deck). The page asks
   for the server address, user name and password.
4. The deck looks the account up and lists its calendars on the phone and on
   its screen, with the number of address books.

The server can be any CalDAV / CardDAV server: Radicale, Nextcloud, Baikal,
Fastmail, iCloud with an app-specific password. Type just the host
(`https://dav.example.com`) and the deck finds the rest; if that fails, type the
full address of the home (`https://dav.example.com/user/`). An account with
only contacts, or only calendars, is fine.

Use a **user made for the deck**, one you can switch off on the server. The
password goes from the phone to the deck as plain HTTP over the home WiFi (the
deck has no certificate), and is kept in the deck's NVS without encryption, the
way the Home Assistant token is. It is never written to the SD card or a log.
The deck sends it only to the address you typed (iCloud's own partition hosts
excepted). The page is open for 5 minutes, five wrong tries at most, and its
address holds a random one-time token.

## Contacts

- Read only: nothing is ever written to the server.
- When: a minute after boot, right after pairing, then every 6 hours (15
  minutes after a failed try), and with S on the account screen. Only on WiFi
  and only while the deck is unlocked; a sync cut by the lock goes on after
  the unlock.
- First the deck asks each address book for its state (sync-token or ctag);
  when none has changed nothing is fetched. Otherwise all cards are fetched
  again in batches (`addressbook-multiget`, up to 20 cards or 360 kB of cards
  a request; a single card larger than that, a huge photo, is skipped).
- Every phone number of a card becomes one entry: the card's name (FN, else
  given and family name, else the organisation) and the number without spaces,
  `00` written as `+`. Up to 1000 numbers.
- They are kept in `/sms/dav-contacts.txt` on the SD card (`number=name`
  lines). Your own SMS contacts in `/sms/contacts.txt` are never touched; an
  own entry for the same number wins. The list in SMS > Contacts shows your own
  contacts first, then the synced ones by name. Editing a synced contact saves
  it as an own one; deleting it on the deck does nothing (delete it on the
  server).
- A number from the network (`+420777123456`) matches a card's `777 123 456`:
  the last 9 digits are compared when both have at least 9.
- Forgetting the account removes the synced contacts too.

## Screen keys

| Key | Does |
|-----|------|
| Enter | Start pairing (no account yet, or after a failure) |
| S | Sync the contacts now |
| P | Pair again (replaces the account, synced contacts stay until the next sync) |
| X twice | Forget the account and its synced contacts |
| Q | Back (stops a pairing that is still waiting) |

## Debug CLI (`meck_max_ui2`)

| Command | Does |
|---------|------|
| `dav` | Account, calendars, address books, contacts sync state, free RAM; the password only as a length |
| `dav pair` / `dav stop` | Start / stop pairing without the screen |
| `dav login <url> <user> <password>` | Find and store an account without the phone |
| `dav test` | Run the discovery for the stored account again |
| `dav sync` | Sync the contacts now and wait for the end |
| `dav ls <n>` | List calendar `n` (etags, first ten), read only |
| `dav forget` | Forget the account |
| `uigo dav` | Open the screen |

HTTPS to a LAN address (10.x, 192.168.x, `.local`) accepts a self-signed
certificate, as the Home Assistant link does; everything else is checked against
the CA bundle, which needs a correct clock. WiFi only for now. While the pairing
page is up the deck keeps its WiFi out of power save, so the phone reaches it
at once.

## Known problems

- With **Mobile data** on, after the 4G data link drops (`[Data] IP lost`) the
  deck's DNS server is left at 0.0.0.0 (`data` shows it): names do not resolve,
  and after the next request the deck stopped answering on WiFi until a reset.
  Seen once while pairing; with mobile data off pairing and discovery worked.
  Not caused by the DAV code, not fixed yet.
- With WiFi on only about 9 kB of internal RAM is left; `dav` prints it.

## Tests

    g++ -std=c++11 -Wall -Wextra -fsanitize=address,undefined \
        -I examples/companion_radio/ui-new test/dav/test_dav.cpp -o /tmp/test_dav && /tmp/test_dav

They cover the URL and XML helpers, the vCard reader (vCard 2.1 to 4.0,
folding, quoted-printable, escapes, CDATA) and the phone number matching. The
CardDAV replies in them are written by hand in Radicale's shape, not captured.
On the deck the pairing page, the QR code, HTTPS discovery against a real
server and the screen have been tried; the contacts sync has not run on a
deck yet.
