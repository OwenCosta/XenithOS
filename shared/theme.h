#ifndef XENITH_THEME_H
#define XENITH_THEME_H

/* XenithOS colors, shared by the shell and every app. */
#include "gfx.h"

/* The colours in use, from the colour theme picked in Settings > Theme (theme_apply). Every
 * theme is flat and dark, in the look of the Systembridge streaming site: near-black, dark cards
 * with faint edges, white text, one accent used sparingly. The default is that site's red. */
struct xenith_palette {
    uint32_t bg, surface, surface2, line, text, sub, dim, accent, accent_hi, ok, warn, bad, sel, ink;
};
extern struct xenith_palette pal;

#define C_BG        (pal.bg)          /* screen background */
#define C_SURFACE   (pal.surface)     /* cards and panels */
#define C_SURFACE2  (pal.surface2)    /* raised panels, empty bars */
#define C_LINE      (pal.line)        /* card edges */
#define C_TEXT      (pal.text)
#define C_SUB       (pal.sub)         /* secondary text */
#define C_DIM       (pal.dim)
#define C_ACCENT    (pal.accent)      /* progress, active chips, the brand dot */
#define C_ACCENT_HI (pal.accent_hi)
#define C_OK        (pal.ok)
#define C_WARN      (pal.warn)
#define C_BAD       (pal.bad)
#define C_SEL       (pal.sel)         /* a selected card: a touch lighter, with a white edge */
#define C_INK       (pal.ink)         /* text on white buttons */
#define C_BRAND     RGB(229, 9, 20)   /* the logo's red, whatever the theme */

/* Controller button colors for on-screen hints */
#define C_BTN_A     RGB(107, 192, 75)
#define C_BTN_B     RGB(229, 72, 63)
#define C_BTN_X     RGB(58, 134, 240)
#define C_BTN_Y     RGB(243, 189, 44)

/* The colour themes. settings.txt keeps a theme by its id ("ocean"), so the list can grow. */
enum { THEME_XENITHOS, THEME_OCEAN, THEME_FOREST, THEME_EMBER, THEME_VIOLET, THEME_TEAL, THEME_COUNT };
const char *theme_id(int theme);           /* "xenithos", "ocean"... */
const char *theme_name(int theme);         /* "XenithOS", "Ocean"... */
int  theme_find(const char *id);           /* THEME_XENITHOS for one it doesn't know */
void theme_apply(int theme);               /* pal = that theme's colours */
int  theme_current(void);
/* A theme's own colours without switching to it, for its card in Settings. */
void theme_colors(int theme, struct xenith_palette *out);

/* High contrast (Settings > Accessibility), with any theme: black behind, white text, light
 * grey for the quieter words, card edges that stand out and a thicker edge on what's selected. */
void theme_set_contrast(int on);
int  theme_contrast(void);
#define UI_EDGE (theme_contrast() ? 3 : 2)    /* how thick a selected card's white edge is */

/* Where XenithOS itself lives, so apps can return to it. */
#define XENITH_SHELL_PATH "uda:/xenon.elf"
#define XENITH_VERSION "0.7.3"

#endif
