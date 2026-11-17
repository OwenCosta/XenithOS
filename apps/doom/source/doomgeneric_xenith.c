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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#include "audio.h"
#include "../../../shared/config.h"     /* doomgeneric has a config.h of its own */
#include "cloudsave.h"
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

/* -------------------------------------------------------- achievements */
#include "doomstat.h"                      /* the game's state, which they come from */
#include "social.h"

/* Earned from what the game is doing, looked at every frame: a level finished, with every monster
 * in it beaten, a secret found, the end of an episode or of DOOM II. Never in the title screen's
 * demos. They're kept on the stick and go to XenithOS Cloud when XenithOS next starts (on a PC,
 * at the end of the level), and Doom's own message line says so. */
static void earned(const char *id)
{
    static char line[64];
    if (!social_earn(id)) return;
    snprintf(line, sizeof line, "achievement: %s", social_achievement_name(id));
    players[consoleplayer].message = line;
}

static void check_achievements(void)
{
    static int last = -1, secrets, last_time;
    player_t *p = &players[consoleplayer];
    if (demoplayback || !usergame || !prof.signed_in) { last = gamestate; return; }
    if (gamestate == GS_LEVEL) {
        /* A level starting, or a save loaded (the clock jumps): the secrets so far don't count */
        if (last != GS_LEVEL || leveltime < last_time || leveltime > last_time + 35) secrets = p->secretcount;
        else if (p->secretcount > secrets) {
            secrets = p->secretcount;
            earned("doom-secret");
        }
        last_time = leveltime;
    } else if (last == GS_LEVEL && (gamestate == GS_INTERMISSION || gamestate == GS_FINALE)) {
        earned("doom-exit");
        if (totalkills > 0 && p->killcount >= totalkills) earned("doom-sweep");
        if (gamestate == GS_FINALE) earned("doom-episode");       /* DOOM's episodes end straight into it */
        social_flush();
    } else if (gamestate == GS_FINALE && last != GS_FINALE && gamemode == commercial && gamemap == 30) {
        earned("doom-episode");                                    /* DOOM II's last level */
        social_flush();
    }
    last = gamestate;
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
    check_achievements();
    gfx_blit_scaled(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY, dst_x, dst_y, dst_w, dst_h);
    gfx_present();
    if (!first_frame_done) {
        first_frame_done = 1;
        startlog_ready("DOOM");
    }
    poll_pad();
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
    if (q_tail == q_head) return 0;
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

/* Where DOOM keeps its settings and saves (m_config.c asks): the signed-in profile's own folder,
 * uda:/doom/profiles/<gamertag>/, which XenithOS keeps in step with the profile's cloud saves
 * before DOOM starts and after it closes, so two people on one console never mix saves. A
 * guest uses uda:/doom/ as before. */
static char config_dir[96] = "uda:/doom/";

const char *DG_ConfigDir(void)
{
    return config_dir;
}

static void use_profile_folder(void)
{
    const struct cloud_app *a = cloud_app_find("doom");
    config_load();                           /* XenithOS Cloud's address, for achievements */
    profile_load();
    if (a) profile_folder_start(a);          /* the first time: what's in uda:/doom/ comes along */
    profile_folder("uda:/doom/", config_dir, sizeof config_dir);
    make_folders(config_dir);
}

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

        ui_bg();
        gfx_str_c(FONT_HEAD, SCR_W / 2, top - 130, C_TEXT, "DOOM");
        gfx_str_c(FONT_BODY, SCR_W / 2, top - 72, C_SUB, "Choose a game");
        for (i = 0; i < nfound; i++) {
            int y = top + i * rowh;
            gfx_fill_rrect((float)x, (float)y, (float)w, rowh - 12.0f, 14, i == sel ? C_ACCENT : C_SURFACE, 255);
            gfx_str_vc(FONT_BODY_SB, x + 26, y + (rowh - 12) / 2, i == sel ? 0xFFFFFF : C_TEXT, games[found_game[i]].name);
        }
        ui_hints("A=Play;B=XenithOS");
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
    gfx_str_c(FONT_BODY_SB, SCR_W / 2, y, C_ACCENT_HI, "Press A to go back to XenithOS");
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
    use_profile_folder();
    atexit(back_to_xenith);       /* Doom's Quit, and its fatal errors, return to XenithOS */

    doomgeneric_Create(3, args);
    startlog_mark("game data");
    for (;;)
        doomgeneric_Tick();
    return 0;
}
