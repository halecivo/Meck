# Radicale on a home server (docker compose)

Production-style setup for daily use: one container, own host name behind the
existing nginx proxy with Let's Encrypt. The deck and the iPhone use the same
`https://dav.example.com` address.

## Install

1. DNS: `dav.example.com` to the proxy (public record).
2. Inside the LAN, make the same name resolve to the proxy's LAN address
   (split DNS: router/Pi-hole/AdGuard local override, or hairpin NAT). Then
   the same URL and the same valid certificate work at home and away, and the
   deck needs no second profile and no plain HTTP.
3. Users (bcrypt; one for you, one for the deck):

       docker run --rm httpd:2 htpasswd -nbB you 'password'   >> config/users
       docker run --rm httpd:2 htpasswd -nbB deck 'password2' >> config/users

4. `docker compose up -d`, add the proxy host (`nginx-radicale.conf`).
5. Check: `curl -u deck:password2 -X PROPFIND -H 'Depth: 0' https://dav.example.com/deck/`

## iPhone

Settings, Calendar (and Contacts), Accounts, Add account, Other, Add CalDAV
(CardDAV) account: server `dav.example.com`, your user and password. Use the
account `you`, not `deck`.

## The deck's own user

Radicale's default `owner_only` rights give every user private collections,
so `deck` would not see yours. Two ways:
- the deck logs in as `you` (simplest, but the deck then holds your password);
- the deck uses `deck`, and your calendar is shared to it: set
  `type = from_file` in `[rights]` with a rights file granting `deck` read
  (or read/write) on `/you/calendar/`. Recommended once writing is in.

## Notes

- Back up `./data` (plain `.ics`/`.vcf` files).
- Update: `docker compose pull && docker compose up -d`.
- Deck-side the certificate check needs correct time, so the deck must have
  a valid clock before the first sync.
