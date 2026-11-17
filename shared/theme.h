#ifndef XENITH_THEME_H
#define XENITH_THEME_H

/* XenithOS colors, shared by the shell and every app. */
#include "gfx.h"

/* Theme preview (branch theme-stream): the look of the Systembridge streaming site.
 * Near-black, dark cards with faint edges, white text, one red accent used sparingly. */
#define C_BG        RGB(7, 8, 10)        /* screen background, #07080a */
#define C_SURFACE   RGB(18, 21, 26)      /* cards and panels, #12151a */
#define C_SURFACE2  RGB(27, 31, 38)      /* raised panels, empty bars */
#define C_LINE      RGB(38, 43, 51)      /* card edges, #262b33 */
#define C_TEXT      RGB(241, 243, 246)
#define C_SUB       RGB(139, 147, 160)   /* secondary text, #8b93a0 */
#define C_DIM       RGB(95, 99, 110)
#define C_ACCENT    RGB(229, 9, 20)      /* the red: progress, active chips, the brand dot */
#define C_ACCENT_HI RGB(255, 90, 104)
#define C_OK        RGB(70, 211, 105)
#define C_WARN      RGB(245, 179, 1)
#define C_BAD       RGB(225, 29, 46)
#define C_SEL       RGB(29, 33, 41)      /* a selected card: a touch lighter, with a white edge */
#define C_INK       RGB(8, 9, 11)        /* text on white buttons */

/* Controller button colors for on-screen hints */
#define C_BTN_A     RGB(107, 192, 75)
#define C_BTN_B     RGB(229, 72, 63)
#define C_BTN_X     RGB(58, 134, 240)
#define C_BTN_Y     RGB(243, 189, 44)

/* Where XenithOS itself lives, so apps can return to it. */
#define XENITH_SHELL_PATH "uda:/xenon.elf"
#define XENITH_VERSION "0.7.2"

#endif
