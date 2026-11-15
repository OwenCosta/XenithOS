#ifndef XENITH_THEME_H
#define XENITH_THEME_H

/* The XENITH palette, shared by the shell and every app so they look like one system. */
#include "gfx.h"

static const uint32_t C_BG     = RGB(11, 11, 15);
static const uint32_t C_PANEL  = RGB(24, 24, 31);
static const uint32_t C_TEXT   = RGB(242, 242, 245);
static const uint32_t C_DIM    = RGB(110, 110, 124);
static const uint32_t C_ACCENT = RGB(124, 92, 255);
static const uint32_t C_OK     = RGB(70, 200, 120);
static const uint32_t C_WARN   = RGB(255, 170, 60);
static const uint32_t C_BAD    = RGB(255, 80, 80);

/* Where XENITH itself lives, so apps can return to it. */
#define XENITH_SHELL_PATH "uda:/xenon.elf"

#endif
