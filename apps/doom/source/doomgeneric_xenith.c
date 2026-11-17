/* DOOM for XenithOS: the doomgeneric platform layer.
 *
 * doomgeneric renders 320x200 frames as 0x00RRGGBB pixels. They're stretched to 4:3 in
 * the middle of the TV, controller changes become Doom key events, and quitting Doom
 * (or holding BACK) goes back to XenithOS.
 *
 * Controls
 *   Left stick      move / strafe          Right stick / D-pad  turn (D-pad up/down move too)
 *   RT              fire                   LT or L3             run
 *   A               use / menu select      B                    menu back
 *   X               map                    Y                    "yes" in prompts
 *   LB / RB         previous / next weapon START                menu
 *   hold BACK       quit to XenithOS
 *
 * On the desktop (opened from the Apps menu, ui_desktop), the usual PC controls instead:
 *   Mouse           turn; left button fires, right held strafes, middle walks
 *   W A S D         move and strafe        Arrows               move and turn
 *   E or Space      use                    Ctrl                 fire
 *   Shift           run                    1 to 7               weapons
 *   Tab             map                    Esc                  Doom's menu (and lets go of the mouse)
 * The game holds the mouse while you play and lets go of it in menus. F11 is full screen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"
#include "doomstat.h"                   /* gamestate, menuactive, paused: where the mouse goes */

#include "audio.h"
#include "../../../shared/config.h"     /* doomgeneric has a config.h of its own */
#include "gfx.h"
#include "handoff.h"
#include "platform.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

#undef STICK_TH
#define STICK_TH 16000
#define TRIGGER_TH 100

/* ------------------------------------------------------------- key queue */
#define QSIZE 64
static unsigned short queue[QSIZE];      /* (pressed << 8) | key */
static int q_head, q_tail;

static void push_key(int pressed, unsigned char key)
{
    int next = (q_head + 1) % QSIZE;
    if (next == q_tail) return;
    queue[q_head] = (unsigned short)((pressed ? 0x100 : 0) | key);
    q_head = next;
}

/* --------------------------------------------------------- controller map */
enum {
    B_UP, B_DOWN, B_TURN_L, B_TURN_R, B_STRAFE_L, B_STRAFE_R, B_FIRE, B_USE, B_ENTER,
    B_BACKSPACE, B_MENU, B_MAP, B_RUN, B_YES, B_COUNT
};

static const unsigned char bind_key[B_COUNT] = {
    KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW, KEY_STRAFE_L, KEY_STRAFE_R,
    KEY_FIRE, KEY_USE, KEY_ENTER, KEY_BACKSPACE, KEY_ESCAPE, KEY_TAB, KEY_RSHIFT, 'y',
};

static int held[B_COUNT];
static struct pad pad, last_pad;
static int weapon = 1, weapon_release_at = -1, polls;
static uint64_t back_since;

static void back_to_xenith(void)
{
    plat_rumble(0, 0);
    ui_back_to_xenithos("doom");
}

static void poll_pad(void)
{
    int want[B_COUNT], i;

    last_pad = pad;
    plat_poll(&pad);
    ui_pad_apply(&pad);                  /* buttons moved in Settings > Accessibility */
    polls++;

    want[B_UP]        = pad.up || pad.ly > STICK_TH;
    want[B_DOWN]      = pad.down || pad.ly < -STICK_TH;
    want[B_TURN_L]    = pad.left || pad.rx < -STICK_TH;
    want[B_TURN_R]    = pad.right || pad.rx > STICK_TH;
    want[B_STRAFE_L]  = pad.lx < -STICK_TH;
    want[B_STRAFE_R]  = pad.lx > STICK_TH;
    want[B_FIRE]      = pad.rt > TRIGGER_TH;
    want[B_USE]       = pad.a;
    want[B_ENTER]     = pad.a;
    want[B_BACKSPACE] = pad.b;
    want[B_MENU]      = pad.start;
    want[B_MAP]       = pad.x;
    want[B_RUN]       = pad.lt > TRIGGER_TH || pad.ls;
    want[B_YES]       = pad.y;

    for (i = 0; i < B_COUNT; i++)
        if (want[i] != held[i]) {
            held[i] = want[i];
            /* Held on the first poll means pressed to start DOOM, not for DOOM. */
            if (polls > 1 || !want[i]) push_key(want[i], bind_key[i]);
        }

    /* Weapon cycling: hold the number key for a few polls so a game tic sees it. */
    if (weapon_release_at >= 0 && polls >= weapon_release_at) {
        push_key(0, (unsigned char)('0' + weapon));
        weapon_release_at = -1;
    }
    if (weapon_release_at < 0 && ((pad.rb && !last_pad.rb) || (pad.lb && !last_pad.lb))) {
        weapon += pad.rb ? 1 : -1;
        if (weapon > 7) weapon = 1;
        if (weapon < 1) weapon = 7;
        push_key(1, (unsigned char)('0' + weapon));
        weapon_release_at = polls + 3;
    }

    /* Hold BACK for about a second to leave. */
    if (pad.back) {
        if (!back_since) back_since = plat_time_us();
        if (plat_time_us() - back_since > 1000000) back_to_xenith();
    } else {
        back_since = 0;
    }
}

/* ------------------------------------------------------ desktop: keys and mouse */

/* In the game the keys are its controls (W walks, E uses); in menus and while a savegame's name
 * is typed they're themselves. Each key's release goes out as what its press went out as, so a
 * key held while the menu opens doesn't keep walking afterwards. */
static unsigned char sent_as[512];
static int mouse_dx, mouse_buttons, mouse_sent_buttons;

/* The keys are the game's controls whenever no menu is up and a game is on: a level, the
 * screen between levels (where Space or Ctrl goes on), the story's text. */
static int controls_on(void)
{
    return !menuactive && !demoplayback && gamestate != GS_DEMOSCREEN;
}

/* The game holds the mouse only while a level is played. */
static int playing(void)
{
    return gamestate == GS_LEVEL && !menuactive && !paused && !demoplayback;
}

static int doom_key(int k, int in_game)
{
    if (k >= 'a' && k <= 'z') {
        if (in_game)
            switch (k) {
            case 'w': return KEY_UPARROW;
            case 's': return KEY_DOWNARROW;
            case 'a': return KEY_STRAFE_L;
            case 'd': return KEY_STRAFE_R;
            case 'e': return KEY_USE;
            }
        return k;
    }
    if ((k >= '0' && k <= '9') || k == '-' || k == '=') return k;      /* weapons; the view's size */
    if (k == ' ') return in_game ? KEY_USE : ' ';
    if (k >= PLAT_KEY_F1 && k < PLAT_KEY_F1 + 10) return KEY_F1 + (k - PLAT_KEY_F1);   /* save, load... */
    if (k == PLAT_KEY_F1 + 11) return KEY_F12;                            /* (F11 is full screen) */
    switch (k) {
    case PLAT_KEY_ESC: return KEY_ESCAPE;
    case PLAT_KEY_ENTER: return KEY_ENTER;
    case PLAT_KEY_TAB: return KEY_TAB;
    case PLAT_KEY_BACKSPACE: return KEY_BACKSPACE;
    case PLAT_KEY_DELETE: return KEY_DEL;
    case PLAT_KEY_UP: return KEY_UPARROW;
    case PLAT_KEY_DOWN: return KEY_DOWNARROW;
    case PLAT_KEY_LEFT: return KEY_LEFTARROW;
    case PLAT_KEY_RIGHT: return KEY_RIGHTARROW;
    case PLAT_KEY_SHIFT: return KEY_RSHIFT;
    case PLAT_KEY_CTRL: return in_game ? KEY_FIRE : KEY_RCTRL;
    case PLAT_KEY_ALT: return KEY_RALT;
    case PLAT_KEY_PAUSE: return KEY_PAUSE;
    }
    return 0;
}

static void poll_desktop(void)
{
    struct plat_mouse m;
    int key, down, in_game;

    plat_poll(&pad);                     /* the window's events; the keys come below */
    polls++;
    in_game = controls_on();
    while (plat_key_event(&key, &down)) {
        int k = key < (int)sizeof sent_as ? key : 0;
        if (down) {
            int d = doom_key(key, in_game);
            if (d && k) {
                sent_as[k] = (unsigned char)d;
                push_key(1, (unsigned char)d);
            }
        } else if (k && sent_as[k]) {
            push_key(0, sent_as[k]);
            sent_as[k] = 0;
        }
    }

    /* The mouse turns and fires while the game holds it; menus get the pointer back. */
    in_game = playing();
    plat_mouse_grab(in_game);
    plat_mouse(&m);
    if (in_game) {
        mouse_dx += m.dx;
        mouse_buttons = m.buttons;
    } else {
        mouse_dx = 0;
        mouse_buttons = 0;
    }
}

/* One mouse event a tic, when there's something to say: Doom takes each ev_mouse as the
 * whole movement since the last, and as which buttons are held. Called as Doom asks for the
 * next key and there's none left. */
static void post_mouse(void)
{
    event_t ev;
    if (!mouse_dx && mouse_buttons == mouse_sent_buttons) return;
    memset(&ev, 0, sizeof ev);
    ev.type = ev_mouse;
    ev.data1 = mouse_buttons;            /* left fires, right strafes, middle walks (Doom's own) */
    ev.data2 = mouse_dx * 3;             /* turning, at about Doom's own speed for a PC mouse */
    ev.data3 = 0;                        /* moving the mouse forward doesn't walk */
    D_PostEvent(&ev);
    mouse_dx = 0;
    mouse_sent_buttons = mouse_buttons;
}

/* ---------------------------------------------------------- loading */

/* Doom reads a lot of its WAD before the first frame. Until then a loading screen is shown,
 * redrawn from DG_OnRead (called before every read) so the spinner keeps turning. */
static int loading = 1, first_frame_done;
static uint64_t loading_drawn;

static void loading_screen(void)
{
    int cy = SCR_H / 2;
    ui_bg();
    ui_spinner(SCR_W / 2, cy - 44, 24, C_ACCENT);
    gfx_str_c(FONT_LABEL, SCR_W / 2, cy + 6, C_TEXT, "DOOM");
    gfx_str_c(FONT_BODY, SCR_W / 2, cy + 46, C_SUB, "Loading the game data");
    gfx_present();
    loading_drawn = plat_time_us();
}

void DG_OnRead(void)
{
    if (loading && plat_time_us() - loading_drawn > 66000) loading_screen();
}

/* ---------------------------------------------------------- doomgeneric */
static int dst_x, dst_y, dst_w, dst_h;

void DG_Init(void)
{
    dst_h = SCR_H;
    dst_w = dst_h * 4 / 3;
    if (dst_w > SCR_W) {
        dst_w = SCR_W;
        dst_h = dst_w * 3 / 4;
    }
    dst_x = (SCR_W - dst_w) / 2;
    dst_y = (SCR_H - dst_h) / 2;
    loading = 0;
    gfx_clear(0);          /* the bars beside the picture; the first frame shows it */
}

void DG_DrawFrame(void)
{
    gfx_blit_scaled(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY, dst_x, dst_y, dst_w, dst_h);
    gfx_present();
    if (!first_frame_done) {
        first_frame_done = 1;
        startlog_ready("DOOM");
    }
    if (ui_desktop) poll_desktop();
    else poll_pad();
}

void DG_SleepMs(uint32_t ms)
{
    plat_sleep_us((int)ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)(plat_time_us() / 1000);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (q_tail == q_head) {
        if (ui_desktop) post_mouse();
        return 0;
    }
    *pressed = (queue[q_tail] >> 8) & 1;
    *key = (unsigned char)(queue[q_tail] & 0xff);
    q_tail = (q_tail + 1) % QSIZE;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

/* ------------------------------------------------------------ game data */
/* Where WADs are looked for: the Store puts game data in uda:/data/. On PCs and Pis the
 * freedoom package (install.sh adds it) puts it in /usr/share/games/doom/. */
static const char *wad_dirs[] = {
    "uda:/data/", "uda:/doom/", "uda:/",
#ifdef XENITH_LINUX
    "/usr/share/games/doom/",
#endif
};
static const struct { const char *file, *name; } games[] = {
    {"doom.wad", "DOOM"},
    {"doom2.wad", "DOOM II"},
    {"plutonia.wad", "The Plutonia Experiment"},
    {"tnt.wad", "TNT: Evilution"},
    {"doom1.wad", "DOOM (shareware)"},
    {"freedoom1.wad", "Freedoom: Phase 1"},
    {"freedoom2.wad", "Freedoom: Phase 2"},
};
#define NGAMES ((int)(sizeof games / sizeof *games))
#define CHOICE_FILE "uda:/doom/iwad.txt"

static char found_path[NGAMES][96];
static int found_game[NGAMES], nfound;

static void find_games(void)
{
    int g;
    unsigned d;
    struct stat st;
    nfound = 0;
    for (g = 0; g < NGAMES; g++)
        for (d = 0; d < sizeof wad_dirs / sizeof *wad_dirs; d++) {
            snprintf(found_path[nfound], sizeof found_path[0], "%s%s", wad_dirs[d], games[g].file);
            if (stat(found_path[nfound], &st) == 0 && st.st_size > 0) {
                found_game[nfound++] = g;
                break;
            }
        }
}

/* The game picked last time, or the first one. */
static int last_choice(void)
{
    char line[40] = "";
    int i;
    FILE *f = fopen(CHOICE_FILE, "r");
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
    }
    line[strcspn(line, "\r\n")] = 0;
    for (i = 0; i < nfound; i++)
        if (!strcmp(games[found_game[i]].file, line)) return i;
    return 0;
}

static void save_choice(int i)
{
    FILE *f;
    mkdir("uda:/doom", 0777);
    f = fopen(CHOICE_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", games[found_game[i]].file);
    fclose(f);
}

/* More than one game installed (Freedoom comes as Phase 1 and Phase 2): ask which.
 * Returns its index in found_*, or -1 to go back to XenithOS. */
static int pick_game(void)
{
    int sel = last_choice(), rowh = 66, w = 560, x = (SCR_W - w) / 2;
    int top = (SCR_H - nfound * rowh) / 2 + 40;
    for (;;) {
        int i, d;
        ui_poll();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < nfound) sel += d;
        if (PRESSED(a) || PRESSED(start)) return sel;
        if (PRESSED(b) || PRESSED(back)) return -1;
        /* On the desktop the pointer picks a game as it moves over it, and a click plays it. */
        for (i = 0; ui_desktop && i < nfound; i++)
            if (ui_mouse_in(x, top + i * rowh, w, rowh - 12)) {
                if (mouse.dx || mouse.dy) sel = i;
                if (mouse.clicks & 1) return i;
            }

        ui_bg();
        gfx_str_c(FONT_HEAD, SCR_W / 2, top - 130, C_TEXT, "DOOM");
        gfx_str_c(FONT_BODY, SCR_W / 2, top - 72, C_SUB, "Choose a game");
        for (i = 0; i < nfound; i++) {
            int y = top + i * rowh;
            gfx_fill_rrect((float)x, (float)y, (float)w, rowh - 12.0f, 14, i == sel ? C_ACCENT : C_SURFACE, 255);
            gfx_str_vc(FONT_BODY_SB, x + 26, y + (rowh - 12) / 2, i == sel ? 0xFFFFFF : C_TEXT, games[found_game[i]].name);
        }
        ui_hints(ui_desktop ? "A=Play;B=Close" : "A=Play;B=XenithOS");
        ui_end_frame();
    }
}

static void no_wad_screen(void)
{
    int y = SCR_H / 2 - 120;
    struct pad c, p;

    gfx_clear(C_BG);
    gfx_str_c(FONT_HEAD, SCR_W / 2, y, C_TEXT, "DOOM needs game data");
    y += gfx_line_h(FONT_HEAD) + 18;
    if (plat_kind() == PLAT_XBOX) {
        gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Update DOOM in the Store and it brings Freedoom with it. It's free.");
        y += 36;
        gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Or copy your own doom.wad or doom2.wad into a doom folder on the USB stick.");
    } else {
        gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Get Freedoom from the Store, or install the freedoom package.");
        y += 36;
        gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Or put your own doom.wad or doom2.wad in /usr/share/games/doom.");
    }
    y += 80;
    gfx_str_c(FONT_BODY_SB, SCR_W / 2, y, C_ACCENT_HI, ui_desktop ? "Press Esc to close" : "Press A to go back to XenithOS");
    gfx_present();

    memset(&p, 0, sizeof p);
    for (;;) {
        plat_poll(&c);
        if ((c.a && !p.a) || (c.b && !p.b) || (c.back && !p.back)) break;
        p = c;
        plat_sleep_us(16000);
    }
}

int main(int argc, char **argv)
{
    static char wad[96];
    static char *args[] = {"doom", "-iwad", wad, NULL};
    int pick = 0;

    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    startlog_mark("video");
    gfx_init();
    ui_init();
    loading_screen();
    plat_storage_init();
    startlog_mark("storage");
    settings_load();
    audio_init_opts(0);                /* the mixer only: Doom brings its own sounds */
    audio_set_volume(st.volume);
    startlog_mark("sounds");

    find_games();
    if (!nfound) {
        loading = 0;
        no_wad_screen();
        back_to_xenith();
        return 0;
    }
    if (nfound > 1) {
        pick = pick_game();
        if (pick < 0) {
            back_to_xenith();
            return 0;
        }
        save_choice(pick);
        loading_screen();
    }
    snprintf(wad, sizeof wad, "%s", found_path[pick]);

    mkdir("uda:/doom", 0777);
    atexit(back_to_xenith);       /* Doom's Quit, and its fatal errors, return to XenithOS */

    doomgeneric_Create(3, args);
    startlog_mark("game data");
    for (;;)
        doomgeneric_Tick();
    return 0;
}
