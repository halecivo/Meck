#!/usr/bin/env python3
"""Press keys on the device over serial (debug builds with MECK_UI_SCREENDUMP).

Each argument is one key: a single character, or enter / esc / bs / space.
Optionally saves a screenshot afterwards.

Usage: python press_keys.py COM3 d d enter [--shot out.png] [--delay 1.5]
Requires pyserial (and Pillow for --shot).
"""
import argparse
import time

import serial


def open_port(port):
    # No DTR/RTS: on the ESP32-S3 native USB port they reset the chip
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("keys", nargs="*")
    ap.add_argument("--delay", type=float, default=1.5, help="seconds between keys")
    ap.add_argument("--shot", help="save a screenshot to this PNG at the end")
    args = ap.parse_args()
    with open_port(args.port) as s:
        time.sleep(0.2)
        for k in args.keys:
            s.write(("\r\nkey %s\r\n" % k).encode())
            s.flush()
            time.sleep(args.delay)
    if args.shot:
        time.sleep(1.0)
        import grab_screen
        (w, h), rows = grab_screen.grab(args.port)
        from PIL import Image
        img = Image.frombytes("1", (w, h), b"".join(rows)).resize((w * 2, h * 2), Image.NEAREST)
        img.save(args.shot)
        print("saved", args.shot)


if __name__ == "__main__":
    main()
