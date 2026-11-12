# DAV test server

A throw-away Radicale for developing the deck's CalDAV/CardDAV client
(`docs/caldav-design.md`). User `deck`, password `deckpass`, test data only.

    py -3 -m pip install radicale        # python3 on Linux/macOS
    py -3 -m radicale --config radicale.conf
    py -3 seed.py                        # in a second shell

The deck reaches it at `http://<pc-ip>:5232` (WiFi, same network). On a real
Radicale for daily use, create a separate user for the deck and use HTTPS.
`data/` is created next to this file and is not in git.
