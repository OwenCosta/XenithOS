#!/usr/bin/env python3
"""Writes XenithOS Desktop's two logo files: logo.svg (the whole logo) and logo-x.svg (the X
alone, for sizes too small for the rings). make-art.py makes every icon, wallpaper and screen
from these two.

    python3 desktop/art/draw-logo.py [TOOLS_DIR]

The logo's shapes have one home, tools/mkart.py (LOGO_X, LOGO_ARCS, LOGO_RING: the moment of
XenithOS Console's start-up that the user picked as the logo), and this only puts them in a
square with a little air around them. Run it again when those numbers change, then make-art.py
picks the new files up at the next build.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "tools"))
import mkart  # noqa: E402  the logo's numbers and its SVG writer

S = 1000            # SVG units to R, the double ring's radius
AIR = 0.03          # room around the shapes, in R


def write(name, box, shapes):
    x, y, size = box
    with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x:.0f} {y:.0f} {size:.0f} {size:.0f}">{shapes}</svg>\n')


def main():
    # The whole logo: out to the outer ring's edge.
    half = (mkart.LOGO_HALF + AIR) * S
    write("logo.svg", (-half, -half, 2 * half), mkart.svg_logo(S))

    # The X alone, in the middle of a square. Its grey is the logo's own here; make-art.py
    # lifts it where the X stands on a dark panel.
    x0, y0, x1, y1 = mkart.logo_x_box()
    half = (max(x1 - x0, y1 - y0) / 2 + AIR) * S
    cx, cy = (x0 + x1) / 2 * S, (y0 + y1) / 2 * S
    write("logo-x.svg", (cx - half, cy - half, 2 * half), mkart.svg_logo(S, rings=False))
    print("logo.svg and logo-x.svg written from tools/mkart.py's numbers")


if __name__ == "__main__":
    main()
