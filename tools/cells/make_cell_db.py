#!/usr/bin/env python3
"""Make the deck's offline cell tower database (docs/position.md).

Input: one or more CSV files in the OpenCellID format, plain or gzipped:

    radio,mcc,net,area,cell,unit,lon,lat,range,samples,changeable,created,updated,averageSignal

OpenCellID offers one file per country (mobile country code) to anyone with a
free account: https://opencellid.org/downloads.php, or with --token straight
from here. The data is CC BY-SA 4.0 (OpenCellID / Unwired Labs).

Output: one file per mobile country code, <out>/<mcc>.bin, sorted for the
deck's binary search. Copy them to /cells/ on the deck's SD card. GSM, UMTS
and LTE cells are kept; 5G (NR) and CDMA are left out (the deck's modem is
LTE Cat 1).

Usage:
    py -3 tools/cells/make_cell_db.py 230.csv.gz --out cells/
    py -3 tools/cells/make_cell_db.py --token <key> --mcc 230 --mcc 226 --out cells/
"""
import argparse
import csv
import gzip
import io
import os
import struct
import sys
import time
import urllib.request

HEADER = struct.Struct("<4sHHHHII12s")
RECORD = struct.Struct("<IIiiHHB3x")
RADIO = {"GSM": 1, "UMTS": 2, "LTE": 3}
URL = "https://opencellid.org/ocid/downloads?token={token}&type=mcc&file={mcc}.csv.gz"


def open_csv(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return io.StringIO(raw.decode("utf-8", "replace"))


def read_cells(f, by_mcc, stats):
    rd = csv.reader(f)
    for row in rd:
        if not row or row[0] == "radio":
            continue
        try:
            radio = RADIO.get(row[0].strip().upper())
            if radio is None:
                stats["skipped radio"] += 1
                continue
            mcc, mnc, area, cell = int(row[1]), int(row[2]), int(row[3]), int(row[4])
            lon, lat, rng = float(row[6]), float(row[7]), int(float(row[8] or 0))
        except (ValueError, IndexError):
            stats["bad rows"] += 1
            continue
        if not (0 < mcc < 1000 and 0 <= mnc < 1000 and 0 <= area < 2**32 and 0 < cell < 2**32):
            stats["bad rows"] += 1
            continue
        if not (-90 <= lat <= 90 and -180 <= lon <= 180):
            stats["bad rows"] += 1
            continue
        key = (radio, mnc, area, cell)
        rec = (radio, mnc, area, cell, round(lat * 1e6), round(lon * 1e6), min(max(rng, 0) // 10, 65535))
        by_mcc.setdefault(mcc, {})[key] = rec   # a cell listed twice: the later row wins
        stats["cells"] += 1


def write_db(path, mcc, cells, source):
    recs = sorted(cells.values(), key=lambda r: r[:4])
    with open(path, "wb") as f:
        f.write(HEADER.pack(b"MCDB", 1, RECORD.size, mcc, 0, len(recs), int(time.time()),
                            source.encode("ascii", "replace")[:12]))
        for radio, mnc, area, cell, lat, lon, rng in recs:
            f.write(RECORD.pack(cell, area, lat, lon, mnc, rng, radio))
    return len(recs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="*", help="OpenCellID CSV files (.csv or .csv.gz)")
    ap.add_argument("--token", help="OpenCellID API key, to download --mcc files")
    ap.add_argument("--mcc", type=int, action="append", default=[], help="country to download (repeatable)")
    ap.add_argument("--out", default="cells", help="output folder (default: cells)")
    ap.add_argument("--source", default="OpenCellID", help="source name in the header (12 chars)")
    args = ap.parse_args()

    files = list(args.csv)
    if args.mcc:
        if not args.token:
            ap.error("--mcc needs --token")
        os.makedirs(args.out, exist_ok=True)
        for mcc in args.mcc:
            dst = os.path.join(args.out, "%d.csv.gz" % mcc)
            print("downloading MCC %d ..." % mcc)
            req = urllib.request.Request(URL.format(token=args.token, mcc=mcc), headers={"User-Agent": "cell-db-tool"})
            with urllib.request.urlopen(req) as r, open(dst, "wb") as f:
                f.write(r.read())
            if open(dst, "rb").read(2) != b"\x1f\x8b":
                sys.exit("MCC %d: the server did not send a file (wrong key, or the daily limit): %s"
                         % (mcc, open(dst, "rb").read(200)))
            files.append(dst)
    if not files:
        ap.error("no input: give CSV files or --token with --mcc")

    stats = {"cells": 0, "skipped radio": 0, "bad rows": 0}
    by_mcc = {}
    for path in files:
        read_cells(open_csv(path), by_mcc, stats)
    os.makedirs(args.out, exist_ok=True)
    for mcc, cells in sorted(by_mcc.items()):
        path = os.path.join(args.out, "%d.bin" % mcc)
        n = write_db(path, mcc, cells, args.source)
        print("%s: %d cells, %.1f MB" % (path, n, os.path.getsize(path) / 1e6))
    print("rows: %(cells)d used, %(skipped radio)d other radios (NR, CDMA), %(bad rows)d bad" % stats)
    print("Copy the .bin files to /cells/ on the deck's SD card.")


if __name__ == "__main__":
    main()
