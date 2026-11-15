#!/usr/bin/env python3
"""Draw XenithOS Store thumbnails (flat style) and write them as PNG and XIMG.

    python3 tools/mkart.py            -> art/thumbs/<id>.png and art/thumbs/<id>.ximg

Needs Pillow and the Inter font (Debian/Kali: apt install python3-pil fonts-inter).
XIMG is the console's thumbnail format; see shared/img.h.
"""
import os
import struct
import zlib

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "art", "thumbs")
FONTS = "/usr/share/fonts/opentype/inter"
W, H = 320, 180


def font(weight, size):
    return ImageFont.truetype(os.path.join(FONTS, f"Inter-{weight}.otf"), size)


def write_ximg(img, path):
    rgba = img.convert("RGBA")
    raw = rgba.tobytes()
    head = b"XIMG" + struct.pack(">HHBBHI", rgba.width, rgba.height, 1, 1, 0, len(raw))
    with open(path, "wb") as f:
        f.write(head + zlib.compress(raw, 9))


# The 5x7 pixel font used for the XenithOS wordmark (same data as shared/gfx.c).
PIX = {
    "X": [17, 17, 10, 4, 10, 17, 17], "E": [31, 16, 16, 30, 16, 16, 31], "N": [17, 17, 25, 21, 19, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14], "T": [31, 4, 4, 4, 4, 4, 4], "H": [17, 17, 17, 31, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14], "S": [15, 16, 16, 14, 1, 1, 30],
}


def wordmark(d, x, y, s):
    for i, ch in enumerate("XENITHOS"):
        color = (47, 125, 255) if i >= 6 else (236, 241, 250)
        for row, bits in enumerate(PIX[ch]):
            for col in range(5):
                if bits & (16 >> col):
                    px = x + (i * 6 + col) * s
                    py = y + row * s
                    d.rectangle([px, py, px + s - 1, py + s - 1], fill=color)


def volt():
    img = Image.new("RGB", (W, H), (11, 18, 32))
    d = ImageDraw.Draw(img)
    cell = 14
    # A light trail on a grid, like the game: blue body, white head, yellow sparks.
    path = [(3, 2), (4, 2), (5, 2), (6, 2), (7, 2), (8, 2), (8, 3), (8, 4), (9, 4), (10, 4), (11, 4), (12, 4),
            (13, 4), (13, 5), (13, 6), (14, 6), (15, 6), (16, 6), (17, 6), (18, 6)]
    ox, oy = 14, 10
    for i, (cx, cy) in enumerate(path):
        color = (236, 241, 250) if i == len(path) - 1 else (47, 125, 255)
        d.rounded_rectangle([ox + cx * cell + 1, oy + cy * cell + 1, ox + cx * cell + cell - 2, oy + cy * cell + cell - 2],
                            radius=3, fill=color)
    for sx, sy in [(20, 3), (16, 9), (6, 7)]:
        d.rounded_rectangle([ox + sx * cell + 2, oy + sy * cell + 2, ox + sx * cell + cell - 3, oy + sy * cell + cell - 3],
                            radius=2, fill=(255, 214, 90))
    d.text((22, 108), "VOLT", font=font("Black", 52), fill=(236, 241, 250))
    return img


def doom():
    img = Image.new("RGB", (W, H), (26, 13, 11))
    d = ImageDraw.Draw(img)
    d.text((22, 30), "DOOM", font=font("Black", 88), fill=(233, 220, 207))
    d.text((24, 136), "doomgeneric for XenithOS", font=font("Medium", 18), fill=(184, 154, 136))
    return img


def freedoom(phase, bg, sub):
    img = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(img)
    d.text((22, 50), "FREEDOOM", font=font("Black", 50), fill=(232, 236, 228))
    d.text((24, 112), f"Phase {phase}", font=font("SemiBold", 28), fill=sub)
    return img


def xenith():
    img = Image.new("RGB", (W, H), (9, 13, 22))
    d = ImageDraw.Draw(img)
    s = 5
    w = 8 * 6 * s - s
    wordmark(d, (W - w) // 2, 58, s)
    t = "System update"
    f = font("Medium", 20)
    d.text(((W - d.textlength(t, font=f)) // 2, 112), t, font=f, fill=(156, 168, 192))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    art = {
        "volt": volt(),
        "doom": doom(),
        "freedoom1": freedoom(1, (19, 32, 26), (143, 181, 154)),
        "freedoom2": freedoom(2, (35, 20, 20), (214, 160, 143)),
        "xenith": xenith(),
    }
    for name, img in art.items():
        img.save(os.path.join(OUT, f"{name}.png"))
        write_ximg(img, os.path.join(OUT, f"{name}.ximg"))
        print(f"{name}: {os.path.getsize(os.path.join(OUT, name + '.ximg'))} bytes")


if __name__ == "__main__":
    main()
