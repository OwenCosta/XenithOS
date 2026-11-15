#ifndef XENITH_THEME_H
#define XENITH_THEME_H

/* XenithOS colors, shared by the shell and every app. */
#include "gfx.h"

#define C_BG        RGB(9, 13, 22)       /* screen background */
#define C_SURFACE   RGB(19, 26, 41)      /* cards and panels */
#define C_SURFACE2  RGB(27, 37, 58)      /* raised panels, empty bars */
#define C_LINE      RGB(36, 48, 72)
#define C_TEXT      RGB(236, 241, 250)
#define C_SUB       RGB(156, 168, 192)   /* secondary text */
#define C_DIM       RGB(102, 115, 141)
#define C_ACCENT    RGB(47, 125, 255)    /* selection, progress, brand "OS" */
#define C_ACCENT_HI RGB(110, 168, 255)
#define C_OK        RGB(52, 199, 116)
#define C_WARN      RGB(255, 176, 46)
#define C_BAD       RGB(240, 82, 82)

/* Controller button colors for on-screen hints */
#define C_BTN_A     RGB(107, 192, 75)
#define C_BTN_B     RGB(229, 72, 63)
#define C_BTN_X     RGB(58, 134, 240)
#define C_BTN_Y     RGB(243, 189, 44)

/* Where XenithOS itself lives, so apps can return to it. */
#define XENITH_SHELL_PATH "uda:/xenon.elf"
#define XENITH_VERSION "0.4.1"

#endif
