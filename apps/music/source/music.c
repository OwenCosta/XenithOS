/* MUSIC: your own music on the TV.
 *
 * The library has three tabs (LB and RB): Albums as a grid of covers, Artists, and Songs. An album
 * or an artist opens a page of its songs with Play and Shuffle at the top. Whatever is playing goes
 * on while you look around: a bar along the bottom shows it, X pauses it from anywhere, and Y opens
 * Now Playing, where A pauses, left and right go back or on ten seconds, LB and RB go to the
 * previous or next song, X turns shuffle on or off and Y goes through repeat (off, all, one).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "icons.h"
#include "music.h"
#include "platform.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

enum { TAB_ALBUMS, TAB_ARTISTS, TAB_SONGS, TABS };
static const char *const tab_name[TABS] = {"Albums", "Artists", "Songs"};

static void now_playing(void);

/* ---------------------------------------------------------------- pieces */

static void fmt_time(double s, char *out, int n)
{
    int t = s > 0 ? (int)s : 0;
    if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* Three bars going up and down, for what's playing (still while it's paused). */
static void eq_bars(int x, int bottom, int h, uint32_t c)
{
    float t = (float)(ui_ms() % 100000) / 1000.0f;
    int k;
    for (k = 0; k < 3; k++) {
        float f = player_paused() ? 0.35f : 0.35f + 0.65f * fabsf(sinf(t * (3.1f + k * 1.7f) + k * 1.3f));
        int bh = (int)(h * f);
        gfx_fill_rrect((float)(x + k * 7), (float)(bottom - bh), 4, (float)bh, 1.5f, c, 255);
    }
}

static int mini_h(void)
{
    return gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 26;
}

/* Where lists and grids stop: above the hints, and above the bar when something is playing. */
static int content_bottom(void)
{
    return SCR_H - 70 - (player_song() >= 0 ? mini_h() + 12 : 0);
}

/* The bar along the bottom: what's playing, how far in, paused or not. */
static void draw_mini(void)
{
    int song = player_song(), h = mini_h(), y = SCR_H - 70 - h, x = M, w = SCR_W - 2 * M, cs = h - 20, tx;
    char a[24], b[24], t[64];
    double pos, len;
    if (song < 0) return;
    pos = player_pos();
    len = player_len();
    ui_panel(x, y, w, h);
    art_draw(songs[song].album_i, x + 10, y + 10, cs, 6);
    tx = x + 10 + cs + 16;
    gfx_str_fit(FONT_BODY_SB, tx, y + 10, C_TEXT, songs[song].title, w - (tx - x) - 220);
    gfx_str_fit(FONT_SMALL, tx, y + 12 + gfx_line_h(FONT_BODY_SB), C_SUB,
                songs[song].artist[0] ? songs[song].artist : albums[songs[song].album_i].artist, w - (tx - x) - 220);
    fmt_time(pos, a, sizeof a);
    fmt_time(len, b, sizeof b);
    snprintf(t, sizeof t, len > 0 ? "%s / %s" : "%s", a, b);
    gfx_str_r(FONT_SMALL, x + w - 52, y + h / 2 - gfx_line_h(FONT_SMALL) / 2, C_SUB, player_paused() ? "Paused" : t);
    eq_bars(x + w - 38, y + h / 2 + 9, 18, player_paused() ? C_DIM : C_ACCENT);
    gfx_fill_rrect((float)tx, (float)(y + h - 7), (float)(x + w - 16 - tx), 3, 1.5f, C_SURFACE2, 255);
    if (len > 0)
        gfx_fill_rrect((float)tx, (float)(y + h - 7), (x + w - 16 - tx) * (float)(pos / len > 1 ? 1 : pos / len), 3, 1.5f, C_ACCENT, 255);
}

/* X pauses and Y opens Now Playing, on every screen once something plays. */
static void anywhere_keys(void)
{
    const char *why;
    if (player_song() >= 0) {
        if (PRESSED(x)) {
            player_pause(!player_paused());
            sfx(SFX_SELECT);
        } else if (PRESSED(y)) {
            sfx(SFX_SELECT);
            now_playing();
            ui_settle_input();
        }
    }
    if ((why = player_problem())) {
        ui_toast_icon(ICON_SOUND);
        ui_toast("Couldn't play a song", why);
    }
}

/* A row of a list: the choice box behind the selected one is drawn by the list. */
static void row_text(int x, int y, int w, int h, int on, const char *top, const char *under, int playing)
{
    int ty = y + (h - 6 - gfx_line_h(FONT_BODY_SB) - (under ? gfx_line_h(FONT_SMALL) : 0)) / 2, tx = x;
    if (playing) {
        eq_bars(x, ty + gfx_line_h(FONT_BODY_SB) - 6, 16, C_ACCENT);
        tx += 30;
    }
    gfx_str_fit(FONT_BODY_SB, tx, ty, on ? 0xFFFFFF : playing ? C_ACCENT_HI : C_TEXT, top, w - (tx - x));
    if (under) gfx_str_fit(FONT_SMALL, tx, ty + gfx_line_h(FONT_BODY_SB), C_SUB, under, w - (tx - x));
}

static void hints_with(const char *what)
{
    char h[200];
    snprintf(h, sizeof h, "%s%s", what, player_song() >= 0 ? (player_paused() ? ";X=Play;Y=Now playing" : ";X=Pause;Y=Now playing") : "");
    ui_hints(h);
}

/* --------------------------------------------------------- now playing */

static void now_playing(void)
{
    int up[4], i;
    ui_fade_in();
    for (;;) {
        int song = player_song(), top = HEADER_H + 10, cs = SCR_H - top - 90, x, y, w, n;
        double pos, len;
        char a[24], b[24], t[96];
        ui_poll();
        if (PRESSED(b) || song < 0) { sfx(SFX_BACK); return; }
        if (PRESSED(a)) { player_pause(!player_paused()); sfx(SFX_SELECT); }
        else if (PRESSED(left)) { player_seek(-10); sfx(SFX_MOVE); }
        else if (PRESSED(right)) { player_seek(10); sfx(SFX_MOVE); }
        else if (PRESSED(lb)) { player_skip(-1); sfx(SFX_MOVE); }
        else if (PRESSED(rb)) { player_skip(1); sfx(SFX_MOVE); }
        else if (PRESSED(x)) {
            player_set_shuffle(!player_shuffle());
            sfx(SFX_SELECT);
            ui_toast_icon(ICON_SWITCH);
            ui_toast(player_shuffle() ? "Shuffle is on" : "Shuffle is off",
                     player_shuffle() ? "The rest of the songs come in a random order." : "The songs come in their own order.");
        } else if (PRESSED(y)) {
            static const char *const said[3][2] = {
                {"Repeat is off", "It stops after the last song."},
                {"Repeat all", "After the last song it starts again from the first."},
                {"Repeat one", "This song plays again and again."},
            };
            player_set_repeat((player_repeat() + 1) % 3);
            sfx(SFX_SELECT);
            ui_toast_icon(ICON_REFRESH);
            ui_toast(said[player_repeat()][0], said[player_repeat()][1]);
        }
        if ((song = player_song()) < 0) continue;
        if (cs > 380) cs = 380;
        pos = player_pos();
        len = player_len();

        ui_bg();
        art_frame_begin();
        ui_header("Now playing", player_paused() ? "Paused" : NULL);
        art_draw(songs[song].album_i, M, top, cs, 14);
        x = M + cs + 40;
        w = SCR_W - M - x;
        y = top + 4;
        y += (gfx_line_h(FONT_TITLE) + 2) * gfx_wrap(FONT_TITLE, x, y, w, 2, C_TEXT, songs[song].title, 2) + 6;
        gfx_str_fit(FONT_LABEL, x, y, C_TEXT, songs[song].artist[0] ? songs[song].artist : albums[songs[song].album_i].artist, w);
        y += gfx_line_h(FONT_LABEL) + 4;
        gfx_str_fit(FONT_BODY, x, y, C_SUB, albums[songs[song].album_i].name, w);
        y += gfx_line_h(FONT_BODY) + 22;
        /* how far in */
        gfx_fill_rrect((float)x, (float)y, (float)w, 6, 3, C_SURFACE2, 255);
        if (len > 0) gfx_fill_rrect((float)x, (float)y, w * (float)(pos / len > 1 ? 1 : pos / len), 6, 3, C_ACCENT, 255);
        fmt_time(pos, a, sizeof a);
        gfx_str(FONT_SMALL, x, y + 14, C_SUB, a);
        if (len > 0) { fmt_time(len, b, sizeof b); gfx_str_r(FONT_SMALL, x + w, y + 14, C_SUB, b); }
        y += 14 + gfx_line_h(FONT_SMALL) + 18;
        /* shuffle and repeat, as chips */
        {
            const char *chips[2];
            int k, cx = x;
            chips[0] = player_shuffle() ? "Shuffle on" : "Shuffle off";
            chips[1] = player_repeat() == REPEAT_ALL ? "Repeat all" : player_repeat() == REPEAT_ONE ? "Repeat one" : "Repeat off";
            for (k = 0; k < 2; k++) {
                int on = k == 0 ? player_shuffle() : player_repeat() != REPEAT_OFF, cw = gfx_str_w(FONT_SMALL, chips[k]) + 28;
                int chh = gfx_line_h(FONT_SMALL) + 12;
                gfx_fill_rrect((float)cx, (float)y, (float)cw, (float)chh, chh / 2.0f, on ? C_ACCENT : C_SURFACE2, 255);
                gfx_str_c(FONT_SMALL, cx + cw / 2, y + 6, on ? 0xFFFFFF : C_SUB, chips[k]);
                cx += cw + 10;
            }
            y += gfx_line_h(FONT_SMALL) + 12 + 26;
        }
        /* what's next */
        n = player_upcoming(up, 4);
        if (n && y + gfx_line_h(FONT_BODY_SB) + 50 < SCR_H - 70) {
            int rh = gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 14;
            gfx_str(FONT_BODY_SB, x, y, C_TEXT, "Up next");
            y += gfx_line_h(FONT_BODY_SB) + 10;
            for (i = 0; i < n && y + rh <= SCR_H - 74; i++, y += rh) {
                int s = up[i];
                art_draw(songs[s].album_i, x, y, rh - 10, 6);
                gfx_str_fit(FONT_BODY_SB, x + rh + 4, y - 2, C_TEXT, songs[s].title, w - rh - 4);
                gfx_str_fit(FONT_SMALL, x + rh + 4, y + gfx_line_h(FONT_BODY_SB) - 4, C_SUB,
                            songs[s].artist[0] ? songs[s].artist : albums[songs[s].album_i].artist, w - rh - 4);
            }
        }
        snprintf(t, sizeof t, "%s;LEFT/RIGHT=10 seconds;LB/RB=Previous, next;X=Shuffle;Y=Repeat;B=Back",
                 player_paused() ? "A=Play" : "A=Pause");
        ui_hints(t);
        art_load_one(0);
        ui_end_frame();
    }
}

/* ------------------------------------------------------- album, artist */

/* A page of songs: a cover and words on the left, then Play, Shuffle and the songs. list is the
 * songs in order; album >= 0 for an album's page, else an artist's. */
static void songs_page(const int *list, int n, int album, const char *title, const char *by)
{
    int sel = 0, first = 0, i;
    float hl = 0;
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 10, cs = 300, lx = M + cs + 36, lw = SCR_W - M - lx;
        int rowh = gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 18, rows = n + 2, visible, d, playing = player_song();
        char sub[96];
        visible = (content_bottom() - top) / rowh;
        if (visible < 1) visible = 1;
        ui_poll();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < rows) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (sel == 0) player_play(list, n, 0, 0);
            else if (sel == 1) player_play(list, n, 0, 1);
            else player_play(list, n, sel - 2, -1);
        }
        anywhere_keys();
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;

        ui_bg();
        art_frame_begin();
        ui_header(album >= 0 ? "Album" : "Artist", NULL);
        if (cs > content_bottom() - top - 110) cs = content_bottom() - top - 110;
        if (album >= 0) art_draw(album, M, top, cs, 14);
        else art_draw(n ? songs[list[0]].album_i : -1, M, top, cs, 14);
        {
            int y = top + cs + 14;
            y += (gfx_line_h(FONT_LABEL) + 2) * gfx_wrap(FONT_LABEL, M, y, cs, 2, C_TEXT, title, 2);
            if (by) { gfx_str_fit(FONT_BODY, M, y, C_SUB, by, cs); y += gfx_line_h(FONT_BODY); }
            snprintf(sub, sizeof sub, n == 1 ? "1 song" : "%d songs", n);
            gfx_str(FONT_SMALL, M, y + 2, C_DIM, sub);
        }
        hl = ui_ease(hl, (float)(sel - first), 0.45f);
        ui_select_box((float)lx, top + hl * rowh, (float)lw, rowh - 6.0f, 10);
        for (i = 0; i < visible && first + i < rows; i++) {
            int idx = first + i, y = top + i * rowh, on = idx == sel;
            if (idx < 2) {
                icon_draw(idx == 0 ? ICON_PLAY : ICON_SWITCH, lx + 30.0f, y + (rowh - 6) / 2.0f, 26, on ? 0xFFFFFF : C_ACCENT_HI, on ? C_SEL : C_BG);
                gfx_str_vc(FONT_BODY_SB, lx + 60, y + (rowh - 6) / 2, on ? 0xFFFFFF : C_TEXT, idx == 0 ? "Play" : "Shuffle");
            } else {
                const struct song *s = &songs[list[idx - 2]];
                char num[16], under[160];
                int tx = lx + 18;
                if (album >= 0) {
                    snprintf(num, sizeof num, "%d", s->track > 0 ? s->track : idx - 1);
                    gfx_str_r(FONT_BODY, tx + 30, y + (rowh - 6 - gfx_line_h(FONT_BODY)) / 2, C_DIM, num);
                    tx += 46;
                    /* the artist under it when it isn't the album's */
                    snprintf(under, sizeof under, "%s", s->artist[0] && strcasecmp(s->artist, albums[album].artist) ? s->artist : "");
                } else {
                    snprintf(under, sizeof under, "%s", albums[s->album_i].name);
                }
                row_text(tx, y, lw - (tx - lx) - 16, rowh, on, s->title, under[0] ? under : NULL, list[idx - 2] == playing);
            }
        }
        draw_mini();
        hints_with(sel < 2 ? (sel ? "A=Shuffle;B=Back" : "A=Play;B=Back") : "A=Play from here;B=Back");
        art_load_one(0);
        ui_end_frame();
    }
}

/* -------------------------------------------------------------- library */

static char counts[96];             /* "19 songs, 7 albums, 6 artists", under the name */

static int tab_x(int i, int *w)
{
    int tw = gfx_str_w(FONT_TITLE, "Music"), sw = gfx_str_w(FONT_SMALL, counts), x = M + (tw > sw ? tw : sw) + 40, k;
    for (k = 0; k < i; k++) x += gfx_str_w(FONT_BODY_SB, tab_name[k]) + 44 + 8;
    if (w) *w = gfx_str_w(FONT_BODY_SB, tab_name[i]) + 44;
    return x;
}

static void draw_top(int tab, float tab_hl)
{
    int i, w, h = gfx_line_h(FONT_BODY_SB) + 18, y = 32, x0, xl, wl;
    snprintf(counts, sizeof counts, "%d %s, %d %s, %d %s", nsongs, nsongs == 1 ? "song" : "songs",
             nalbums, nalbums == 1 ? "album" : "albums", nartists, nartists == 1 ? "artist" : "artists");
    x0 = tab_x(0, NULL);
    gfx_str(FONT_TITLE, M, 30, C_TEXT, "Music");
    gfx_str(FONT_SMALL, M, 30 + gfx_line_h(FONT_TITLE) + 2, C_SUB, counts);
    xl = tab_x(TABS - 1, &wl);
    gfx_fill_rrect((float)(x0 - 4), (float)(y - 4), (float)(xl + wl - x0 + 8), (float)(h + 8), (h + 8) / 2.0f, C_SURFACE, 255);
    gfx_stroke_rrect((float)(x0 - 4), (float)(y - 4), (float)(xl + wl - x0 + 8), (float)(h + 8), (h + 8) / 2.0f, 1, C_LINE, 255);
    {
        int fl = (int)floorf(tab_hl), fx, fw, cx, cw;
        float f = tab_hl - fl;
        fx = tab_x(fl, &fw);
        cx = fl + 1 < TABS ? tab_x(fl + 1, &cw) : fx;
        if (fl + 1 >= TABS) cw = fw;
        gfx_fill_rrect(fx + (cx - fx) * f, (float)y, fw + (cw - fw) * f, (float)h, h / 2.0f, C_ACCENT, 255);
    }
    for (i = 0; i < TABS; i++) {
        int x = tab_x(i, &w);
        gfx_str_c(FONT_BODY_SB, x + w / 2, y + 9, i == tab ? 0xFFFFFF : C_SUB, tab_name[i]);
    }
    ui_profile_chip(SCR_W - M, 54);
}

static void empty_library(void)
{
    icon_draw(ICON_SOUND, SCR_W / 2.0f, SCR_H / 2.0f - 50, 70, C_DIM, C_BG);
    gfx_str_c(FONT_LABEL, SCR_W / 2, SCR_H / 2 + 10, C_TEXT, "No music yet");
    gfx_wrap(FONT_BODY, M + 100, SCR_H / 2 + 50, SCR_W - 2 * M - 200, 4, C_SUB, music_where(), 3);
}

static void scan(void)
{
    int found = 0;
    char line[64];
    library_scan_begin();
    for (;;) {
        int done = library_scan_step(12, &found);
        snprintf(line, sizeof line, found == 1 ? "1 song so far" : "%d songs so far", found);
        if (done) break;
        ui_loading_frame("Looking for music", line);
    }
}

int main(int argc, char **argv)
{
    int tab = TAB_ALBUMS, sel[TABS] = {0, 0, 0}, first[TABS] = {0, 0, 0}, i;
    float hl[TABS] = {0, 0, 0}, tab_hl = 0, lift[64];
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    gfx_init();
    ui_init();
    ui_loading_frame("Music", "Loading");
    plat_storage_init();
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    audio_set_ui_sounds(st.ui_sounds);
    startlog_mark("sounds");
    if (!library_alloc()) {
        ui_message("Music can't start", "There isn't enough memory.");
        ui_back_to_xenithos("music");
        return 0;
    }
    ui_set_frame_hook(player_feed);
    scan();
    memset(lift, 0, sizeof lift);
    ui_fade_in();

    for (;;) {
        int top = HEADER_H + 10, bottom = content_bottom(), n = tab == TAB_ALBUMS ? nalbums : tab == TAB_ARTISTS ? nartists : nsongs + 1;
        int dx = 0, dy = 0;
        ui_poll();
        if (nsongs) {
            if (PRESSED(rb) || PRESSED(lb)) {
                tab = (tab + (PRESSED(rb) ? 1 : TABS - 1)) % TABS;
                sfx(SFX_MOVE);
                continue;
            }
            dx = ui_nav_x();
            dy = ui_nav_y();
        }
        if (PRESSED(b)) {
            sfx(SFX_BACK);
            player_stop();
            ui_set_frame_hook(NULL);
            ui_back_to_xenithos("music");
            return 0;
        }
        if (!nsongs && PRESSED(y)) {            /* nothing found: look again */
            sfx(SFX_SELECT);
            scan();
            ui_fade_in();
            continue;
        }
        anywhere_keys();

        if (tab == TAB_ALBUMS) {
            int cols = gfx_text_large() ? 6 : 7, gap = 16, cw = (SCR_W - 2 * M - gap * (cols - 1)) / cols;
            int rowh = cw + 12 + gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 14, rows = (bottom - top) / rowh, s = sel[tab];
            if (rows < 1) rows = 1;
            if (n) {
                int to = s + dx + dy * cols;
                if (dy > 0 && to >= n && s / cols < (n - 1) / cols) to = n - 1;   /* down to a short last row */
                if ((dx || dy) && to >= 0 && to < n && !(dx && to / cols != s / cols)) { sel[tab] = to; sfx(SFX_MOVE); }
            }
            if (PRESSED(a) && n) {
                const struct album *al = &albums[albums_by_name[sel[tab]]];
                sfx(SFX_SELECT);
                songs_page(album_songs + al->at, al->n, albums_by_name[sel[tab]], al->name, al->artist);
                ui_settle_input();
                ui_fade_in();
            }
            s = sel[tab];
            if (s / cols < first[tab]) first[tab] = s / cols;
            if (s / cols >= first[tab] + rows) first[tab] = s / cols - rows + 1;

            ui_bg();
            art_frame_begin();
            draw_top(tab, tab_hl = ui_ease(tab_hl, (float)tab, 0.4f));
            {
                int clip[4];
                gfx_get_clip(clip);
                gfx_clip_intersect(0, top - 12, SCR_W, bottom - top + 12);
                for (i = first[tab] * cols; i < n && i < (first[tab] + rows + 1) * cols; i++) {
                    int a = albums_by_name[i], r = i / cols - first[tab], c = i % cols, x = M + c * (cw + gap), y = top + r * rowh;
                    float *l = &lift[i % 64], g;
                    *l = ui_ease(*l, i == s ? 1.0f : 0.0f, 0.35f);
                    g = (1 - *l) * 6;
                    if (i == s) gfx_stroke_rrect(x - 5 - g, y - 5 - g, cw + 10 + 2 * g, cw + 10 + 2 * g, 15 + g, UI_EDGE + 1, 0xFFFFFF, (int)(255 * *l));
                    art_draw(a, x, y, cw, 10);
                    gfx_str_fit(FONT_BODY_SB, x + 2, y + cw + 10, i == s ? 0xFFFFFF : C_TEXT, albums[a].name, cw - 4);
                    gfx_str_fit(FONT_SMALL, x + 2, y + cw + 10 + gfx_line_h(FONT_BODY_SB), C_SUB, albums[a].artist, cw - 4);
                }
                gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
            }
        } else {
            int rowh = gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 18, rows = (bottom - top) / rowh, w = SCR_W - 2 * M;
            int playing = player_song();
            if (rows < 1) rows = 1;
            if (dy && sel[tab] + dy >= 0 && sel[tab] + dy < n) { sel[tab] += dy; sfx(SFX_MOVE); }
            if (PRESSED(a) && n) {
                sfx(SFX_SELECT);
                if (tab == TAB_ARTISTS) {
                    const struct artist *ar = &artists[artists_by_name[sel[tab]]];
                    char by[48];
                    snprintf(by, sizeof by, ar->albums == 1 ? "1 album" : "%d albums", ar->albums);
                    songs_page(artist_songs + ar->at, ar->n, -1, ar->name, by);
                    ui_settle_input();
                    ui_fade_in();
                } else if (sel[tab] == 0) {
                    player_play(songs_by_title, nsongs, 0, 1);      /* Shuffle all */
                } else {
                    player_play(songs_by_title, nsongs, sel[tab] - 1, -1);
                }
            }
            if (sel[tab] < first[tab]) first[tab] = sel[tab];
            if (sel[tab] >= first[tab] + rows) first[tab] = sel[tab] - rows + 1;

            ui_bg();
            art_frame_begin();
            draw_top(tab, tab_hl = ui_ease(tab_hl, (float)tab, 0.4f));
            hl[tab] = ui_ease(hl[tab], (float)(sel[tab] - first[tab]), 0.45f);
            if (n) ui_select_box((float)M, top + hl[tab] * rowh, (float)w, rowh - 6.0f, 10);
            for (i = 0; i < rows && first[tab] + i < n; i++) {
                int idx = first[tab] + i, y = top + i * rowh, on = idx == sel[tab], cs = rowh - 18;
                char under[160];
                if (tab == TAB_ARTISTS) {
                    const struct artist *ar = &artists[artists_by_name[idx]];
                    art_placeholder(ar->name, M + 14, y + 6, cs, cs / 2.0f);
                    snprintf(under, sizeof under, "%d %s, %d %s", ar->albums, ar->albums == 1 ? "album" : "albums",
                             ar->n, ar->n == 1 ? "song" : "songs");
                    row_text(M + 14 + cs + 16, y, w - cs - 46, rowh, on, ar->name, under,
                             playing >= 0 && songs[playing].artist_i == artists_by_name[idx]);
                } else if (idx == 0) {
                    icon_draw(ICON_SWITCH, M + 14 + cs / 2.0f, y + (rowh - 6) / 2.0f, 26, on ? 0xFFFFFF : C_ACCENT_HI, on ? C_SEL : C_BG);
                    snprintf(under, sizeof under, "All %d songs, in a random order", nsongs);
                    row_text(M + 14 + cs + 16, y, w - cs - 46, rowh, on, "Shuffle all", under, 0);
                } else {
                    const struct song *so = &songs[songs_by_title[idx - 1]];
                    art_draw(so->album_i, M + 14, y + 6, cs, 6);
                    snprintf(under, sizeof under, "%s, %s", so->artist[0] ? so->artist : albums[so->album_i].artist, albums[so->album_i].name);
                    row_text(M + 14 + cs + 16, y, w - cs - 46, rowh, on, so->title, under, songs_by_title[idx - 1] == playing);
                }
            }
        }
        if (!nsongs) {
            ui_bg();
            gfx_str(FONT_TITLE, M, 30, C_TEXT, "Music");
            ui_profile_chip(SCR_W - M, 54);
            empty_library();
        }
        draw_mini();
        if (!nsongs) ui_hints("Y=Look again;B=Back");
        else hints_with(tab == TAB_ARTISTS ? "A=Open;LB/RB=Albums, artists, songs;B=Back"
                        : tab == TAB_SONGS ? (sel[tab] ? "A=Play;LB/RB=Albums, artists, songs;B=Back" : "A=Shuffle all;LB/RB=Albums, artists, songs;B=Back")
                        : "A=Open;LB/RB=Albums, artists, songs;B=Back");
        art_load_one(0);
        ui_end_frame();
    }
}
