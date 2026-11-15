/* The XenithOS insignia's shapes. Written by tools/mkart.py from its LOGO_* numbers: change
 * those and run `python3 tools/mkart.py logo`, not this file.
 *
 * Units are R, the outer ring's radius, with the centre at (0, 0) and y pointing down. Arc
 * angles are radians clockwise from 3 o'clock, the way the screen goes. */
#ifndef LOGO_SHAPE_H
#define LOGO_SHAPE_H

struct logo_poly { unsigned char red, n; float xy[12]; };
struct logo_arc { unsigned char red; float r, w, a0, a1; };

/* The X: the grey chevron, the upper red arm, the lower red arm, then the slash (its point first). */
static const struct logo_poly logo_x[4] = {
    {0, 6, {-0.8680f, -0.5640f, -0.4040f, -0.5640f, 0.1600f, 0.0000f, -0.4040f, 0.5640f, -0.8680f, 0.5640f, -0.3040f, 0.0000f}},
    {1, 6, {0.3660f, -0.5640f, 0.8850f, -0.5640f, 0.6390f, -0.3180f, 0.4530f, -0.3180f, 0.2010f, -0.0660f, 0.0350f, -0.2330f}},
    {1, 4, {0.3180f, 0.0640f, 0.8180f, 0.5640f, 0.3760f, 0.5640f, 0.0970f, 0.2850f}},
    {1, 6, {-0.4100f, 0.5950f, 0.4900f, -0.2560f, 0.5700f, -0.2610f, 0.2590f, 0.0380f, -0.0640f, 0.3330f, -0.2920f, 0.5170f}},
};

/* The ticks at 3, 12, 9 and 6 o'clock, inner point first. */
static const struct logo_poly logo_ticks[4] = {
    {1, 6, {0.7400f, 0.0000f, 0.8100f, 0.0090f, 1.0900f, 0.0090f, 1.1250f, 0.0000f, 1.0900f, -0.0090f, 0.8100f, -0.0090f}},
    {1, 6, {0.0000f, -0.7400f, 0.0090f, -0.8100f, 0.0090f, -1.0900f, 0.0000f, -1.1250f, -0.0090f, -1.0900f, -0.0090f, -0.8100f}},
    {1, 6, {-0.7400f, 0.0000f, -0.8100f, -0.0090f, -1.0900f, -0.0090f, -1.1250f, 0.0000f, -1.0900f, 0.0090f, -0.8100f, 0.0090f}},
    {1, 6, {0.0000f, 0.7400f, -0.0090f, 0.8100f, -0.0090f, 1.0900f, 0.0000f, 1.1250f, 0.0090f, 1.0900f, 0.0090f, 0.8100f}},
};

/* The rings, in pieces. */
static const struct logo_arc logo_arcs[12] = {
    {1, 1.000f, 0.058f, -1.50098f, -0.68068f},
    {1, 1.000f, 0.058f, -2.46091f, -1.64061f},
    {1, 1.000f, 0.058f, -4.64258f, -3.82227f},
    {1, 1.000f, 0.058f, -5.60251f, -4.78220f},
    {0, 0.904f, 0.058f, -1.48353f, -0.74176f},
    {0, 0.904f, 0.058f, -2.39983f, -1.65806f},
    {0, 0.904f, 0.058f, -4.62512f, -3.88336f},
    {0, 0.904f, 0.058f, -5.54142f, -4.79966f},
    {1, 0.960f, 0.044f, -0.49742f, -0.06109f},
    {0, 0.960f, 0.044f, -6.22210f, -5.78577f},
    {0, 0.960f, 0.044f, -3.08051f, -2.64417f},
    {0, 0.960f, 0.044f, -3.63901f, -3.20268f},
};

#define LOGO_TICK_OUT 1.125f              /* how far the ticks reach: the insignia's half-size */
#define LOGO_X_LEFT   (-0.868f)                 /* the box around the X alone */
#define LOGO_X_TOP    (-0.564f)
#define LOGO_X_RIGHT  0.885f
#define LOGO_X_BOTTOM 0.595f
#define LOGO_GREY       RGB(39, 36, 37)       /* the logo's own dark grey */
#define LOGO_GREY_SMALL RGB(170, 176, 186)    /* the X alone, at small sizes */

#endif
