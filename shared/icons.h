#ifndef XENITH_ICONS_H
#define XENITH_ICONS_H

/* Line-art icons drawn from shapes at any size, so they stay sharp. */
#include <stdint.h>

enum {
    ICON_APPS, ICON_STORE, ICON_CLOUD, ICON_PROFILE, ICON_SETTINGS, ICON_POWER,
    ICON_SYSTEM, ICON_CONTROLLER, ICON_DOWNLOAD, ICON_CHECK, ICON_SOUND, ICON_NETWORK,
    ICON_INFO, ICON_PLAY, ICON_PERSON_ADD, ICON_SIGN_OUT, ICON_MAIL, ICON_TROPHY,
    ICON_SERVER, ICON_KEY, ICON_PALETTE, ICON_REFRESH, ICON_HOME, ICON_CLOSE, ICON_SWITCH,
    ICON_POINTER, ICON_COUNT
};

/* fg draws the icon; bg is used for cut-outs (like holes in the controller). */
void icon_draw(int icon, float cx, float cy, float size, uint32_t fg, uint32_t bg);

#endif
