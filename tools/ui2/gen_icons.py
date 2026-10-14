#!/usr/bin/env python3
"""Generate 1-bit icon bitmaps for the Meck UI v2 from tools/ui2/icons.json.

icons.json holds the SVG path data of the design system's Icon component
(16-unit viewBox). Each icon has a "stroke" path (outlined, square caps, miter
joins) and a "fill" path (solid). The design renders them with
shape-rendering: crispEdges and a stroke width of 1 px at sizes <= 12 and
2 px above that; this script reproduces that at 12/16/24/32 px.

Output: a C header with MSB-first rows, (w+7)/8 bytes per row (the format of
HomeIcons.h and drawXbmRaw), plus an optional preview PNG.

Usage: python gen_icons.py [--out PATH.h] [--preview PATH.png]
Requires Pillow and numpy.
"""
import argparse
import json
import math
import os
import re

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "..", "..", "examples", "companion_radio", "ui-new", "ui2", "Ui2Icons.h")
SIZES = (12, 16, 24, 32)
SS = 8  # supersampling factor


# ---------------------------------------------------------------- SVG paths

def _tokens(d):
    for m in re.finditer(r"[MmLlHhVvQqAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", d):
        yield m.group(0)


def _arc_points(x1, y1, rx, ry, phi, fa, fs, x2, y2):
    """Flatten an SVG elliptical arc (endpoint parameterisation)."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if fa == fs:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not fs and dt > 0:
        dt -= 2 * math.pi
    elif fs and dt < 0:
        dt += 2 * math.pi
    n = max(8, int(abs(dt) / (math.pi / 32)))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * ex - sp * ey + cx, sp * ex + cp * ey + cy))
    return pts


def parse_path(d):
    """Return a list of subpaths: (points, closed)."""
    subs, pts = [], []
    closed = False
    x = y = sx = sy = 0.0
    toks = list(_tokens(d))
    i, cmd = 0, None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def flush():
        nonlocal pts, closed
        if len(pts) > 1:
            subs.append((pts, closed))
        pts, closed = [], False

    while i < len(toks):
        t = toks[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                closed = True
                x, y = sx, sy
                flush()
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            flush()
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy = x, y
            pts = [(x, y)]
            cmd = "l" if rel else "L"
        elif c == "L":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            pts.append((x, y))
        elif c == "H":
            v = num()
            x = x + v if rel else v
            pts.append((x, y))
        elif c == "V":
            v = num()
            y = y + v if rel else v
            pts.append((x, y))
        elif c == "Q":
            qx, qy, nx, ny = num(), num(), num(), num()
            if rel:
                qx, qy, nx, ny = x + qx, y + qy, x + nx, y + ny
            for k in range(1, 13):
                t = k / 12
                pts.append(((1 - t) ** 2 * x + 2 * (1 - t) * t * qx + t * t * nx,
                            (1 - t) ** 2 * y + 2 * (1 - t) * t * qy + t * t * ny))
            x, y = nx, ny
        elif c == "A":
            rx, ry, phi, fa, fs, nx, ny = num(), num(), num(), num(), num(), num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            pts.extend(_arc_points(x, y, rx, ry, phi, int(fa), int(fs), nx, ny))
            x, y = nx, ny
        else:
            raise ValueError("unsupported command " + cmd)
        if not pts:
            pts = [(x, y)]
    flush()
    return subs


# ------------------------------------------------------------ rasterisation

def _seg_poly(p, q, hw):
    """Rectangle around segment p-q, extended by hw at both ends (square cap)."""
    (x1, y1), (x2, y2) = p, q
    dx, dy = x2 - x1, y2 - y1
    ln = math.hypot(dx, dy)
    if ln == 0:
        return [(x1 - hw, y1 - hw), (x1 + hw, y1 - hw), (x1 + hw, y1 + hw), (x1 - hw, y1 + hw)]
    ux, uy = dx / ln * hw, dy / ln * hw
    nx, ny = -uy, ux
    return [(x1 - ux + nx, y1 - uy + ny), (x2 + ux + nx, y2 + uy + ny),
            (x2 + ux - nx, y2 + uy - ny), (x1 - ux - nx, y1 - uy - ny)]


def render(icon, size):
    scale = size / 16.0 * SS
    sw = (16.0 / size if size <= 12 else 32.0 / size)  # viewBox units
    img = Image.new("L", (size * SS, size * SS), 0)
    dr = ImageDraw.Draw(img)
    hw = sw * scale / 2
    for pts, closed in parse_path(icon.get("stroke", "")):
        sp = [(px * scale, py * scale) for px, py in pts]
        segs = list(zip(sp, sp[1:]))
        if closed:
            segs.append((sp[-1], sp[0]))
        for a, b in segs:
            dr.polygon(_seg_poly(a, b, hw), fill=255)
    for pts, _ in parse_path(icon.get("fill", "")):
        sp = [(px * scale, py * scale) for px, py in pts]
        if len(sp) >= 3:
            dr.polygon(sp, fill=255)
    a = np.asarray(img, dtype=np.float32).reshape(size, SS, size, SS) / 255.0
    cover = a.mean(axis=(1, 3))
    centre = a[:, SS // 2, :, SS // 2]
    return (cover >= 0.5) | ((centre > 0.5) & (cover >= 0.3))


def pack(bits):
    h, w = bits.shape
    bpr = (w + 7) // 8
    out = []
    for y in range(h):
        for bx in range(bpr):
            v = 0
            for b in range(8):
                x = bx * 8 + b
                if x < w and bits[y, x]:
                    v |= 0x80 >> b
            out.append(v)
    return out


def c_ident(name):
    return re.sub(r"[^A-Za-z0-9]", "_", name).upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--preview")
    args = ap.parse_args()

    with open(os.path.join(HERE, "icons.json"), encoding="utf-8") as f:
        icons = json.load(f)
    names = list(icons.keys())

    lines = [
        "#pragma once",
        "// Generated by tools/ui2/gen_icons.py from tools/ui2/icons.json. Do not edit.",
        "// 1-bit icons, MSB first, (w+7)/8 bytes per row. Draw with drawXbmRaw().",
        "",
        "#include <stdint.h>",
        "#ifdef ESP32",
        "#include <pgmspace.h>",
        "#endif",
        "",
        "enum Ui2Icon : uint8_t {",
    ]
    lines += ["  UI2_IC_%s," % c_ident(n) for n in names]
    lines += ["  UI2_IC_COUNT", "};", ""]
    # The bitmaps are compiled in one translation unit only (Ui2Assets.cpp
    # defines UI2_ICONS_IMPL); everything else just sees the enum.
    lines += ["#ifdef UI2_ICONS_IMPL", ""]

    rendered = {}
    for size in SIZES:
        for n in names:
            bits = render(icons[n], size)
            rendered[(n, size)] = bits
            data = pack(bits)
            body = ",".join("0x%02X" % v for v in data)
            lines.append("static const uint8_t ui2_ic%d_%s[] PROGMEM = {%s};" % (size, c_ident(n).lower(), body))
        lines.append("static const uint8_t* const UI2_ICONS_%d[UI2_IC_COUNT] = {" % size)
        lines += ["  ui2_ic%d_%s," % (size, c_ident(n).lower()) for n in names]
        lines += ["};", ""]

    lines += [
        "// Returns the bitmap of an icon at 12, 16, 24 or 32 px (nullptr otherwise).",
        "static inline const uint8_t* ui2IconBits(Ui2Icon id, uint8_t size) {",
        "  if (id >= UI2_IC_COUNT) return nullptr;",
        "  switch (size) {",
    ]
    lines += ["    case %d: return UI2_ICONS_%d[id];" % (s, s) for s in SIZES]
    lines += ["    default: return nullptr;", "  }", "}", "", "#endif // UI2_ICONS_IMPL", ""]

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s (%d icons x %d sizes)" % (args.out, len(names), len(SIZES)))

    if args.preview:
        cols, pad, zoom = 12, 4, 3
        cell = (sum(SIZES) + pad * (len(SIZES) + 1))
        rows = (len(names) + cols - 1) // cols
        sheet = Image.new("L", (cols * cell, rows * (max(SIZES) + 2 * pad)), 255)
        for i, n in enumerate(names):
            cx, cy = (i % cols) * cell + pad, (i // cols) * (max(SIZES) + 2 * pad) + pad
            for s in SIZES:
                bits = rendered[(n, s)]
                im = Image.fromarray(np.where(bits, 0, 255).astype(np.uint8))
                sheet.paste(im, (cx, cy))
                cx += s + pad
        sheet = sheet.resize((sheet.width * zoom, sheet.height * zoom), Image.NEAREST)
        sheet.save(args.preview)
        print("wrote preview", args.preview)


if __name__ == "__main__":
    main()
