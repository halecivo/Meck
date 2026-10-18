#!/usr/bin/env python3
"""Save the device's current e-ink frame as a PNG.

Needs a build with -D MECK_UI_SCREENDUMP (enabled in meck_max_ui2).
Sends the serial CLI command "screendump" and decodes the reply.

Usage: python grab_screen.py COM3 out.png [--scale 2]
Requires pyserial and Pillow.
"""
import argparse
import time

import serial
from PIL import Image


def grab(port, timeout=10.0):
    # Open without asserting DTR/RTS: on the ESP32-S3 native USB port those
    # lines reset the chip, which would only ever capture the boot splash.
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    with s:
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(b"\r\nscreendump\r\n")
        deadline = time.time() + timeout
        rows, size = [], None
        while time.time() < deadline:
            line = s.readline().decode("ascii", "replace").strip()
            if not line:
                continue
            if line.startswith("SCREENDUMP"):
                _, w, h = line.split()
                size = (int(w), int(h))
                rows = []
                continue
            if size is None:
                continue
            if line == "END":
                break
            if len(line) == size[0] // 4:
                rows.append(bytes.fromhex(line))
        if size is None or len(rows) != size[1]:
            raise SystemExit("incomplete dump (%d rows)" % len(rows))
        return size, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()
    (w, h), rows = grab(args.port)
    img = Image.frombytes("1", (w, h), b"".join(rows))
    if args.scale > 1:
        img = img.resize((w * args.scale, h * args.scale), Image.NEAREST)
    img.save(args.out)
    print("saved", args.out, "%dx%d" % (w, h))


if __name__ == "__main__":
    main()
