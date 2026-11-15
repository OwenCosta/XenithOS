#!/usr/bin/env python3
"""Draw XenithOS Store thumbnails (flat style) and write them as PNG and XIMG.

    python3 tools/mkart.py            -> art/thumbs/<id>.png and art/thumbs/<id>.ximg

Besides XenithOS's own apps, it draws a card for every app in server/flathub.example.json and
every game in server/steam.example.json: the name on a flat colour, and what it is under it.

Needs Pillow and the Inter font (Debian/Kali: apt install python3-pil fonts-inter).
XIMG is the console's thumbnail format; see shared/img.h.
"""
import json
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
    img = Image.new("RGB", (W, H), (12, 14, 18))
    d = ImageDraw.Draw(img)
    cell = 14
    # A light trail on a grid, like the game: blue body, white head, yellow sparks.
    path = [(3, 2), (4, 2), (5, 2), (6, 2), (7, 2), (8, 2), (8, 3), (8, 4), (9, 4), (10, 4), (11, 4), (12, 4),
            (13, 4), (13, 5), (13, 6), (14, 6), (15, 6), (16, 6), (17, 6), (18, 6)]
    ox, oy = 14, 10
    for i, (cx, cy) in enumerate(path):
        color = (241, 243, 246) if i == len(path) - 1 else (229, 9, 20)
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


# The theme's colours (shared/theme.h)
BG_DARK = (12, 14, 18)
TEXT = (241, 243, 246)
SUB = (139, 147, 160)
RED = (229, 9, 20)


def mark(d, cx, cy, size, color=TEXT, dot=RED):
    """The XenithOS mark (shared/ui.c ui_logo): two chevrons that make an X which doesn't quite
    meet, and a red point where it would."""
    arms = [((0, -0.15), (-0.36, -0.42)), ((0, -0.15), (0.36, -0.42)),
            ((0, 0.15), (-0.36, 0.42)), ((0, 0.15), (0.36, 0.42))]
    r = size * 0.06                                  # half the stroke
    for (ix, iy), (ox, oy) in arms:
        ax, ay = cx + ix * size, cy + iy * size
        bx, by = cx + ox * size, cy + oy * size
        length = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
        nx, ny = -(by - ay) / length * r, (bx - ax) / length * r   # across the stroke
        d.polygon([(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)], fill=color)
        for x, y in ((ax, ay), (bx, by)):            # round caps
            d.ellipse([x - r, y - r, x + r, y + r], fill=color)
    r = size * 0.055
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=dot)


def logo_files():
    """art/logo.png (the mark on transparent), art/logo-on-dark.png and art/logo-wide.png."""
    big = 2048
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    mark(ImageDraw.Draw(img), big / 2, big / 2, big * 0.9)
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "art", "logo.png"))
    dark = Image.new("RGB", (big, big), (7, 8, 10))
    mark(ImageDraw.Draw(dark), big / 2, big / 2, big * 0.62)
    dark.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "art", "logo-on-dark.png"))
    k = 4                                           # drawn 4x and scaled down, for smooth edges
    wide = Image.new("RGB", (1600 * k, 480 * k), (7, 8, 10))
    d = ImageDraw.Draw(wide)
    mark(d, 330 * k, 240 * k, 260 * k)
    name = font("Bold", 150 * k)
    top, bottom = d.textbbox((0, 0), "XenithOS", font=name)[1::2]
    d.text((500 * k, 240 * k - (top + bottom) / 2), "XenithOS", font=name, fill=TEXT)   # level with the mark
    wide.resize((1600, 480), Image.LANCZOS).save(os.path.join(ROOT, "art", "logo-wide.png"))


def tube():
    img = Image.new("RGB", (W, H), BG_DARK)
    d = ImageDraw.Draw(img)
    d.polygon([(26, 44), (26, 100), (74, 72)], fill=RED)
    d.text((92, 34), "TUBE", font=font("Black", 66), fill=TEXT)
    d.text((26, 128), "Videos from YouTube", font=font("Medium", 20), fill=SUB)
    return img


def photos():
    img = Image.new("RGB", (W, H), BG_DARK)
    d = ImageDraw.Draw(img)
    # A flat picture frame: a sun and two hills.
    d.rounded_rectangle([24, 40, 92, 96], radius=8, fill=(38, 43, 51))
    d.ellipse([66, 48, 80, 62], fill=RED)
    d.polygon([(24, 96), (46, 66), (64, 96)], fill=TEXT)
    d.polygon([(48, 96), (70, 74), (92, 96)], fill=SUB)
    d.text((108, 34), "PHOTOS", font=font("Black", 50), fill=TEXT)
    d.text((26, 128), "Your pictures on the TV", font=font("Medium", 20), fill=SUB)
    return img


def xenith():
    img = Image.new("RGB", (W, H), BG_DARK)
    d = ImageDraw.Draw(img)
    mark(d, 70, 76, 84)
    d.text((124, 48), "XenithOS", font=font("Bold", 40), fill=TEXT)
    d.text((126, 128), "System update", font=font("Medium", 20), fill=SUB)
    return img


# What each catalog item is, under its name, and the colour it sits on (by kind of thing).
BG = {"game": (17, 27, 52), "media": (34, 20, 48), "social": (12, 38, 44), "emulator": (46, 20, 24),
      "steam": (20, 32, 48), "stream": (14, 34, 30)}
ABOUT = {
    "roblox": ("game", "Through Sober"), "minecraft": ("game", "Java Edition"),
    "kodi": ("media", "Media center"), "firefox": ("social", "Web browser"), "spotify": ("media", "Music and podcasts"),
    "discord": ("social", "Chat and voice"), "moonlight": ("stream", "Game streaming"),
    "chiaki": ("stream", "PlayStation Remote Play"), "heroic": ("game", "Epic, GOG and Amazon games"),
    "retroarch": ("emulator", "Emulators"), "dolphin": ("emulator", "GameCube and Wii"), "ppsspp": ("emulator", "PSP"),
    "supertuxkart": ("game", "Kart racing"), "luanti": ("game", "Block building"), "supertux": ("game", "Platformer"),
    "xonotic": ("game", "Arena shooter"), "tuxracer": ("game", "Racing"), "vlc": ("media", "Video player"),
    "steam": ("steam", "Big Picture"),
}


def fit(d, text, weight, start, smallest, width):
    """The biggest size from start down to smallest at which text fits in width, or None."""
    for size in range(start, smallest - 1, -2):
        f = font(weight, size)
        if d.textlength(text, font=f) <= width:
            return f
    return None


def card(name, sub, bg):
    """A flat card: the name as big as fits (on two lines if it must), what it is under it."""
    img = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(img)
    width = W - 44
    f = fit(d, name, "Black", 56, 30, width)
    if f:
        lines = [name]
    else:
        words = name.split()
        best = None
        for cut in range(1, len(words)):
            a, b = " ".join(words[:cut]), " ".join(words[cut:])
            g = fit(d, max(a, b, key=len), "Black", 44, 24, width)
            if g and (best is None or g.size > best[0].size):
                best = (g, [a, b])
        f, lines = best if best else (font("Black", 24), [name])
    line_h = int(f.size * 1.08)
    sf = fit(d, sub, "Medium", 20, 14, width) or font("Medium", 14)
    block = line_h * len(lines) + 12 + sf.size
    y = (H - block) // 2 - 4
    for line in lines:
        d.text((22, y), line, font=f, fill=(236, 241, 250))
        y += line_h
    d.text((24, y + 12), sub, font=sf, fill=(170, 182, 206))
    return img


def catalog_art():
    here = os.path.join(ROOT, "server")
    art = {}
    with open(os.path.join(here, "flathub.example.json"), encoding="utf-8") as f:
        for a in json.load(f):
            kind, sub = ABOUT.get(a["id"], ("game", "App"))
            art[a["id"]] = card(a["name"], sub, BG[kind])
    with open(os.path.join(here, "steam.example.json"), encoding="utf-8") as f:
        for g in json.load(f):
            art[g["id"]] = card(g["name"], "On Steam, free to play" if g.get("free") else "On Steam", BG["steam"])
    return art


def main():
    os.makedirs(OUT, exist_ok=True)
    art = {
        "volt": volt(),
        "doom": doom(),
        "tube": tube(),
        "photos": photos(),
        "freedoom1": freedoom(1, (19, 32, 26), (143, 181, 154)),
        "freedoom2": freedoom(2, (35, 20, 20), (214, 160, 143)),
        "xenith": xenith(),
        "xenithos-pc": xenith(),
    }
    art.update(catalog_art())
    logo_files()
    for name, img in art.items():
        img.save(os.path.join(OUT, f"{name}.png"))
        write_ximg(img, os.path.join(OUT, f"{name}.ximg"))
        print(f"{name}: {os.path.getsize(os.path.join(OUT, name + '.ximg'))} bytes")


if __name__ == "__main__":
    main()
