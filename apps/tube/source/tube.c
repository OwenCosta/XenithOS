/* TUBE: YouTube on XenithOS.
 *
 * The Xbox can't talk to YouTube itself (no TLS, and nothing to decode YouTube's video
 * formats), so XenithOS Cloud does that part. It searches, and turns the chosen video into a
 * small stream the console can play: 426x240 JPEG pictures, 20 a second, with 24 kHz mono
 * sound, packed in an AVI file. TUBE fetches that stream 256 KB at a time and plays it. The
 * sound is the clock: each picture is shown when the sound reaches its moment.
 *
 * You need to be signed in to a XenithOS profile to use it.
 *
 * Controls: D-pad to pick a video, A to play, Y to search, B back to XenithOS.
 * Playing: A pauses, left and right skip 10 seconds, B goes back to the list.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "img.h"
#include "jpeg.h"
#include "keyboard.h"
#include "net.h"
#include "platform.h"
#include "handoff.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

/* ---------------------------------------------------------------- helpers */

static void kv(const char *body, const char *key, char *out, int n)
{
    size_t kl = strlen(key);
    const char *p = body;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            int i = 0;
            p += kl + 1;
            while (*p && *p != '\n' && i < n - 1) out[i++] = *p++;
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

static int fields(char *line, char **f, int max)
{
    int n = 0;
    while (n < max) {
        f[n++] = line;
        line = strchr(line, '|');
        if (!line) break;
        *line++ = 0;
    }
    return n;
}

static const char *problem(struct http_resp *r, int code)
{
    static char msg[120];
    if (code < 0) return net_error(code);
    msg[0] = 0;
    if (r && r->body) kv(r->body, "error", msg, sizeof msg);
    if (!msg[0]) snprintf(msg, sizeof msg, "XenithOS Cloud answered with error %d.", code);
    return msg;
}

static void clock_text(int s, char *out, int n)
{
    if (s < 0) s = 0;
    if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

static void back_to_shell(void)
{
    plat_rumble(0, 0);
    ui_back_to_xenithos("tube");
}

/* ---------------------------------------------------------------- results */

#define MAX_RESULTS 16
#define COLS 4
static struct result {
    char id[16], title[100], channel[48], dur[12];
    int seconds;
    struct img thumb;
    int tried;
} results[MAX_RESULTS];
static int nresults, sel, top_row;
static char query[64];
static const char *const suggestions[] = {"Music", "Speedruns", "Minecraft", "Retro games", "Space", "Xbox 360"};
#define NSUGG 6

static void clear_results(void)
{
    int i;
    for (i = 0; i < nresults; i++) img_free(&results[i].thumb);
    nresults = 0;
    sel = top_row = 0;
}

static int search(const char *q)
{
    struct http_resp r;
    char form[400], tok[160], eq[200], *line;
    int code;
    net_escape(prof.token, tok, sizeof tok);
    net_escape(q, eq, sizeof eq);
    snprintf(form, sizeof form, "token=%s&q=%s", tok, eq);
    ui_busy_begin("Searching", q);
    code = http_request(cfg.server, "POST", "/v1/tube/search", form, (int)strlen(form), 64 * 1024, NULL, NULL, &r);
    ui_busy_end();
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("The search didn't work", problem(&r, code));
        http_free(&r);
        return 0;
    }
    clear_results();
    for (line = r.body; line && *line && nresults < MAX_RESULTS; ) {
        char *next = strchr(line, '\n'), *f[4];
        if (next) *next++ = 0;
        if (fields(line, f, 4) == 4 && strlen(f[0]) == 11) {
            struct result *it = &results[nresults++];
            memset(it, 0, sizeof *it);
            snprintf(it->id, sizeof it->id, "%s", f[0]);
            snprintf(it->title, sizeof it->title, "%s", f[1]);
            snprintf(it->channel, sizeof it->channel, "%s", f[2]);
            it->seconds = atoi(f[3]);
            if (it->seconds > 0) clock_text(it->seconds, it->dur, sizeof it->dur);
        }
        line = next;
    }
    http_free(&r);
    snprintf(query, sizeof query, "%s", q);
    if (!nresults) ui_message("Nothing found", "Try different words.");
    return nresults > 0;
}

/* Thumbnails arrive one at a time while the list is up. */
static void load_thumb(struct result *it)
{
    struct http_resp r;
    char path[64];
    int w, h;
    uint32_t *px;
    it->tried = 1;
    snprintf(path, sizeof path, "/v1/tube/thumb/%s.jpg", it->id);
    if (http_request(cfg.server, "GET", path, NULL, 0, 256 * 1024, NULL, NULL, &r) == 200 &&
        (px = jpeg_decode((const unsigned char *)r.body, r.body_len, &w, &h))) {
        it->thumb.w = w;
        it->thumb.h = h;
        it->thumb.px = px;
        it->thumb.id = gfx_new_image_id();
    }
    http_free(&r);
}

static void draw_list(void);
static void list_idle(void) { draw_list(); ui_end_frame(); }

static int card_w(void) { return (SCR_W - 2 * M - (COLS - 1) * 20) / COLS; }

static void draw_list(void)
{
    int top = HEADER_H + 10, cw = card_w(), ch = cw * 9 / 16, rowh = ch + 76, i, sy = top, x, y;
    char line[96];

    ui_bg();
    ui_header("Tube", nresults ? query : "YouTube on XenithOS");

    /* The search bar: selected when sel == -1 */
    gfx_fill_rrect((float)M, (float)sy, (float)(SCR_W - 2 * M), 48, 24, sel < 0 ? C_ACCENT : C_SURFACE, 255);
    gfx_ring(M + 27.0f, sy + 21.0f, 8, 3, sel < 0 ? 0xFFFFFF : C_SUB, 255);           /* magnifier */
    gfx_line(M + 33.0f, sy + 27.0f, M + 39.0f, sy + 33.0f, 3.5f, sel < 0 ? 0xFFFFFF : C_SUB, 255);
    snprintf(line, sizeof line, "%s", query[0] ? query : "Search YouTube");
    gfx_str_vc(FONT_BODY_SB, M + 56, sy + 24, sel < 0 ? 0xFFFFFF : query[0] ? C_TEXT : C_SUB, line);
    sy += 64;

    if (!nresults) {
        gfx_str(FONT_BODY_SB, M, sy, C_SUB, "Try one of these");
        for (i = 0, x = M; i < NSUGG; i++) {
            int w = gfx_str_w(FONT_BODY_SB, suggestions[i]) + 44, s = sel == i;
            gfx_fill_rrect((float)x, sy + 40.0f, (float)w, 48, 24, s ? C_ACCENT : C_SURFACE, 255);
            gfx_str_vc(FONT_BODY_SB, x + 22, sy + 64, s ? 0xFFFFFF : C_TEXT, suggestions[i]);
            x += w + 14;
        }
        ui_hints("A=Select;B=XenithOS");
        return;
    }

    for (i = top_row * COLS; i < nresults && i < (top_row + 2) * COLS; i++) {
        struct result *it = &results[i];
        int s = i == sel;
        x = M + (i % COLS) * (cw + 20);
        y = sy + (i / COLS - top_row) * rowh;
        if (s) gfx_stroke_rrect(x - 5.0f, y - 5.0f, cw + 10.0f, ch + 10.0f, 15, 3, 0xFFFFFF, 255);
        if (it->thumb.px) gfx_image_rounded(&it->thumb, x, y, cw, ch, 10, 255);
        else gfx_fill_rrect((float)x, (float)y, (float)cw, (float)ch, 10, C_SURFACE2, 255);
        if (it->dur[0]) {
            int dw = gfx_str_w(FONT_SMALL, it->dur) + 14;
            gfx_fill_rrect((float)(x + cw - dw - 8), (float)(y + ch - 34), (float)dw, 26, 6, 0x000000, 200);
            gfx_str(FONT_SMALL, x + cw - dw - 1, y + ch - 32, 0xFFFFFF, it->dur);
        }
        gfx_str_fit(FONT_BODY_SB, x, y + ch + 10, s ? C_TEXT : C_SUB, it->title, cw);
        gfx_str_fit(FONT_SMALL, x, y + ch + 42, C_DIM, it->channel, cw);
    }
    ui_hints("A=Play;Y=Search;B=XenithOS");
}

/* ---------------------------------------------------------------- player */

#define CHUNK (256 * 1024)
#define VQ 64
#define AUDIO_HELD (48000 * 4)        /* up to 4 s of sound waiting for the mixer */

static struct {
    char sid[40], title[100], why[120];   /* why: the server's reason when the video won't play */
    int fps, rate, duration, start;   /* start: the second of the video this stream begins at */
    unsigned char *buf;
    int len, pos, cap;
    long long next_off;
    int in_movi, ended, failed, fetch_fails;
    struct { unsigned char *jpg; int len, index; } vq[VQ];
    int vq_n, video_index;
    int16_t *held;
    int held_len, playing, paused, had_audio, buffering;
    uint32_t *frame;
    int fw, fh, shown;
    uint64_t wall0, overlay_until;
} pl;

static void vq_drop_first(void)
{
    free(pl.vq[0].jpg);
    memmove(&pl.vq[0], &pl.vq[1], sizeof pl.vq[0] * (size_t)(pl.vq_n - 1));
    pl.vq_n--;
}

static void player_reset(void)
{
    while (pl.vq_n) vq_drop_first();
    free(pl.buf);
    free(pl.held);
    free(pl.frame);
    memset(&pl, 0, sizeof pl);
}

static void stop_session(void)
{
    struct http_resp r;
    char path[80];
    if (!pl.sid[0]) return;
    snprintf(path, sizeof path, "/v1/tube/stop?s=%s", pl.sid);
    http_request(cfg.server, "GET", path, NULL, 0, 1024, NULL, NULL, &r);
    http_free(&r);
    pl.sid[0] = 0;
}

static int start_session(const char *id, int at)
{
    struct http_resp r;
    char form[300], tok[160], v[32];
    int code;
    snprintf(form, sizeof form, "token=%s&id=%s&t=%d", (net_escape(prof.token, tok, sizeof tok), tok), id, at);
    code = http_request(cfg.server, "POST", "/v1/tube/start", form, (int)strlen(form), 8 * 1024, NULL, NULL, &r);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("This video won't play", problem(&r, code));
        http_free(&r);
        return 0;
    }
    kv(r.body, "session", pl.sid, sizeof pl.sid);
    kv(r.body, "title", pl.title, sizeof pl.title);
    kv(r.body, "fps", v, sizeof v); pl.fps = atoi(v) > 0 ? atoi(v) : 20;
    kv(r.body, "rate", v, sizeof v); pl.rate = atoi(v) > 0 ? atoi(v) : 24000;
    kv(r.body, "duration", v, sizeof v); pl.duration = atoi(v);
    http_free(&r);
    pl.start = at;
    pl.cap = 4 * CHUNK;
    pl.buf = malloc(pl.cap);
    pl.held = malloc(sizeof(int16_t) * AUDIO_HELD);
    return pl.sid[0] && pl.buf && pl.held;
}

/* Where playback is, in milliseconds into the video. */
static int now_ms(void)
{
    if (pl.had_audio) return pl.start * 1000 + (int)((unsigned long long)audio_stream_clock() * 1000 / 48000);
    if (!pl.playing) return pl.start * 1000;
    return pl.start * 1000 + (int)((plat_time_us() - pl.wall0) / 1000);
}

/* 24 kHz little-endian mono -> 48 kHz samples waiting for the mixer. */
static int take_audio(const unsigned char *p, int bytes)
{
    int n = bytes / 2, i;
    if (pl.held_len + 2 * n > AUDIO_HELD) return 0;
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
        int16_t nx = i + 1 < n ? (int16_t)(p[i * 2 + 2] | (p[i * 2 + 3] << 8)) : s;
        pl.held[pl.held_len++] = s;
        pl.held[pl.held_len++] = (int16_t)((s + nx) / 2);
    }
    pl.had_audio = 1;
    return 1;
}

/* Read AVI chunks out of the fetched bytes until something is full or more bytes are needed. */
static void parse(void)
{
    for (;;) {
        unsigned char *p = pl.buf + pl.pos;
        int avail = pl.len - pl.pos, size, total;
        if (!pl.in_movi) {
            int i;
            for (i = 0; i + 4 <= avail; i++)
                if (!memcmp(p + i, "movi", 4)) break;
            if (i + 4 > avail) {
                if (avail > 256 * 1024) pl.failed = 1;        /* that isn't an AVI */
                return;
            }
            pl.pos += i + 4;
            pl.in_movi = 1;
            continue;
        }
        if (avail < 8) return;
        size = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
        if (!memcmp(p, "LIST", 4) || !memcmp(p, "RIFF", 4)) {   /* step into it (long videos get more RIFFs) */
            if (avail < 12) return;
            pl.pos += 12;
            continue;
        }
        if (size < 0 || size > 4 * 1024 * 1024) { pl.failed = 1; return; }
        total = 8 + size + (size & 1);
        if (avail < total) return;
        if ((p[2] == 'd' && (p[3] == 'c' || p[3] == 'b'))) {
            if (pl.vq_n == VQ) return;                          /* wait for the pictures to be shown */
            pl.vq[pl.vq_n].jpg = malloc(size);
            if (pl.vq[pl.vq_n].jpg) {
                memcpy(pl.vq[pl.vq_n].jpg, p + 8, size);
                pl.vq[pl.vq_n].len = size;
                pl.vq[pl.vq_n].index = pl.video_index;
                pl.vq_n++;
            }
            pl.video_index++;
        } else if (p[2] == 'w' && p[3] == 'b') {
            if (!take_audio(p + 8, size)) return;               /* wait for the mixer */
        }
        pl.pos += total;
    }
}

/* Hand waiting sound to the mixer; start (or restart) playing once enough is buffered. */
static void feed_audio(void)
{
    int n;
    if (!pl.playing) {
        int want = pl.buffering ? 48000 / 2 : 48000;           /* half a second after a stall */
        if (pl.held_len >= want || (pl.ended && (pl.held_len || pl.vq_n)) ||
            (!pl.had_audio && pl.video_index > pl.fps * 2)) {
            pl.playing = 1;
            pl.buffering = 0;
            pl.wall0 = plat_time_us();
            audio_stream_pause(pl.paused);
        }
    }
    if (!pl.playing || !pl.held_len) return;
    n = audio_stream_space();
    if (n > pl.held_len) n = pl.held_len;
    if (n <= 0) return;
    audio_stream_write(pl.held, n);
    memmove(pl.held, pl.held + n, sizeof(int16_t) * (size_t)(pl.held_len - n));
    pl.held_len -= n;
}

static void show_due_picture(void)
{
    int due = (int)((long long)(now_ms() - pl.start * 1000) * pl.fps / 1000), w, h, k = -1, i;
    uint32_t *px;
    if (!pl.playing) return;
    for (i = 0; i < pl.vq_n && pl.vq[i].index <= due; i++) k = i;
    if (k < 0) return;
    while (k-- > 0) vq_drop_first();                    /* late pictures are skipped */
    px = jpeg_decode(pl.vq[0].jpg, pl.vq[0].len, &w, &h);
    pl.shown = pl.vq[0].index;
    vq_drop_first();
    if (!px) return;
    free(pl.frame);
    pl.frame = px;
    pl.fw = w;
    pl.fh = h;
}

static void draw_player(void)
{
    int dh = SCR_H, dw = dh * 16 / 9, dx, dy, t = now_ms() / 1000;
    char a[16], b[16], line[48];
    if (dw > SCR_W) { dw = SCR_W; dh = dw * 9 / 16; }
    dx = (SCR_W - dw) / 2;
    dy = (SCR_H - dh) / 2;
    gfx_clear(0);
    if (pl.frame) gfx_blit_scaled(pl.frame, pl.fw, pl.fh, dx, dy, dw, dh);
    if (!pl.playing || pl.buffering) ui_spinner(SCR_W / 2, SCR_H / 2, 26, C_ACCENT);
    if (!pl.next_off && !pl.paused)             /* XenithOS Cloud is still asking YouTube */
        gfx_str_c(FONT_BODY, SCR_W / 2, SCR_H / 2 + 48, 0xC8D0DC, "Getting the video from YouTube");
    if (ui_ms() < pl.overlay_until || pl.paused || !pl.playing) {
        int by = SCR_H - 118, bw = SCR_W - 2 * M;
        gfx_rect_a(0, by - 20, SCR_W, SCR_H - by + 20, 0x000000, 170);
        gfx_str_fit(FONT_BODY_SB, M, by - 6, 0xFFFFFF, pl.title, bw - 220);
        clock_text(t, a, sizeof a);
        clock_text(pl.duration, b, sizeof b);
        snprintf(line, sizeof line, pl.duration ? "%s / %s" : "%s", a, b);
        gfx_str_r(FONT_BODY_SB, M + bw, by - 6, 0xFFFFFF, line);
        gfx_fill_rrect((float)M, by + 38.0f, (float)bw, 6, 3, 0x505864, 255);
        if (pl.duration > 0)
            gfx_fill_rrect((float)M, by + 38.0f, bw * (float)(t < pl.duration ? t : pl.duration) / pl.duration, 6, 3, C_ACCENT, 255);
        ui_hints(pl.paused ? "A=Play;Left=-10s;Right=+10s;B=Back" : "A=Pause;Left=-10s;Right=+10s;B=Back");
    }
}

/* Everything except fetching; also runs while a fetch is waiting on the network. */
static void tick(void)
{
    parse();
    feed_audio();
    if (pl.playing && pl.had_audio && !pl.paused && !audio_stream_buffered() && !pl.held_len && !pl.ended) {
        pl.playing = 0;                         /* ran dry: wait for more before going on */
        pl.buffering = 1;
    }
    show_due_picture();
    draw_player();
    ui_end_frame();
}

static void fetch(void)
{
    struct http_resp r;
    char path[96];
    int code;
    snprintf(path, sizeof path, "/v1/tube/chunk?s=%s&off=%lld", pl.sid, pl.next_off);
    code = http_request(cfg.server, "GET", path, NULL, 0, CHUNK, NULL, NULL, &r);
    if (code == 200 && r.body_len > 0) {
        if (pl.pos > 0) {                       /* drop what's been read */
            memmove(pl.buf, pl.buf + pl.pos, pl.len - pl.pos);
            pl.len -= pl.pos;
            pl.pos = 0;
        }
        if (pl.len + r.body_len > pl.cap) {
            unsigned char *nb = realloc(pl.buf, pl.len + r.body_len);
            if (nb) { pl.buf = nb; pl.cap = pl.len + r.body_len; }
        }
        if (pl.len + r.body_len <= pl.cap) {
            memcpy(pl.buf + pl.len, r.body, r.body_len);
            pl.len += r.body_len;
            pl.next_off += r.body_len;
        }
        pl.fetch_fails = 0;
    } else if (code == 410) {
        pl.ended = 1;
    } else if (code >= 400 && r.body && strstr(r.body, "error=")) {   /* the server says why */
        snprintf(pl.why, sizeof pl.why, "%s", problem(&r, code));
        pl.failed = 1;
    } else if (code != 204) {
        if (++pl.fetch_fails >= 4) pl.failed = 1;
    }
    http_free(&r);
}

/* Play from second `at`. Returns the second it stopped at (or -1 when the video ended or
 * the player was closed), so seeking can restart from there. */
static int play_from(const struct result *it, int at, int *closed)
{
    *closed = 0;
    player_reset();
    ui_busy_begin("Starting the video", it->title);
    if (!start_session(it->id, at)) { ui_busy_end(); player_reset(); *closed = 1; return -1; }
    ui_busy_end();
    if (!pl.title[0]) snprintf(pl.title, sizeof pl.title, "%s", it->title);
    if (!pl.duration) pl.duration = it->seconds;
    audio_stream_open();
    audio_stream_pause(1);
    pl.overlay_until = ui_ms() + 3000;
    net_set_idle_hook(tick);

    for (;;) {
        int dx;
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); *closed = 1; break; }
        if (PRESSED(a)) {
            pl.paused = !pl.paused;
            audio_stream_pause(pl.paused);
            if (!pl.had_audio && !pl.paused) pl.wall0 = plat_time_us() - (uint64_t)(now_ms() - pl.start * 1000) * 1000;
            pl.overlay_until = ui_ms() + 3000;
        }
        dx = ui_nav_x();
        if (dx) {                               /* skip: a new stream from the new spot */
            int to = now_ms() / 1000 + dx * 10;
            if (to < 0) to = 0;
            if (pl.duration && to >= pl.duration) to = pl.duration - 1;
            sfx(SFX_MOVE);
            net_set_idle_hook(NULL);
            stop_session();
            audio_stream_close();
            return to;
        }
        if (ui_nav_y()) pl.overlay_until = ui_ms() + 3000;
        tick();
        if (pl.failed) {
            net_set_idle_hook(NULL);
            sfx(SFX_ERROR);
            if (pl.why[0]) ui_message("This video won't play", pl.why);
            else ui_message("Playback stopped", "The video stream broke off. Try playing it again.");
            *closed = 1;
            break;
        }
        if (pl.ended && !pl.vq_n && !pl.held_len && !audio_stream_buffered()) break;   /* the end */
        if (!pl.ended && pl.len - pl.pos < 3 * CHUNK && pl.vq_n < VQ - 8 && pl.held_len < AUDIO_HELD / 2) fetch();
    }
    net_set_idle_hook(NULL);
    stop_session();
    audio_stream_close();
    player_reset();
    return -1;
}

static void play(const struct result *it)
{
    int at = 0, closed = 0;
    while ((at = play_from(it, at, &closed)) >= 0 && !closed) { }
}

/* ------------------------------------------------------------------- main */

static void ask_search(void)
{
    char q[64];
    snprintf(q, sizeof q, "%s", query);
    if (keyboard("Search YouTube", "Type what you want to watch", q, 48, KB_TEXT) && q[0]) search(q);
}

int main(int argc, char **argv)
{
    int started = 0;
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    startlog_mark("video");
    gfx_init();
    ui_init();
    ui_loading_frame("Tube", "Loading");
    plat_storage_init();
    startlog_mark("storage");
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    startlog_mark("sounds");

    if (!cfg.server[0]) {
        ui_message("XenithOS Cloud isn't set up", "Tube plays YouTube through XenithOS Cloud, and this copy of XenithOS doesn't have its address.");
        back_to_shell();
        return 0;
    }
    if (!prof.signed_in) {
        ui_message("Sign in first", "Tube needs a XenithOS profile. Make one or sign in from Profile on the home screen.");
        back_to_shell();
        return 0;
    }
    if (!net_is_up()) {
        ui_loading_frame("Connecting", "Getting an address from your router");
        audio_prepare_stall();
        if (!net_up()) {
            ui_message("No network", plat_kind() == PLAT_XBOX ? "Plug an Ethernet cable into the Xbox, then try again." : "Connect to the internet, then try again.");
            back_to_shell();
            return 0;
        }
    }
    sel = -1;
    ui_fade_in();

    for (;;) {
        int dx, dy, i;
        ui_poll();
        dx = ui_nav_x();
        dy = ui_nav_y();
        if (!nresults) {
            if (dy > 0 && sel < 0) { sel = 0; sfx(SFX_MOVE); }
            else if (dy < 0 && sel >= 0) { sel = -1; sfx(SFX_MOVE); }
            else if (dx && sel >= 0 && sel + dx >= 0 && sel + dx < NSUGG) { sel += dx; sfx(SFX_MOVE); }
        } else {
            if (dy < 0 && sel >= 0 && sel < COLS) { sel = -1; sfx(SFX_MOVE); }
            else if (dy < 0 && sel >= COLS) { sel -= COLS; sfx(SFX_MOVE); }
            else if (dy > 0 && sel < 0) { sel = top_row * COLS; sfx(SFX_MOVE); }
            else if (dy > 0 && sel + COLS < nresults) { sel += COLS; sfx(SFX_MOVE); }
            else if (dx && sel >= 0 && sel + dx >= 0 && sel + dx < nresults && (sel + dx) / COLS == sel / COLS) { sel += dx; sfx(SFX_MOVE); }
            if (sel >= 0) {
                if (sel / COLS < top_row) top_row = sel / COLS;
                if (sel / COLS > top_row + 1) top_row = sel / COLS - 1;
            }
        }
        if (PRESSED(b) || PRESSED(back)) { sfx(SFX_BACK); clear_results(); back_to_shell(); return 0; }
        if (PRESSED(y)) { sfx(SFX_SELECT); ask_search(); if (nresults) sel = 0; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (sel < 0) { ask_search(); if (nresults) sel = 0; }
            else if (!nresults) { if (search(suggestions[sel])) sel = 0; }
            else { play(&results[sel]); ui_fade_in(); }
        }

        draw_list();
        ui_end_frame();
        if (!started) { started = 1; startlog_ready("TUBE"); }

        /* Fill in one missing thumbnail per frame, keeping the screen drawn meanwhile. */
        for (i = top_row * COLS; i < nresults && i < (top_row + 3) * COLS; i++)
            if (!results[i].tried) {
                net_set_idle_hook(list_idle);
                load_thumb(&results[i]);
                net_set_idle_hook(NULL);
                break;
            }
    }
}
