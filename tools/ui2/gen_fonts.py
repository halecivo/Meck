#!/usr/bin/env python3
"""Generate the Adafruit GFX bitmap fonts used by the Meck UI v2.

The design system uses Noto Sans (regular and bold) for text and Montserrat
Bold for numerals, at exact pixel sizes. This script renders them with
FreeType (through Pillow, monochrome hinting) and writes one GFXfont header
per face/size into examples/companion_radio/ui-new/ui2/fonts/.

The glyph table covers U+0020..last and is indexed by code point, the same
convention as the existing *8b fonts in ui-new/fonts/ (U+0100..U+017F holds
the Czech letters). Code points that are not in a face's character set get an
empty glyph.

The TTF files are not stored in the repository. Download them into
tools/ui2/fonts/ first (both are SIL Open Font License 1.1, see OFL-*.txt):
  NotoSans-Regular.ttf, NotoSans-Bold.ttf
    https://github.com/notofonts/notofonts.github.io/tree/main/fonts/NotoSans/hinted/ttf
  Montserrat-Bold.ttf
    https://github.com/JulietaUla/Montserrat/tree/master/fonts/ttf

Usage: python gen_fonts.py [--preview PATH.png]
Requires Pillow.
"""
import argparse
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, "fonts")
OUT_DIR = os.path.join(HERE, "..", "..", "examples", "companion_radio", "ui-new", "ui2", "fonts")

LATIN = (0x20, 0x17F)      # ASCII + Latin-1 + Latin Extended-A
ASCII_DEG = (0x20, 0xB0)   # ASCII + degree sign
ASCII = (0x20, 0x7E)
NUMERALS = set("0123456789 .,:%+-/°")

# (C name, TTF file, pixel size, code point range, optional character subset)
FACES = [
    ("Ui2NotoSans10", "NotoSans-Regular.ttf", 10, LATIN, None),
    ("Ui2NotoSans11", "NotoSans-Regular.ttf", 11, LATIN, None),
    ("Ui2NotoSans12", "NotoSans-Regular.ttf", 12, LATIN, None),
    ("Ui2NotoSansBold9", "NotoSans-Bold.ttf", 9, ASCII, None),
    ("Ui2NotoSansBold10", "NotoSans-Bold.ttf", 10, LATIN, None),
    ("Ui2NotoSansBold11", "NotoSans-Bold.ttf", 11, LATIN, None),
    ("Ui2NotoSansBold12", "NotoSans-Bold.ttf", 12, LATIN, None),
    ("Ui2NotoSansBold14", "NotoSans-Bold.ttf", 14, LATIN, None),
    ("Ui2NotoSansBold16", "NotoSans-Bold.ttf", 16, LATIN, None),
    ("Ui2NotoSansBold18", "NotoSans-Bold.ttf", 18, LATIN, None),
    ("Ui2Montserrat9", "Montserrat-Bold.ttf", 9, ASCII_DEG, None),
    ("Ui2Montserrat10", "Montserrat-Bold.ttf", 10, ASCII_DEG, None),
    ("Ui2Montserrat11", "Montserrat-Bold.ttf", 11, ASCII_DEG, None),
    ("Ui2Montserrat16", "Montserrat-Bold.ttf", 16, ASCII_DEG, None),
    ("Ui2Montserrat22", "Montserrat-Bold.ttf", 22, ASCII_DEG, NUMERALS),
    ("Ui2Montserrat40", "Montserrat-Bold.ttf", 40, ASCII_DEG, NUMERALS),
]


# Faces rendered anti-aliased, with the coverage threshold (0-255) that
# turns a grey pixel black. Monochrome hinting suits the hinted Noto Sans,
# with one exception: at 12 px bold it rounds some stems (T, E) down to one
# pixel, so that size reads half bold; a 38 % threshold keeps it even.
# Montserrat's outlines collapse under hinting at small sizes (the 11 px
# clock read "14:1B"), so it is thresholded at 50 %.
AA_THRESHOLD = {"Ui2NotoSansBold12": 96}


def aa_threshold(name, ttf):
    if name in AA_THRESHOLD:
        return AA_THRESHOLD[name]
    return 128 if ttf.startswith("Montserrat") else None


def render_glyph(font, ch, threshold=None):
    """Return (bits rows, width, height, xOffset, yOffset, xAdvance).

    threshold None renders with monochrome hinting; a number renders
    anti-aliased and keeps pixels with at least that coverage.
    """
    adv = int(round(font.getlength(ch)))
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    if x1 <= x0 or y1 <= y0:
        return [], 0, 0, 0, 0, adv
    pad = 4
    size = (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad)
    if threshold is not None:
        grey = Image.new("L", size, 0)
        d = ImageDraw.Draw(grey)
        d.fontmode = "L"
        d.text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
        img = grey.point(lambda v: 255 if v >= threshold else 0).convert("1")
    else:
        img = Image.new("1", size, 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        d.text((pad - x0, pad - y0), ch, font=font, fill=1, anchor="ls")
    box = img.getbbox()
    if not box:
        return [], 0, 0, 0, 0, adv
    cx0, cy0, cx1, cy1 = box
    w, h = cx1 - cx0, cy1 - cy0
    px = img.load()
    rows = [[1 if px[cx0 + x, cy0 + y] else 0 for x in range(w)] for y in range(h)]
    return rows, w, h, x0 + (cx0 - pad), y0 + (cy0 - pad), adv


def build(name, ttf, size, rng, subset):
    font = ImageFont.truetype(os.path.join(FONT_DIR, ttf), size, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    first, last = rng
    bitmap, glyphs, rendered = [], [], {}
    for cp in range(first, last + 1):
        ch = chr(cp)
        if (0x7F <= cp <= 0x9F) or (subset is not None and ch not in subset):
            glyphs.append((len(bitmap), 0, 0, 0, 0, 0, cp))
            continue
        rows, w, h, xo, yo, adv = render_glyph(font, ch, threshold=aa_threshold(name, ttf))
        offset = len(bitmap)
        acc, nbits = 0, 0
        for r in rows:
            for b in r:
                acc = (acc << 1) | b
                nbits += 1
                if nbits == 8:
                    bitmap.append(acc)
                    acc, nbits = 0, 0
        if nbits:
            bitmap.append(acc << (8 - nbits))
        glyphs.append((offset, w, h, adv, xo, yo, cp))
        rendered[cp] = (rows, xo, yo, adv)
    if len(bitmap) > 0xFFFF:
        raise SystemExit("%s: bitmap too large for uint16 offsets" % name)

    guard = name.upper() + "_H"
    out = [
        "#ifndef " + guard,
        "#define " + guard,
        "// Generated by tools/ui2/gen_fonts.py. Do not edit.",
        "// %s %d px, U+%04X..U+%04X%s. SIL Open Font License 1.1." % (
            ttf, size, first, last, " (numerals subset)" if subset else ""),
        "",
        "#include <Adafruit_GFX.h>",
        "",
        "const uint8_t %sBitmaps[] PROGMEM = {" % name,
    ]
    for i in range(0, len(bitmap), 16):
        out.append("  " + ", ".join("0x%02X" % b for b in bitmap[i:i + 16]) + ",")
    out.append("  0x00 };")
    out.append("")
    out.append("const GFXglyph %sGlyphs[] PROGMEM = {" % name)
    for (off, w, h, adv, xo, yo, cp) in glyphs:
        out.append("  { %5d, %3d, %3d, %3d, %4d, %4d },   // 0x%02X" % (off, w, h, adv, xo, yo, cp))
    out.append("};")
    out.append("")
    out.append("const GFXfont %s PROGMEM = {" % name)
    out.append("  (uint8_t  *)%sBitmaps," % name)
    out.append("  (GFXglyph *)%sGlyphs," % name)
    out.append("  0x%02X, 0x%02X, %d };" % (first, last, ascent + descent))
    out.append("")
    out.append("#endif // " + guard)
    out.append("")
    path = os.path.join(OUT_DIR, name + ".h")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print("%-20s %3d px  %5d bitmap bytes  asc %d desc %d" % (name, size, len(bitmap), ascent, descent))
    return rendered


def preview(fonts, path):
    lines = [
        ("Ui2NotoSansBold14", "Prague Hams and Friends (EU narrow)"),
        ("Ui2NotoSansBold12", "Ing. Vaclav Novotny-Svoboda ml. 12B"),
        ("Ui2NotoSans12", "Příliš žluťoučký kůň úpěl ďábelské ódy"),
        ("Ui2NotoSans12", "Mise à jour du firmware impossible : œuf"),
        ("Ui2NotoSans11", "Last packet SNR +6.5 RSSI -94 (11)"),
        ("Ui2NotoSans10", "Meta 10 · 2h · 0 hops · Préréglage"),
        ("Ui2NotoSansBold9", "W/S A/D Alt Q Enter (9B keycaps)"),
        ("Ui2Montserrat11", "20:04"),
        ("Ui2Montserrat16", "14° 3 on 14:30 34%"),
        ("Ui2Montserrat22", "869.618 100 %"),
        ("Ui2Montserrat40", "-12.5°"),
    ]
    img = Image.new("L", (240, 240), 255)
    y = 4
    for name, text in lines:
        rendered = fonts[name]
        x = 4
        asc = max((-yo for (_, _, yo, _) in rendered.values()), default=10)
        base = y + asc
        for ch in text:
            g = rendered.get(ord(ch))
            if not g:
                continue
            rows, xo, yo, adv = g
            for ry, r in enumerate(rows):
                for rx, b in enumerate(r):
                    if b and 0 <= x + xo + rx < 240 and 0 <= base + yo + ry < 240:
                        img.putpixel((x + xo + rx, base + yo + ry), 0)
            x += adv
        y = base + 6
    img.resize((720, 720), Image.NEAREST).save(path)
    print("wrote preview", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    args = ap.parse_args()
    os.makedirs(OUT_DIR, exist_ok=True)
    fonts = {}
    for name, ttf, size, rng, subset in FACES:
        fonts[name] = build(name, ttf, size, rng, subset)
    if args.preview:
        preview(fonts, args.preview)


if __name__ == "__main__":
    main()
