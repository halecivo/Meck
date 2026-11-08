# Home Assistant (experimental)

The deck can join Home Assistant as a device, the way the phone apps do,
and report its battery and mesh state as sensors. You log in by scanning a
QR code on the deck with your phone; nothing has to be typed on the deck.

Only in the T-Deck MAX builds with WiFi (`meck_max_ble_wifi`, `meck_max_ui2`).
Nothing is sent over LoRa: all of it goes over WiFi to Home Assistant.

## Pairing

1. Connect the deck to WiFi (Settings > WiFi). The phone has to be on the
   same network as the deck: the home WiFi, or the phone's hotspot with the
   deck on it.
2. **Settings > Experimental Features > Home Assistant**, then **Enter**.
   A QR code appears with an address like `http://192.168.1.20/ha`; that is
   a small page on the deck itself.
3. Scan it with the phone. The page asks for the Home Assistant address, as
   the phone opens it (`http://homeassistant.local:8123` at home, or the
   external `https://` address away from home). When the deck found Home
   Assistant on the network, the address is filled in already.
4. **Log in to Home Assistant**: the phone shows Home Assistant's own login.
   After it, Home Assistant sends the phone back to the deck, which finishes
   the pairing in a few seconds. The phone shows "Done".

The pairing waits 5 minutes for the phone. **Q** stops it.

The deck then shows up under **Settings > Devices & services > Mobile App**
with the name of the node.

## Sensors

| Sensor | |
|---|---|
| Battery level | usable percent, as on the deck |
| Battery state | charging / discharging / full / not charging |
| Charger | plugged in or not |
| Battery voltage | diagnostic |
| Battery time left | minutes until the deck shuts down; unavailable while charging (diagnostic) |
| Mesh contacts | |
| Unread messages | channels and direct messages |
| LoRa packets received | since the last restart (diagnostic) |
| Last LoRa packet | time of the last packet heard (diagnostic) |

The deck sends them every 15 minutes, and about 10 seconds after the
charger is plugged in or out. Only while WiFi is on: the deck does not turn
WiFi on for this. When it locks, WiFi stays on if the lock keeps it (the
default, see quick settings); the updates then go on in the automatic light
sleep.

## Inside and outside the home network

Home Assistant tells the deck its internal and external address during the
pairing, and Nabu Casa's cloudhook if there is one. On the WiFi it was
paired on, the deck tries the internal address first; on any other network
the cloudhook, Nabu Casa's remote address, then the external address. The
first one that answers is kept until the network changes.

Certificates of external `https://` addresses are checked against the
usual root certificates, so the deck's clock has to be right (it is set from
the phone, the GPS or the 4G modem). A host that only exists inside a home
network (a `192.168.x.x`-style address or a `.local` name) may have a
self-signed certificate, which is accepted.

Mobile data through the 4G modem is not used yet.

## The screen when paired

- **U** sends an update now.
- **P** pairs again (a new registration replaces the old one).
- **X** twice forgets Home Assistant: the login is revoked and the deck
  stops sending. Delete the device in Home Assistant to remove its
  entities.

When the device is deleted in Home Assistant, the deck notices at the next
update ("Removed in Home Assistant") and stops sending until it is paired
again.

## What is stored

The refresh token and the webhook ID are kept in the deck's flash (NVS),
not on the SD card and not in the settings export. They are not encrypted.
Anyone with the deck and a serial cable could read them; forget the link
(**X** twice) before you give the deck away.

## Serial commands (meck_max_ui2)

| Command | |
|---|---|
| `ha` / `ha status` | state, last update; secrets are shortened |
| `ha status full` | also the addresses |
| `ha pair`, `ha stop` | start / stop the pairing (as Enter / Q on the screen) |
| `ha post` | send an update now |
| `ha post away` | one update the way it goes away from home (external address first) |
| `ha sensors` | print the update that would be sent |
| `ha forget` | as X twice |
| `uigo ha` | open the screen |

## How the login works

Home Assistant accepts a client on the local network without registering
it first: the deck's own address (`http://<deck>/`) is the client ID, and
`http://<deck>/ha/cb` the address the browser is sent back to. The login
uses PKCE, so the one-time code is no use to anyone without the secret the
deck kept. The deck trades the code for a token, reads Home Assistant's
addresses (`/api/config`), registers (`/api/mobile_app/registrations`) and
adds its sensors. After that it only uses the webhook, which needs no
token.
