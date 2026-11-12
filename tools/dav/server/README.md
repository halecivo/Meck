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
- the deck uses `deck` and may only read your collections (recommended):

      cp config/rights.example config/rights     # put your user name for `you`

  then in `config/config` switch `[rights]` to `type = from_file` and
  `file = /config/rights` (the lines are there, commented), and
  `docker compose restart radicale`. On the deck type the address of your
  home, with your user name in it: `https://dav.example.com/you/`, and log in
  as `deck`. The deck looks at a typed path first, so it finds your calendars
  and address books there.

## iCloud into Radicale (vdirsyncer, one way)

The `vdirsyncer` service copies the iCloud contacts and calendars into your
Radicale user every 15 minutes, and once a day picks up calendars or address
books added in iCloud since. iCloud is only read: Radicale is a mirror, and a
change made in Radicale is put back the iCloud way at the next sync. Change
things on the phone.

1. On appleid.apple.com, Sign-In and Security, App-Specific Passwords: make
   one for "vdirsyncer". iCloud takes nothing else over CardDAV/CalDAV. It
   works with Advanced Data Protection on (Apple keeps contacts and calendars
   out of end-to-end encryption).
2. Secrets, readable by the container's user (uid 1000) only:

       mkdir -p secrets status
       printf '%s' 'abcd-efgh-ijkl-mnop'   > secrets/icloud     # the app-specific password
       printf '%s' 'your Radicale password' > secrets/radicale
       sudo chown -R 1000:1000 secrets status && chmod 700 secrets && chmod 600 secrets/*

3. `cp vdirsyncer/config.example vdirsyncer/config`, put in your Apple ID and
   your Radicale user (`you`, the one the iPhone would use, not `deck`).
4. `docker compose up -d --build`, then watch the first run:

       docker logs -f vdirsyncer

   The first `discover` creates in Radicale one collection per iCloud address
   book and calendar (random-looking names, the display names come along),
   then the first `sync` copies everything; a few thousand contacts take a
   few minutes.
5. Deck: pair (or `dav test`) once, so it learns the new address books; it
   syncs the contacts by itself from then on.

Stop it with `docker compose stop vdirsyncer`; Radicale keeps the copies.
Removing a contact in iCloud removes it in Radicale at the next sync, and on
the deck at the deck's next sync (every 6 hours, or S on its account screen).
`./status` holds vdirsyncer's sync state; deleting it makes the next run
compare everything again (nothing is lost).

## Notes

- Back up `./data` (plain `.ics`/`.vcf` files). `./secrets` and `./vdirsyncer/config`
  stay on the server (`.gitignore`).
- Update: `docker compose pull && docker compose up -d`.
- Deck-side the certificate check needs correct time, so the deck must have
  a valid clock before the first sync.
