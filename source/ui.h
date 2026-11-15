#ifndef XENITH_UI_H
#define XENITH_UI_H

/* Shared shell UI: layout, controller input, chrome, list rows, dialogs. */
#include <stdint.h>
#include <input/input.h>

extern int S;        /* base text scale: 3 at 720p, 2 at SD */
extern int M;        /* side margin */
extern int TOP;      /* y where screen content starts */

extern struct controller_data_s cur, prev;
#define PRESSED(f) (cur.f && !prev.f)
#define STICK_TH 20000

void ui_init(void);
void poll_input(void);                 /* one frame: ~16 ms */
int  nav_y(void);                      /* -1 up, +1 down, with repeat */
void wait_release(void);

void draw_chrome(const char *title, const char *hint);
void draw_row(int y, int h, int selected, int scale, const char *label, const char *sub);
int  confirm(const char *question);    /* A yes, B no */

/* Centered panel with a big line and an optional small line. Flushes. */
void ui_panel(const char *big, const char *small, uint32_t big_color);
/* Progress bar inside the same panel area. total <= 0 shows an indeterminate state. */
void ui_progress(const char *label, int done, int total);
/* Show a panel and wait for A or B. */
void ui_alert(const char *big, const char *small, uint32_t big_color);

#endif
