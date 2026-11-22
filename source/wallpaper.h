#ifndef XENITH_WALLPAPER_H
#define XENITH_WALLPAPER_H

/* Wallpapers for the home screen (Settings > Theme): flat, dark pictures made of a few shapes,
 * drawn in the colour theme's own colours, so each one suits every theme. settings.txt keeps
 * one by its id ("peaks"). */
enum { WALL_PLAIN, WALL_RINGS, WALL_PEAKS, WALL_BANDS, WALL_HALFTONE, WALL_WAVES, WALL_COUNT };

const char *wallpaper_id(int w);              /* "plain", "rings"... */
const char *wallpaper_name(int w);            /* "Plain", "Rings"... */
int  wallpaper_find(const char *id);          /* WALL_PLAIN for one it doesn't know */
/* Draws wallpaper w filling that box, in the colours in use (a card's preview). */
void wallpaper_draw(int w, int x, int y, int bw, int bh);
/* The whole screen's background: drawn once into a picture of its own, then copied in each
 * frame (it's drawn again when the colours change). WALL_PLAIN is ui_bg(). */
void wallpaper_bg(int w);

#endif
