/* Colour themes. See theme.h. */
#include <string.h>

#include "theme.h"

struct theme_def {
    const char *id, *name;
    uint32_t bg, surface, surface2, line, sel, accent, accent_hi;
};

/* Each is a near-black with a little of its colour in it, cards a step up, and the accent.
 * The accents are dark enough for white on them (the toasts' icons, active chips). */
static const struct theme_def themes[THEME_COUNT] = {
    {"xenithos", "XenithOS", RGB(7, 8, 10), RGB(18, 21, 26), RGB(27, 31, 38), RGB(38, 43, 51), RGB(29, 33, 41),
     RGB(229, 9, 20), RGB(255, 90, 104)},
    {"ocean", "Ocean", RGB(6, 9, 14), RGB(15, 21, 31), RGB(23, 31, 44), RGB(34, 45, 62), RGB(25, 34, 49),
     RGB(33, 110, 228), RGB(112, 168, 255)},
    {"forest", "Forest", RGB(6, 10, 8), RGB(15, 23, 19), RGB(23, 34, 28), RGB(34, 48, 41), RGB(25, 37, 31),
     RGB(26, 138, 72), RGB(104, 214, 148)},
    {"ember", "Ember", RGB(11, 8, 6), RGB(25, 19, 15), RGB(36, 28, 22), RGB(51, 40, 31), RGB(39, 30, 24),
     RGB(212, 86, 10), RGB(255, 164, 92)},
    {"violet", "Violet", RGB(9, 7, 13), RGB(21, 17, 29), RGB(31, 26, 42), RGB(45, 38, 58), RGB(33, 28, 46),
     RGB(126, 70, 222), RGB(186, 148, 255)},
    {"teal", "Teal", RGB(6, 10, 11), RGB(14, 23, 25), RGB(21, 33, 36), RGB(31, 48, 52), RGB(23, 37, 40),
     RGB(0, 128, 132), RGB(88, 212, 208)},
};

/* Until theme_apply: the first theme, so a program that never picks one looks as it always has. */
struct xenith_palette pal = {
    RGB(7, 8, 10), RGB(18, 21, 26), RGB(27, 31, 38), RGB(38, 43, 51), RGB(241, 243, 246), RGB(139, 147, 160),
    RGB(95, 99, 110), RGB(229, 9, 20), RGB(255, 90, 104), RGB(70, 211, 105), RGB(245, 179, 1), RGB(225, 29, 46),
    RGB(29, 33, 41), RGB(8, 9, 11),
};

static int current;

const char *theme_id(int theme)
{
    return themes[theme >= 0 && theme < THEME_COUNT ? theme : 0].id;
}

const char *theme_name(int theme)
{
    return themes[theme >= 0 && theme < THEME_COUNT ? theme : 0].name;
}

int theme_find(const char *id)
{
    int i;
    for (i = 0; id && i < THEME_COUNT; i++)
        if (!strcmp(id, themes[i].id)) return i;
    return THEME_XENITHOS;
}

void theme_colors(int theme, struct xenith_palette *p)
{
    const struct theme_def *t = &themes[theme >= 0 && theme < THEME_COUNT ? theme : 0];
    p->bg = t->bg;
    p->surface = t->surface;
    p->surface2 = t->surface2;
    p->line = t->line;
    p->sel = t->sel;
    p->accent = t->accent;
    p->accent_hi = t->accent_hi;
    p->text = RGB(241, 243, 246);
    p->sub = RGB(139, 147, 160);
    p->dim = RGB(95, 99, 110);
    p->ok = RGB(70, 211, 105);
    p->warn = RGB(245, 179, 1);
    p->bad = RGB(225, 29, 46);
    p->ink = RGB(8, 9, 11);
}

void theme_apply(int theme)
{
    current = theme >= 0 && theme < THEME_COUNT ? theme : 0;
    theme_colors(current, &pal);
}

int theme_current(void)
{
    return current;
}
