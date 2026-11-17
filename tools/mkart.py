#!/usr/bin/env python3
"""Draw XenithOS Store thumbnails (flat style) and write them as PNG and XIMG.

    python3 tools/mkart.py            -> art/thumbs/<id>.png and art/thumbs/<id>.ximg
    python3 tools/mkart.py hostmc     -> only the ones named
    python3 tools/mkart.py logo       -> only the logo's files (see LOGO_X below)

Besides XenithOS's own apps, it draws a card for every app in server/flathub.example.json and
every game in server/steam.example.json: the name on a flat colour, and what it is under it.

Needs Pillow and the Inter font (Debian/Kali: apt install python3-pil fonts-inter).
XIMG is the console's thumbnail format; see shared/img.h.
"""
import json
import math
import os
import struct
import sys
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


# ----------------------------------------------------------------------------------- the logo
# The XenithOS logo: a moment of the console's start-up that the user picked, 992 ms in, as the
# four streaks close into a ring and the X comes together. In the middle a grey chevron and two
# red arms make an X, standing a little apart. Around it is a broken red and grey double ring,
# and outside that the streaks' ring: white, with a red quarter. (The X and the double ring come
# from the insignia picture desktop/art/logo-source.png, measured and squared up: every edge of
# the X runs at 45 degrees.)
#
# These numbers are the only copy. `python3 tools/mkart.py logo` writes shared/logo_shape.h (the
# console draws the logo with ui_logo and ui_mark, and its start-up ends on it), the website's
# SVGs and the logo pictures. XenithOS Desktop's art can take its SVG from svg_logo() below.
#
# Units are R, the double ring's outer radius, with the centre at (0, 0) and y pointing down.
# Angles are degrees counter-clockwise from 3 o'clock.
LOGO_GREY = (39, 36, 37)           # the dark grey, #272425
LOGO_GREY_SMALL = (170, 176, 186)  # in its place where the X stands alone at small sizes, #aab0ba
LOGO_X = [                         # (red, where it sits, corners): the chevron, the upper arm, the lower arm
    (False, (-0.0628, 0.0),
     [(-0.868, -0.564), (-0.404, -0.564), (0.160, 0.0), (-0.404, 0.564), (-0.868, 0.564), (-0.304, 0.0)]),
    (True, (0.0444, -0.0444),
     [(0.366, -0.564), (0.885, -0.564), (0.639, -0.318), (0.453, -0.318), (0.201, -0.066), (0.035, -0.233)]),
    (True, (0.0444, 0.0444),
     [(0.318, 0.064), (0.818, 0.564), (0.376, 0.564), (0.097, 0.285)]),
]
# The double ring, in pieces with square ends: (red, radius, width, from, to). The X's four
# corners poke out through the gaps on the diagonals.
LOGO_ARCS = [
    (True, 1.0, 0.058, 43.22, 86.0), (True, 1.0, 0.058, 94.0, 136.78),
    (True, 1.0, 0.058, 223.22, 266.0), (True, 1.0, 0.058, 274.0, 316.78),
    (False, 0.904, 0.058, 46.31, 85.0), (False, 0.904, 0.058, 95.0, 133.69),
    (False, 0.904, 0.058, 226.31, 265.0), (False, 0.904, 0.058, 275.0, 313.69),
    (True, 0.96, 0.044, 3.5, 26.26), (False, 0.96, 0.044, 333.74, 356.5),
    (False, 0.96, 0.044, 153.74, 176.5), (False, 0.96, 0.044, 183.5, 206.26),
]
# The outer ring: the start-up's four streaks, with round ends, drawn in this order so that each
# one's end lies over the one before it: (red, from, to). It's open just below 3 o'clock.
LOGO_RING_R, LOGO_RING_W = 1.30, 0.064
LOGO_RING = [(False, 248.99, 337.45), (False, 160.53, 248.99), (True, 72.47, 160.54), (False, -15.24, 72.50)]
LOGO_HALF = LOGO_RING_R + LOGO_RING_W / 2     # the logo's half-size: to the outer ring's edge


def logo_x_box():
    """The box around the X alone, its pieces where they sit."""
    pts = [(x + ox, y + oy) for _, (ox, oy), poly in LOGO_X for x, y in poly]
    return min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts)


def ring_point(r, deg):
    return r * math.cos(math.radians(deg)), -r * math.sin(math.radians(deg))


def insignia(d, cx, cy, R, grey=LOGO_GREY, red=RED, rings=True):
    """Draws the logo (or, without rings, the X alone) on a PIL ImageDraw, R pixels to the double ring."""
    if rings:
        w = LOGO_RING_W * R
        ro = LOGO_RING_R * R + w / 2
        for is_red, a0, a1 in LOGO_RING:
            colour = red if is_red else TEXT
            # PIL's angles go clockwise, ours counter-clockwise
            d.arc([cx - ro, cy - ro, cx + ro, cy + ro], (-a1) % 360, (-a0) % 360, fill=colour, width=max(1, round(w)))
            for a in (a0, a1):                       # the round ends
                x, y = ring_point(LOGO_RING_R * R, a)
                d.ellipse([cx + x - w / 2, cy + y - w / 2, cx + x + w / 2, cy + y + w / 2], fill=colour)
        for is_red, r, aw, a0, a1 in LOGO_ARCS:
            ar = (r + aw / 2) * R
            d.arc([cx - ar, cy - ar, cx + ar, cy + ar], (-a1) % 360, (-a0) % 360, fill=red if is_red else grey,
                  width=max(1, round(aw * R)))
    for is_red, (ox, oy), poly in LOGO_X:
        d.polygon([(cx + (x + ox) * R, cy + (y + oy) * R) for x, y in poly], fill=red if is_red else grey)


def mark(d, cx, cy, size, color=LOGO_GREY_SMALL, dot=RED):
    """The X alone, size pixels high, centred on (cx, cy): for thumbnails and small pictures."""
    x0, y0, x1, y1 = logo_x_box()
    R = size / (y1 - y0)
    insignia(d, cx - (x0 + x1) / 2 * R, cy - (y0 + y1) / 2 * R, R, grey=color, red=dot, rings=False)


def hexc(rgb):
    return "#%02x%02x%02x" % rgb


def svg_logo(scale, grey=LOGO_GREY, rings=True):
    """The logo (or, without rings, the X alone) as SVG elements around (0, 0), R = scale units."""
    out, fills = [], {}
    if rings:
        r = LOGO_RING_R * scale
        for is_red, a0, a1 in LOGO_RING:
            (ax, ay), (bx, by) = ring_point(r, a0), ring_point(r, a1)
            out.append(f'<path fill="none" stroke="{hexc(RED if is_red else TEXT)}" stroke-width="{LOGO_RING_W * scale:.1f}" '
                       f'stroke-linecap="round" d="M{ax:.1f} {ay:.1f} A{r:.1f} {r:.1f} 0 0 0 {bx:.1f} {by:.1f}"/>')
        for is_red, rr, aw, a0, a1 in LOGO_ARCS:
            ro, ri = (rr + aw / 2) * scale, (rr - aw / 2) * scale
            (ax, ay), (bx, by), (cx, cy), (dx, dy) = ring_point(ro, a0), ring_point(ro, a1), ring_point(ri, a1), ring_point(ri, a0)
            fills.setdefault(RED if is_red else grey, []).append(
                f"M{ax:.1f} {ay:.1f} A{ro:.1f} {ro:.1f} 0 0 0 {bx:.1f} {by:.1f} L{cx:.1f} {cy:.1f} "
                f"A{ri:.1f} {ri:.1f} 0 0 1 {dx:.1f} {dy:.1f} Z")
    for is_red, (ox, oy), poly in LOGO_X:
        fills.setdefault(RED if is_red else grey, []).append(
            "M" + " L".join(f"{(x + ox) * scale:.1f} {(y + oy) * scale:.1f}" for x, y in poly) + " Z")
    out += [f'<path fill="{hexc(c)}" d="{" ".join(ds)}"/>' for c, ds in fills.items()]
    return "".join(out)


def logo_svgs():
    """server/web/static: insignia.svg (the logo), mark.svg (the X alone, for small sizes) and
    logo.svg (the site's icon: the X on a dark rounded square)."""
    web = os.path.join(ROOT, "server", "web", "static")
    s = 1000                                        # R in SVG units
    e = round(LOGO_HALF * s) + 12                   # half the box
    with open(os.path.join(web, "insignia.svg"), "w", newline="\n") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-e} {-e} {2 * e} {2 * e}">{svg_logo(s)}</svg>\n')
    x0, y0, x1, y1 = logo_x_box()
    pad = 0.02
    with open(os.path.join(web, "mark.svg"), "w", newline="\n") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{(x0 - pad) * s:.0f} {(y0 - pad) * s:.0f} '
                f'{(x1 - x0 + 2 * pad) * s:.0f} {(y1 - y0 + 2 * pad) * s:.0f}">{svg_logo(s, LOGO_GREY_SMALL, rings=False)}</svg>\n')
    k = 46 / ((x1 - x0) * s)                        # the icon: 64 units square, the X 46 wide in the middle
    with open(os.path.join(web, "logo.svg"), "w", newline="\n") as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">'
                '<rect width="64" height="64" rx="14" fill="#07080a"/>'
                f'<g transform="translate({32 - (x0 + x1) / 2 * s * k:.2f} {32 - (y0 + y1) / 2 * s * k:.2f}) scale({k:.5f})">'
                f'{svg_logo(s, LOGO_GREY_SMALL, rings=False)}</g></svg>\n')


def logo_header():
    """shared/logo_shape.h: the same numbers for the console (ui_logo and ui_mark in shared/ui.c)."""
    def screen(a0, a1):
        """Counter-clockwise degrees to the screen's clockwise radians, a0 < a1, a0 in [0, 2 pi)."""
        s0, s1 = math.radians(-a1), math.radians(-a0)
        while s0 < 0:
            s0, s1 = s0 + 2 * math.pi, s1 + 2 * math.pi
        return s0, s1
    def arc_c(is_red, r, w, a0, a1):
        s0, s1 = screen(a0, a1)
        return f"    {{{int(is_red)}, {r:.3f}f, {w:.3f}f, {s0:.5f}f, {s1:.5f}f}},"
    x0, y0, x1, y1 = logo_x_box()
    lines = [
        "/* The XenithOS logo's shapes. Written by tools/mkart.py from its LOGO_* numbers: change those",
        " * and run `python3 tools/mkart.py logo`, not this file.",
        " *",
        " * Units are R, the double ring's outer radius, with the centre at (0, 0) and y pointing down.",
        " * Arc angles are radians clockwise from 3 o'clock, the way the screen goes. */",
        "#ifndef LOGO_SHAPE_H",
        "#define LOGO_SHAPE_H",
        "",
        "struct logo_poly { unsigned char red, n; float ox, oy; float xy[12]; };   /* ox, oy: where it sits */",
        "struct logo_arc { unsigned char red; float r, w, a0, a1; };",
        "",
        "/* The X: the grey chevron, the upper red arm and the lower red arm. */",
        f"static const struct logo_poly logo_x[{len(LOGO_X)}] = {{",
        *[f"    {{{int(r)}, {len(p)}, {ox:.4f}f, {oy:.4f}f, {{{', '.join(f'{x:.4f}f, {y:.4f}f' for x, y in p)}}}}},"
          for r, (ox, oy), p in LOGO_X],
        "};",
        "",
        "/* The double ring, in pieces with square ends. */",
        f"static const struct logo_arc logo_arcs[{len(LOGO_ARCS)}] = {{",
        *[arc_c(*a) for a in LOGO_ARCS],
        "};",
        "",
        "/* The outer ring: the start-up's four streaks, with round ends, in the order they're drawn. */",
        f"static const struct logo_arc logo_ring[{len(LOGO_RING)}] = {{",
        *[arc_c(r, LOGO_RING_R, LOGO_RING_W, a0, a1) for r, a0, a1 in LOGO_RING],
        "};",
        "",
        f"#define LOGO_HALF     {LOGO_HALF:.3f}f      /* the logo's half-size, to the outer ring's edge */",
        f"#define LOGO_X_LEFT   ({x0:.4f}f)  /* the box around the X alone */",
        f"#define LOGO_X_TOP    ({y0:.4f}f)",
        f"#define LOGO_X_RIGHT  {x1:.4f}f",
        f"#define LOGO_X_BOTTOM {y1:.4f}f",
        f"#define LOGO_GREY       RGB({LOGO_GREY[0]}, {LOGO_GREY[1]}, {LOGO_GREY[2]})      /* the logo's dark grey */",
        f"#define LOGO_GREY_SMALL RGB({LOGO_GREY_SMALL[0]}, {LOGO_GREY_SMALL[1]}, {LOGO_GREY_SMALL[2]})   /* the X alone, at small sizes */",
        "",
        "#endif",
        "",
    ]
    with open(os.path.join(ROOT, "shared", "logo_shape.h"), "w", newline="\n") as f:
        f.write("\n".join(lines))


def logo_files():
    """Everything drawn from the logo's numbers: shared/logo_shape.h, the website's SVGs, and
    art/logo.png (on transparent), art/logo-on-dark.png and art/logo-wide.png."""
    logo_header()
    logo_svgs()
    big = 2048
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    insignia(ImageDraw.Draw(img), big / 2, big / 2, big * 0.47 / LOGO_HALF)
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "art", "logo.png"))
    dark = Image.new("RGB", (big, big), (7, 8, 10))
    insignia(ImageDraw.Draw(dark), big / 2, big / 2, big * 0.40 / LOGO_HALF)
    dark.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, "art", "logo-on-dark.png"))
    k = 4                                           # drawn 4x and scaled down, for smooth edges
    wide = Image.new("RGB", (1600 * k, 480 * k), (7, 8, 10))
    d = ImageDraw.Draw(wide)
    insignia(d, 280 * k, 240 * k, 200 * k / LOGO_HALF)
    name = font("Bold", 150 * k)
    top, bottom = d.textbbox((0, 0), "XenithOS", font=name)[1::2]
    d.text((530 * k, 240 * k - (top + bottom) / 2), "XenithOS", font=name, fill=TEXT)   # level with the logo
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


def music():
    k = 4                                           # drawn 4x and scaled down, for round edges
    img = Image.new("RGB", (W * k, H * k), BG_DARK)
    d = ImageDraw.Draw(img)
    # A flat record: dark grooves, the red label, the hole.
    cx, cy, r = 58 * k, 68 * k, 34 * k
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(38, 43, 51))
    for g in (27, 21):
        d.ellipse([cx - g * k, cy - g * k, cx + g * k, cy + g * k], outline=(27, 31, 38), width=2 * k)
    d.ellipse([cx - 12 * k, cy - 12 * k, cx + 12 * k, cy + 12 * k], fill=RED)
    d.ellipse([cx - 3 * k, cy - 3 * k, cx + 3 * k, cy + 3 * k], fill=BG_DARK)
    d.text((108 * k, 34 * k), "MUSIC", font=font("Black", 50 * k), fill=TEXT)
    d.text((26 * k, 128 * k), "Your songs on the TV", font=font("Medium", 20 * k), fill=SUB)
    return img.resize((W, H), Image.LANCZOS)


def hostmc():
    img = Image.new("RGB", (W, H), BG_DARK)
    d = ImageDraw.Draw(img)
    d.text((22, 34), "HostMC", font=font("Black", 64), fill=TEXT)
    d.text((26, 128), "Your Minecraft server", font=font("Medium", 20), fill=SUB)
    return img


def xenith():
    k = 4                                           # drawn 4x and scaled down, for smooth edges
    img = Image.new("RGB", (W * k, H * k), BG_DARK)
    d = ImageDraw.Draw(img)
    insignia(d, 68 * k, 90 * k, 52 * k / LOGO_HALF)
    d.text((132 * k, 46 * k), "XenithOS", font=font("Bold", 40 * k), fill=TEXT)
    d.text((134 * k, 122 * k), "System update", font=font("Medium", 20 * k), fill=SUB)
    return img.resize((W, H), Image.LANCZOS)


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
    only = set(sys.argv[1:])
    os.makedirs(OUT, exist_ok=True)
    art = {
        "volt": volt(),
        "doom": doom(),
        "tube": tube(),
        "photos": photos(),
        "music": music(),
        "hostmc": hostmc(),
        "freedoom1": freedoom(1, (19, 32, 26), (143, 181, 154)),
        "freedoom2": freedoom(2, (35, 20, 20), (214, 160, 143)),
        "xenith": xenith(),
        "xenithos-pc": xenith(),
    }
    art.update(catalog_art())
    if not only or "logo" in only:                  # "logo": the logo's files alone
        logo_files()
    for name, img in art.items():
        if only and name not in only:
            continue
        img.save(os.path.join(OUT, f"{name}.png"))
        write_ximg(img, os.path.join(OUT, f"{name}.ximg"))
        print(f"{name}: {os.path.getsize(os.path.join(OUT, name + '.ximg'))} bytes")


if __name__ == "__main__":
    main()
