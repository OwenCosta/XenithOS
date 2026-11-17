#!/usr/bin/env python3
"""Draws XenithOS Desktop's art around the XenithOS logo (logo.svg, and logo-x.svg for sizes too
small for its rings; both drawn by draw-logo.py from the insignia's numbers): the icons, the
icons XenithOS Console's own apps have in the Apps menu, the wallpapers, the start-up screen, the
boot menu's background and highlight, the window borders, and the tiles XenithOS Console shows
for the installer and PC Settings. Flat and dark:
near-black, cards #12151a with #262b33 edges, white, and the logo's red.

    python3 desktop/art/make-art.py OUT_DIR [TOOLS_DIR]

Writes OUT_DIR/rootfs/... (files for the system), OUT_DIR/live/... (only on the ISO) and
OUT_DIR/boot/... (the ISO's boot menu). TOOLS_DIR is the repository's tools/ (found by itself
when this runs from the checkout). Needs Pillow, rsvg-convert (librsvg2-bin) and the Inter font
(apt install python3-pil librsvg2-bin fonts-inter).
"""
import io
import math
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

OUT = os.path.abspath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "tools"))
import mkart  # noqa: E402  font(), write_ximg() and XenithOS's colours

LOGO = os.path.join(HERE, "logo.svg")          # the whole logo
LOGO_X = os.path.join(HERE, "logo-x.svg")      # the X alone
NEAR_BLACK = (7, 8, 10)     # #07080a
CARD = (18, 21, 26)         # #12151a
EDGE = (38, 43, 51)         # #262b33
WHITE = mkart.TEXT          # #f1f3f6
GREY = mkart.SUB            # #8b93a0
LOGO_GREY = "#272425"       # the logo's own dark grey
SMALL_GREY = "#aab0ba"      # in its place at icon sizes, where dark grey on a dark panel disappears
K = 4                       # drawn 4x and scaled down, for smooth edges


def hexc(c):
    return "#%02x%02x%02x" % c[:3]


def path(*parts):
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def write(p, text):
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def svg_text(source, grey=None):
    text = open(source, encoding="utf-8").read()
    return text.replace(LOGO_GREY, grey) if grey else text


def render(source, size, grey=None):
    """The logo as a picture, size pixels square, see-through around it."""
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size)], input=svg_text(source, grey).encode(),
                         capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def paste_logo(img, cx, cy, size, source=LOGO, grey=None):
    logo = render(source, size, grey)
    img.paste(logo, (int(cx - size / 2), int(cy - size / 2)), logo)


# ---------------------------------------------------------------- icons

def icons():
    theme = ("rootfs", "usr", "share", "icons", "XenithOS")
    # Big sizes: the whole logo. Small sizes: the X alone, its grey lifted so it shows on the panel.
    write(path(*theme, "scalable", "apps", "xenithos.svg"), svg_text(LOGO))
    write(path(*theme, "scalable", "apps", "xenithos-mark.svg"), svg_text(LOGO_X, SMALL_GREY))
    for size in (16, 22, 24, 32):
        small = render(LOGO_X, size * K, SMALL_GREY).resize((size, size), Image.LANCZOS)
        small.save(path(*theme, f"{size}x{size}", "apps", "xenithos.png"))
        small.save(path(*theme, f"{size}x{size}", "apps", "xenithos-mark.png"))
    render(LOGO, 512).save(path("rootfs", "usr", "share", "pixmaps", "xenithos.png"))


# ---------------------------------------------------------------- icons for XenithOS Console's apps

# XenithOS Console's own apps have entries in the desktop's Apps menu too (console-apps makes
# them), and these are their icons there: xenithos-app-<the app>. Each is the thing on the app's
# home screen picture (tools/mkart.py), without the words, on a tile of that picture's colour. An
# app that has none here gets the XenithOS logo.
APP_TILE = '<rect x="3" y="3" width="58" height="58" rx="14" fill="%s" stroke="%s" stroke-width="1.5"/>'


def app_icon_svgs():
    dark, edge = hexc(mkart.BG_DARK), hexc(EDGE)
    white, grey, red = hexc(WHITE), hexc(GREY), hexc(mkart.RED)
    found = {}

    # VOLT: the light trail, its head and two sparks.
    trail = [(0, 1), (1, 1), (2, 1), (2, 2), (2, 3), (3, 3), (4, 3), (4, 4), (5, 4)]
    cells = ['<rect x="%.1f" y="%d" width="7" height="7" rx="1.8" fill="%s"/>'
             % (8.5 + 8 * c, 8 + 8 * r, white if i == len(trail) - 1 else red) for i, (c, r) in enumerate(trail)]
    cells += ['<rect x="%.1f" y="%d" width="7" height="7" rx="1.8" fill="#ffd65a"/>' % (8.5 + 8 * c, 8 + 8 * r)
              for c, r in ((5, 1), (1, 4))]
    found["volt"] = APP_TILE % (dark, edge) + "".join(cells)

    # DOOM: its first letter, in its picture's bone white on dark red.
    found["doom"] = (APP_TILE % ("#1a0d0b", "#3a1e19")
                     + '<path fill="#e9dccf" fill-rule="evenodd" d="M21 15h11.5a17 17 0 0 1 0 34H21z'
                       'M30 24v16h2.5a8 8 0 0 0 0-16z"/>')

    # Tube: the play triangle.
    found["tube"] = (APP_TILE % (dark, edge)
                     + '<path d="M25 19.5v25l22-12.5z" fill="%s" stroke="%s" stroke-width="5" stroke-linejoin="round"/>'
                     % (red, red))

    # Photos: a flat picture, a sun and two hills.
    found["photos"] = (APP_TILE % (dark, edge)
                       + '<clipPath id="frame"><rect x="12" y="17" width="40" height="30" rx="6"/></clipPath>'
                       + '<rect x="12" y="17" width="40" height="30" rx="6" fill="%s"/>' % edge
                       + '<g clip-path="url(#frame)"><circle cx="41.5" cy="26" r="4.5" fill="%s"/>' % red
                       + '<path d="M12 47l13-18 13 18z" fill="%s"/><path d="M30 47l12-11 12 11z" fill="%s"/></g>' % (white, grey))

    # HostMC: a block (the server), with a red light on it.
    found["hostmc"] = (APP_TILE % (dark, edge)
                       + '<g stroke="%s" stroke-width="1.2" stroke-linejoin="round">' % dark
                       + '<path d="M32 14l17 9-17 9-17-9z" fill="%s"/>' % white
                       + '<path d="M15 23l17 9v19l-17-9z" fill="%s"/>' % grey
                       + '<path d="M49 23l-17 9v19l17-9z" fill="#565d69"/></g>'
                       + '<circle cx="42" cy="38.5" r="2.6" fill="%s"/>' % red)

    head = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">'
    return {app: head + body + "</svg>\n" for app, body in found.items()}


def app_icons():
    theme = ("rootfs", "usr", "share", "icons", "XenithOS")
    drawn = app_icon_svgs()

    def at(text, size):
        png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size)], input=text.encode(),
                             capture_output=True, check=True).stdout
        return Image.open(io.BytesIO(png)).convert("RGBA")

    # (The preview has them all side by side, each at three sizes, to look at.)
    sheet = Image.new("RGB", (len(drawn) * 190 + 16, 172), NEAR_BLACK)
    for n, (app, text) in enumerate(sorted(drawn.items())):
        write(path(*theme, "scalable", "apps", f"xenithos-app-{app}.svg"), text)
        for size in (16, 22, 24, 32):
            at(text, size).save(path(*theme, f"{size}x{size}", "apps", f"xenithos-app-{app}.png"))
        for size, x, y in ((128, 16, 22), (32, 154, 22), (16, 154, 64)):
            shot = at(text, size)
            sheet.paste(shot, (n * 190 + x, y), shot)
    sheet.save(path("preview", "app-icons.png"))


# ---------------------------------------------------------------- wallpapers

W4K, H4K = 3840, 2160


def wallpapers():
    out = ("rootfs", "usr", "share", "backgrounds", "xenithos")
    logo_size = int(H4K * 0.36)

    # The one XenithOS Desktop starts with (and the start-up screen and the gap between the
    # desktop and XenithOS Console show): the logo in the middle of near-black.
    img = Image.new("RGB", (W4K, H4K), NEAR_BLACK)
    paste_logo(img, W4K / 2, H4K / 2, logo_size)
    img.save(path(*out, "wallpaper.png"), optimize=True)

    # Rings: faint circles spreading from the logo.
    img = Image.new("RGB", (W4K, H4K), NEAR_BLACK)
    d = ImageDraw.Draw(img)
    for i, r in enumerate(range(560, 2400, 230)):
        shade = max(10, 24 - i * 2)
        d.ellipse([W4K / 2 - r, H4K / 2 - r, W4K / 2 + r, H4K / 2 + r], outline=(shade, shade + 2, shade + 6), width=4)
    paste_logo(img, W4K / 2, H4K / 2, logo_size)
    img.save(path(*out, "rings.png"), optimize=True)

    # Grid: a faint grid, like XenithOS Console's VOLT, with the logo over it.
    img = Image.new("RGB", (W4K, H4K), NEAR_BLACK)
    d = ImageDraw.Draw(img)
    for x in range(0, W4K, 120):
        d.line([(x, 0), (x, H4K)], fill=(13, 15, 19), width=2)
    for y in range(0, H4K, 120):
        d.line([(0, y), (W4K, y)], fill=(13, 15, 19), width=2)
    d.ellipse([W4K / 2 - 520, H4K / 2 - 520, W4K / 2 + 520, H4K / 2 + 520], fill=NEAR_BLACK)
    paste_logo(img, W4K / 2, H4K / 2, logo_size)
    img.save(path(*out, "grid.png"), optimize=True)

    # Corner: the logo small, out of the way of your windows.
    img = Image.new("RGB", (W4K, H4K), NEAR_BLACK)
    paste_logo(img, W4K - 330, H4K - 420, 380)
    img.save(path(*out, "corner.png"), optimize=True)

    # Plain: just near-black.
    Image.new("RGB", (W4K, H4K), NEAR_BLACK).save(path(*out, "plain.png"), optimize=True)


# ---------------------------------------------------------------- start-up screen (Plymouth)

def plymouth():
    # The same picture as the wallpaper, so the start-up screen, the moment before the desktop
    # and the desktop itself are one image that never moves.
    shutil.copyfile(path("rootfs", "usr", "share", "backgrounds", "xenithos", "wallpaper.png"),
                    path("rootfs", "usr", "share", "plymouth", "themes", "xenithos", "background.png"))


# ---------------------------------------------------------------- the ISO's boot menu (GRUB)

def boot_menu():
    w, h = 1920, 1080
    img = Image.new("RGB", (w * 2, h * 2), NEAR_BLACK)
    paste_logo(img, w, 290 * 2, 360 * 2)
    dr = ImageDraw.Draw(img)
    bold, regular = mkart.font("Bold", 60 * 2), mkart.font("Regular", 60 * 2)
    a, b = "XenithOS", " Desktop"
    wa, wb = dr.textlength(a, font=bold), dr.textlength(b, font=regular)
    x = w - (wa + wb) / 2
    top, bottom = dr.textbbox((0, 0), a, font=bold)[1::2]
    y = 525 * 2 - (top + bottom) / 2
    dr.text((x, y), a, font=bold, fill=WHITE)
    dr.text((x + wa, y), b, font=regular, fill=GREY)
    img.resize((w, h), Image.LANCZOS).save(path("boot", "splash.png"))

    # The highlight behind the chosen line: a card with a white outline, cut in nine for GRUB.
    r, line = 10, 2
    n = 2 * r + 1
    box = Image.new("RGBA", (n * K, n * K), (0, 0, 0, 0))
    bd = ImageDraw.Draw(box)
    bd.rounded_rectangle([0, 0, n * K - 1, n * K - 1], radius=r * K, fill=WHITE + (255,))
    bd.rounded_rectangle([line * K, line * K, n * K - 1 - line * K, n * K - 1 - line * K],
                         radius=(r - line) * K, fill=CARD + (255,))
    box = box.resize((n, n), Image.LANCZOS)
    pieces = {"nw": (0, 0, r, r), "n": (r, 0, r + 1, r), "ne": (r + 1, 0, n, r),
              "w": (0, r, r, r + 1), "c": (r, r, r + 1, r + 1), "e": (r + 1, r, n, r + 1),
              "sw": (0, r + 1, r, n), "s": (r, r + 1, r + 1, n), "se": (r + 1, r + 1, n, n)}
    for name, crop in pieces.items():
        box.crop(crop).save(path("boot", f"select_{name}.png"))


# ---------------------------------------------------------------- window borders (xfwm4)

TITLE_H, BORDER, BUTTON_W = 32, 4, 46
STATES = {
    # state: (frame fill, outer edge, glyph colour)
    "active": (CARD, EDGE, WHITE),
    "inactive": ((13, 15, 18), (29, 33, 40), (107, 115, 128)),
}
XPM_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+@"


def write_xpm(img, p):
    """An RGB image as XPM (two characters a pixel), the format xfwm4 themes use."""
    img = img.convert("RGB")
    colours = {}
    for c in img.getdata():
        if c not in colours:
            i = len(colours)
            colours[c] = XPM_CHARS[i // 64] + XPM_CHARS[i % 64]
    name = os.path.basename(p).replace("-", "_").replace(".", "_")
    out = ["/* XPM */", f"static char * {name}[] = {{", f'"{img.width} {img.height} {len(colours)} 2",']
    out += [f'"{code} c {hexc(c)}",' for c, code in colours.items()]
    px = list(img.getdata())
    for y in range(img.height):
        out.append('"' + "".join(colours[px[y * img.width + x]] for x in range(img.width)) + '",')
    out[-1] = out[-1].rstrip(",")
    out.append("};")
    write(p, "\n".join(out) + "\n")


def frame_piece(w, h, fill, edge, top=False, left=False, right=False, bottom=False):
    img = Image.new("RGB", (w, h), fill)
    d = ImageDraw.Draw(img)
    if top:
        d.line([(0, 0), (w - 1, 0)], fill=edge)
    if bottom:
        d.line([(0, h - 1), (w - 1, h - 1)], fill=edge)
    if left:
        d.line([(0, 0), (0, h - 1)], fill=edge)
    if right:
        d.line([(w - 1, 0), (w - 1, h - 1)], fill=edge)
    return img


def glyph(kind, d, cx, cy, colour):
    """A button's symbol, drawn at K times its size around (cx, cy)."""
    s, wd = 5 * K, int(1.4 * K)              # half the symbol's size, line width
    if kind == "close":
        d.line([(cx - s, cy - s), (cx + s, cy + s)], fill=colour, width=wd)
        d.line([(cx - s, cy + s), (cx + s, cy - s)], fill=colour, width=wd)
    elif kind == "hide":
        d.line([(cx - s, cy), (cx + s, cy)], fill=colour, width=wd)
    elif kind == "maximize":
        d.rectangle([cx - s, cy - s, cx + s, cy + s], outline=colour, width=wd)
    elif kind == "maximize-toggled":
        o = int(2 * K)
        d.line([(cx - s + o, cy - s), (cx + s, cy - s), (cx + s, cy + s - o)], fill=colour, width=wd)
        d.rectangle([cx - s, cy - s + o, cx + s - o, cy + s], outline=colour, width=wd)
    elif kind == "menu":
        for dy in (-s * 0.7, 0, s * 0.7):
            d.line([(cx - s, cy + dy), (cx + s, cy + dy)], fill=colour, width=wd)
    elif kind in ("shade", "shade-toggled"):
        sign = -1 if kind == "shade" else 1
        d.line([(cx - s, cy - sign * s / 2), (cx, cy + sign * s / 2), (cx + s, cy - sign * s / 2)], fill=colour, width=wd)
    elif kind in ("stick", "stick-toggled"):
        r = s * 0.45
        if kind == "stick":
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=colour, width=wd)
        else:
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)


def xfwm4():
    out = ("rootfs", "usr", "share", "themes", "XenithOS", "xfwm4")
    for state, (fill, edge, ink) in STATES.items():
        pieces = {
            "top-left": frame_piece(BORDER, TITLE_H, fill, edge, top=True, left=True),
            "top-right": frame_piece(BORDER, TITLE_H, fill, edge, top=True, right=True),
            "title-1": frame_piece(1, TITLE_H, fill, edge, top=True),
            "title-2": frame_piece(1, TITLE_H, fill, edge, top=True),
            "title-3": frame_piece(1, TITLE_H, fill, edge, top=True),
            "title-4": frame_piece(1, TITLE_H, fill, edge, top=True),
            "title-5": frame_piece(1, TITLE_H, fill, edge, top=True),
            "left": frame_piece(BORDER, 1, fill, edge, left=True),
            "right": frame_piece(BORDER, 1, fill, edge, right=True),
            "bottom": frame_piece(1, BORDER, fill, edge, bottom=True),
            "bottom-left": frame_piece(BORDER, BORDER, fill, edge, left=True, bottom=True),
            "bottom-right": frame_piece(BORDER, BORDER, fill, edge, right=True, bottom=True),
        }
        for name, img in pieces.items():
            write_xpm(img, path(*out, f"{name}-{state}.xpm"))
        for kind in ("close", "hide", "maximize", "maximize-toggled", "menu", "shade", "shade-toggled",
                     "stick", "stick-toggled"):
            for look in ("", "prelight", "pressed"):
                if look == "prelight":
                    bg, colour = ((232, 17, 35), (255, 255, 255)) if kind == "close" else ((35, 40, 50), WHITE)
                elif look == "pressed":
                    bg, colour = ((176, 8, 18), (255, 255, 255)) if kind == "close" else ((44, 50, 60), WHITE)
                else:
                    bg, colour = fill, ink
                img = Image.new("RGB", (BUTTON_W * K, TITLE_H * K), bg)
                glyph(kind, ImageDraw.Draw(img), BUTTON_W * K / 2, TITLE_H * K / 2 + K, colour)
                img = img.resize((BUTTON_W, TITLE_H), Image.LANCZOS)
                ImageDraw.Draw(img).line([(0, 0), (BUTTON_W - 1, 0)], fill=edge)
                if look:
                    if state == "active":
                        write_xpm(img, path(*out, f"{kind}-{look}.xpm"))
                else:
                    write_xpm(img, path(*out, f"{kind}-{state}.xpm"))
    write(path(*out, "themerc"), "\n".join([
        "# XenithOS's window borders: flat #12151a title bars with a #262b33 edge, white titles,",
        "# and a red close button under the pointer. Drawn by desktop/art/make-art.py.",
        "button_offset=0",
        "button_spacing=0",
        "full_width_title=true",
        "title_alignment=left",
        "title_horizontal_offset=12",
        "title_shadow_active=false",
        "title_shadow_inactive=false",
        "title_vertical_offset_active=1",
        "title_vertical_offset_inactive=1",
        "active_text_color=" + hexc(WHITE),
        "inactive_text_color=" + hexc(STATES["inactive"][2]),
        "shadow_delta_height=0",
        "shadow_delta_width=0",
        "shadow_delta_x=0",
        "shadow_delta_y=-4",
        "shadow_opacity=45",
        "maximized_offset=0",
        "",
    ]))


# ---------------------------------------------------------------- tiles for XenithOS Console

def tile(big, small, where):
    """A flat tile for XenithOS Console's home screen: the logo, a name, and what it is."""
    w, h = mkart.W, mkart.H
    img = Image.new("RGB", (w * K, h * K), mkart.BG_DARK)
    paste_logo(img, 64 * K, 74 * K, 104 * K, grey="#3a3f47")
    d = ImageDraw.Draw(img)
    name = mkart.font("Bold", 36 * K)
    while d.textlength(big, font=name) > (w - 132) * K and name.size > 20 * K:
        name = mkart.font("Bold", name.size - 2 * K)
    d.text((124 * K, (74 - 22) * K), big, font=name, fill=WHITE)
    d.text((26 * K, 134 * K), small, font=mkart.font("Medium", 19 * K), fill=GREY)
    img = img.resize((w, h), Image.LANCZOS)
    mkart.write_ximg(img, path(*where))
    img.save(path("preview", os.path.basename(where[-1]).replace(".ximg", ".png")))


def tiles():
    tile("Install", "XenithOS Desktop on this PC", ("live", "usr", "share", "xenithos-desktop", "xenithos-install.ximg"))
    tile("PC Settings", "Wi-Fi, sound and updates", ("rootfs", "usr", "share", "xenithos-desktop", "pc-settings.ximg"))


def main():
    icons()
    app_icons()
    wallpapers()
    plymouth()
    boot_menu()
    xfwm4()
    tiles()
    print(f"Art drawn in {OUT}")


if __name__ == "__main__":
    main()
