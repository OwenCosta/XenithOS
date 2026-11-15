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
int  ui_nav_x(void);       /* -1 / +1 with key repeat; call once per frame */
int  ui_nav_y(void);
void ui_end_frame(void);   /* toasts, fade, present, sound, 60 fps */
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

/* Full-screen loading and progress */
void ui_loading_frame(const char *title, const char *detail);
void ui_busy_begin(const char *title, const char *detail);   /* animates during network waits */
void ui_busy_end(void);
void ui_progress_frame(const char *title, const char *detail, int done, int total);

/* Modal dialogs over the current screen */
int  ui_dialog(const char *title, const char *body, const char *const *options, int n);   /* -1 on B */
void ui_message(const char *title, const char *body);
int  ui_confirm(const char *title, const char *body, const char *yes, const char *no);

#endif
