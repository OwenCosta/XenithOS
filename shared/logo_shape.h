/* The XenithOS logo's shapes. Written by tools/mkart.py from its LOGO_* numbers: change those
 * and run `python3 tools/mkart.py logo`, not this file.
 *
 * Units are R, the double ring's outer radius, with the centre at (0, 0) and y pointing down.
 * Arc angles are radians clockwise from 3 o'clock, the way the screen goes. */
#ifndef LOGO_SHAPE_H
#define LOGO_SHAPE_H

struct logo_poly { unsigned char red, n; float ox, oy; float xy[12]; };   /* ox, oy: where it sits */
struct logo_arc { unsigned char red; float r, w, a0, a1; };

/* The X: the grey chevron, the upper red arm and the lower red arm. */
static const struct logo_poly logo_x[3] = {
    {0, 6, -0.0628f, 0.0000f, {-0.8680f, -0.5640f, -0.4040f, -0.5640f, 0.1600f, 0.0000f, -0.4040f, 0.5640f, -0.8680f, 0.5640f, -0.3040f, 0.0000f}},
    {1, 6, 0.0444f, -0.0444f, {0.3660f, -0.5640f, 0.8850f, -0.5640f, 0.6390f, -0.3180f, 0.4530f, -0.3180f, 0.2010f, -0.0660f, 0.0350f, -0.2330f}},
    {1, 4, 0.0444f, 0.0444f, {0.3180f, 0.0640f, 0.8180f, 0.5640f, 0.3760f, 0.5640f, 0.0970f, 0.2850f}},
};

/* The double ring, in pieces with square ends. */
static const struct logo_arc logo_arcs[12] = {
    {1, 1.000f, 0.058f, 4.78220f, 5.52885f},
    {1, 1.000f, 0.058f, 3.89592f, 4.64258f},
    {1, 1.000f, 0.058f, 1.64061f, 2.38726f},
    {1, 1.000f, 0.058f, 0.75433f, 1.50098f},
    {0, 0.904f, 0.058f, 4.79966f, 5.47492f},
    {0, 0.904f, 0.058f, 3.94985f, 4.62512f},
    {0, 0.904f, 0.058f, 1.65806f, 2.33333f},
    {0, 0.904f, 0.058f, 0.80826f, 1.48353f},
    {1, 0.960f, 0.044f, 5.82486f, 6.22210f},
    {0, 0.960f, 0.044f, 0.06109f, 0.45832f},
    {0, 0.960f, 0.044f, 3.20268f, 3.59992f},
    {0, 0.960f, 0.044f, 2.68327f, 3.08051f},
};

/* The outer ring: the start-up's four streaks, with round ends, in the order they're drawn. */
static const struct logo_arc logo_ring[4] = {
    {0, 1.300f, 0.064f, 0.39357f, 1.93749f},
    {0, 1.300f, 0.064f, 1.93749f, 3.48141f},
    {1, 1.300f, 0.064f, 3.48123f, 5.01835f},
    {0, 1.300f, 0.064f, 5.01782f, 6.54917f},
};

#define LOGO_HALF     1.332f      /* the logo's half-size, to the outer ring's edge */
#define LOGO_X_LEFT   (-0.9308f)  /* the box around the X alone */
#define LOGO_X_TOP    (-0.6084f)
#define LOGO_X_RIGHT  0.9294f
#define LOGO_X_BOTTOM 0.6084f
#define LOGO_GREY       RGB(39, 36, 37)      /* the logo's dark grey */
#define LOGO_GREY_SMALL RGB(170, 176, 186)   /* the X alone, at small sizes */

#endif
