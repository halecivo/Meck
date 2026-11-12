#!/usr/bin/env python3
"""Fill a test Radicale with sample calendars/events for the deck's DAV client.

    python3 seed.py [http://127.0.0.1:5232] [user] [password]

Creates the calendar "test" with a few events covering what the parser has
to handle: timed, all-day, UTC, TZID, recurring, an override and a cancelled
instance. Test data only; it never talks to anything but the given server.
"""
import base64, sys, urllib.request

base = (sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:5232").rstrip("/")
user = sys.argv[2] if len(sys.argv) > 2 else "deck"
pw = sys.argv[3] if len(sys.argv) > 3 else "deckpass"
auth = "Basic " + base64.b64encode(f"{user}:{pw}".encode()).decode()

def req(method, path, body=None, headers=None):
    r = urllib.request.Request(base + path, data=body.encode() if body else None, method=method)
    r.add_header("Authorization", auth)
    for k, v in (headers or {}).items():
        r.add_header(k, v)
    try:
        with urllib.request.urlopen(r) as resp:
            return resp.status
    except urllib.error.HTTPError as e:
        return e.code

def vcal(uid, *lines):
    body = "\r\n".join(lines)
    return ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//meck//seed//EN\r\n"
            f"BEGIN:VEVENT\r\nUID:{uid}\r\nDTSTAMP:20261101T000000Z\r\n{body}\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")

mk = ('<?xml version="1.0"?><mkcol xmlns="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav">'
      '<set><prop><resourcetype><collection/><c:calendar/></resourcetype>'
      '<displayname>test</displayname>'
      '<c:supported-calendar-component-set><c:comp name="VEVENT"/></c:supported-calendar-component-set>'
      '</prop></set></mkcol>')
print("MKCALENDAR", req("MKCOL", f"/{user}/test/", mk, {"Content-Type": "application/xml"}))

events = {
 "timed": vcal("timed@seed", "DTSTART;TZID=Europe/Prague:20261112T093000",
               "DTEND;TZID=Europe/Prague:20261112T103000", "SUMMARY:Schuzka s diakritikou: Zlutoucky kun",
               "LOCATION:Praha"),
 "utc": vcal("utc@seed", "DTSTART:20261113T120000Z", "DTEND:20261113T130000Z", "SUMMARY:UTC event"),
 "allday": vcal("allday@seed", "DTSTART;VALUE=DATE:20261114", "DTEND;VALUE=DATE:20261115",
                "SUMMARY:All day"),
 "weekly": vcal("weekly@seed", "DTSTART;TZID=Europe/Prague:20261109T180000",
                "DTEND;TZID=Europe/Prague:20261109T190000", "RRULE:FREQ=WEEKLY;BYDAY=MO,TH;COUNT=20",
                "EXDATE;TZID=Europe/Prague:20261116T180000", "SUMMARY:Weekly Mon+Thu",
                "BEGIN:VALARM", "ACTION:DISPLAY", "TRIGGER:-PT15M", "DESCRIPTION:x", "END:VALARM"),
 # DST edge: Europe/Prague leaves summer time on 2027-03-28
 "dst": vcal("dst@seed", "DTSTART;TZID=Europe/Prague:20270328T013000",
             "DTEND;TZID=Europe/Prague:20270328T033000", "SUMMARY:DST change night"),
 "monthly": vcal("monthly@seed", "DTSTART;VALUE=DATE:20261101", "RRULE:FREQ=MONTHLY;BYDAY=-1FR",
                 "SUMMARY:Last Friday"),
}
for name, ics in events.items():
    print(name, req("PUT", f"/{user}/test/{name}.ics", ics,
                    {"Content-Type": "text/calendar", "If-None-Match": "*"}))

# Address book "contacts" for the deck's CardDAV sync: made-up names and
# numbers (555 / reserved ranges), a card without a number, 2.1 quoted-printable
mkab = ('<?xml version="1.0"?><mkcol xmlns="DAV:" xmlns:r="urn:ietf:params:xml:ns:carddav">'
        '<set><prop><resourcetype><collection/><r:addressbook/></resourcetype>'
        '<displayname>contacts</displayname></prop></set></mkcol>')
print("MKCOL addressbook", req("MKCOL", f"/{user}/contacts/", mkab, {"Content-Type": "application/xml"}))

def vcard(uid, *lines):
    return "BEGIN:VCARD\r\nVERSION:3.0\r\n" + f"UID:{uid}\r\n" + "\r\n".join(lines) + "\r\nEND:VCARD\r\n"

cards = {
 "anna": vcard("anna@seed", "FN:Anna Testov\u00e1", "N:Testov\u00e1;Anna;;;",
               "TEL;TYPE=CELL:+420 555 000 101", "TEL;TYPE=HOME:+420 555 000 102"),
 "bob": vcard("bob@seed", "N:Example;Bob;;;", "TEL:00420555000103"),
 "pizza": vcard("pizza@seed", "ORG:Pizza Test;Delivery", "TEL;TYPE=WORK:555 000 104"),
 "nonum": vcard("nonum@seed", "FN:No Number", "EMAIL:nobody@example.com"),
 "qp": ("BEGIN:VCARD\r\nVERSION:2.1\r\nUID:qp@seed\r\n"
        "N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:=C4=8Cern=C3=BD;Petr;;;\r\n"
        "TEL;CELL:+420555000105\r\nEND:VCARD\r\n"),
}
for name, vcf in cards.items():
    print(name, req("PUT", f"/{user}/contacts/{name}.vcf", vcf,
                    {"Content-Type": "text/vcard", "If-None-Match": "*"}))
