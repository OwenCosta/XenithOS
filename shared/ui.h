#ifndef XENITH_UI_H
#define XENITH_UI_H

/* The XenithOS UI kit: input, frame pacing, headers, button hints, loading screens,
 * toasts and dialogs. Every screen runs the same loop:
 *
 *     for (;;) { ui_poll(); ...handle input...; ...draw...; ui_end_frame(); }
 */
#include <stdint.h>
#include "platform.h"

extern struct pad cur, prev;
#define PRESSED(f) (cur.f && !prev.f)
#define STICK_TH 20000

extern int M;              /* side margin */
extern int HEADER_H;       /* space the screen header takes */

void ui_init(void);
void ui_poll(void);
void ui_settle_input(void); /* buttons held right now don't count until let go (after another program) */
int  ui_nav_x(void);       /* -1 / +1 with key repeat; call once per frame */
int  ui_nav_y(void);
void ui_end_frame(void);   /* toasts, fade, present, sound, 60 fps */
int  ui_frame_ms(void);    /* how long frames take to draw (not counting the wait), smoothed */
uint64_t ui_ms(void);
void ui_fade_in(void);
float ui_ease(float value, float target, float rate);   /* move a fraction of the way each frame */

void ui_bg(void);
void ui_header(const char *title, const char *subtitle);
void ui_profile_chip(int right_x, int cy);
void ui_hints(const char *spec);                /* "A=Select;B=Back;Y=Refresh" */
int  ui_button_glyph(int x, int cy, const char *btn);   /* returns width */
void ui_spinner(int cx, int cy, int r, uint32_t color);
void ui_toast(const char *title, const char *body);
void ui_toast_icon(int icon);                   /* icon for the next toast (ICON_*) */
void ui_panel(int x, int y, int w, int h);      /* a card */
void ui_select_box(float x, float y, float w, float h, float r);   /* a selected row or card */
int  ui_brand(int x, int y, int font, int measure_only);   /* the mark and "XenithOS" */
/* The XenithOS insignia: a grey chevron and two red arms make an X with a red slash through it,
 * inside a broken double ring with four ticks (its shapes are in logo_shape.h, from
 * tools/mkart.py). size is its whole height, tick to tick. build (0..1) brings the X's pieces in
 * from outside and sweeps the rings round from the ticks; slash (0..1) grows the slash out of its
 * point and brings the ticks in. */
void ui_logo(float cx, float cy, float size, float build, float slash, int alpha);
/* The X on its own, size high, for small sizes where the rings would blur: its grey is lifted
 * so it shows on dark panels. */
void ui_mark(float cx, float cy, float size, int alpha);

/* Full-screen loading and progress */
void ui_loading_frame(const char *title, const char *detail);
/* ui_loading_frame with a thin bar under it (done of total; no bar while total is 0) */
void ui_loading_progress(const char *title, const char *detail, int done, int total);
void ui_busy_begin(const char *title, const char *detail);   /* animates during network waits */
void ui_busy_end(void);
void ui_progress_frame(const char *title, const char *detail, int done, int total);

/* Start another program (plat_launch) behind a loading screen in ui_loading_frame's layout,
 * with a bar while it's read. Programs start with a frame laid out the same way, so on the
 * Xbox the switch doesn't show. Returns what plat_launch returns. */
int  ui_launch(const char *path, const char *title, const char *detail);
/* What that loading screen says from now on (the guide's "Closing" while a game closes).
 * The strings have to last. */
void ui_launch_text(const char *title, const char *detail);
/* Leave an app for XenithOS, which comes back on the home screen with the app selected. */
void ui_back_to_xenithos(const char *app_id);

/* Modal dialogs over the current screen */
int  ui_dialog(const char *title, const char *body, const char *const *options, int n);   /* -1 on B */
void ui_message(const char *title, const char *body);
int  ui_confirm(const char *title, const char *body, const char *yes, const char *no);

#endif
