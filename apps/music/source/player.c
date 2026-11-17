/* PLAYER: the queue, shuffle and repeat, and the sound.
 *
 * Songs are decoded a little at a time (decode.c), made 48 kHz with a cubic resampler, and written
 * into the mixer's streamed voice (audio.h) about a second ahead, from the frame hook, so it keeps
 * playing on every screen, in dialogs and while the library is read again. When a song ends the
 * next one follows straight on in the same stream (no gap). The stream's clock says which song is
 * being heard: each song's place in the stream is noted as it starts going in. Skipping and seeking
 * start the stream afresh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "audio.h"
#include "config.h"
#include "music.h"
#include "platform.h"

#define OUT_RATE 48000
#define AHEAD    (OUT_RATE * 9 / 10)       /* frames kept in the stream */
#define CHUNK    2048                       /* frames decoded at a time */
#define SETTINGS "uda:/xenith/music-player.txt"

static int *order;            /* the songs as given */
static int *queue;            /* as they're played (shuffled or not) */
static int qlen;
static int shuffle_on, repeat_mode, paused, active, loaded;

/* What's going into the stream */
static struct decoder *dec;
static int feed_at;           /* queue index being decoded */
static int feed_done;         /* the queue has run out */
static unsigned written;      /* frames written since the stream was (re)opened */

/* Where each song starts in the stream, so the clock can say which one is heard */
#define SEGS 8
static struct { unsigned start; double offset, length; int at; } seg[SEGS];
static int nseg;

static char problem[160];

/* ------------------------------------------------------------ resampler */

/* A cubic (Hermite) resampler over a little window of the song's frames, stereo. */
static struct {
    double step, t;           /* input frames per output frame; position in the window */
    int n;                    /* frames in the window */
    float w[CHUNK + 8][2];
} rs;

static void rs_reset(int rate)
{
    rs.step = (double)rate / OUT_RATE;
    rs.t = 1.0;               /* one frame of history before the first */
    rs.n = 1;
    rs.w[0][0] = rs.w[0][1] = 0;
}

static inline float hermite(float ym1, float y0, float y1, float y2, float x)
{
    float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * x + c2) * x + c1) * x + y0;
}

/* Adds n frames to the window and makes as many output frames as they allow (into out, at most
 * max). The window keeps the frames still needed. */
static int rs_run(const int16_t *in, int n, int16_t *out, int max)
{
    int i, made = 0, keep;
    for (i = 0; i < n && rs.n < CHUNK + 8; i++) {
        rs.w[rs.n][0] = in[i * 2];
        rs.w[rs.n][1] = in[i * 2 + 1];
        rs.n++;
    }
    while (made < max) {
        int k = (int)rs.t, c;
        float x = (float)(rs.t - k);
        if (k + 2 >= rs.n) break;
        for (c = 0; c < 2; c++) {
            float v = hermite(rs.w[k - 1][c], rs.w[k][c], rs.w[k + 1][c], rs.w[k + 2][c], x);
            out[made * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        made++;
        rs.t += rs.step;
    }
    keep = (int)rs.t - 1;                      /* frames before that aren't needed any more */
    if (keep > 0) {
        memmove(rs.w, rs.w + keep, sizeof rs.w[0] * (size_t)(rs.n - keep));
        rs.n -= keep;
        rs.t -= keep;
    }
    return made;
}

/* --------------------------------------------------------------- queue */

static void save_settings(void)
{
    FILE *f;
    mkdir("uda:/xenith", 0777);
    if ((f = fopen(SETTINGS, "w"))) {
        fprintf(f, "# Written by Music\nshuffle=%d\nrepeat=%d\n", shuffle_on, repeat_mode);
        fclose(f);
    }
}

static void load_settings(void)
{
    char line[64];
    FILE *f;
    if (loaded) return;
    loaded = 1;
    if (!(f = fopen(SETTINGS, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "shuffle=", 8)) shuffle_on = atoi(line + 8) != 0;
        if (!strncmp(line, "repeat=", 7)) repeat_mode = atoi(line + 7) % 3;
    }
    fclose(f);
}

static uint32_t rnd = 0x2545F491;

static int random_below(int n)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return (int)(rnd % (uint32_t)n);
}

/* The queue from the list: in order, or shuffled with first at the front. */
static void make_queue(int first)
{
    int i;
    for (i = 0; i < qlen; i++) queue[i] = order[i];
    if (!shuffle_on) return;
    for (i = qlen - 1; i > 0; i--) {
        int j = random_below(i + 1), t = queue[i];
        queue[i] = queue[j];
        queue[j] = t;
    }
    for (i = 0; i < qlen; i++)
        if (queue[i] == first) { queue[i] = queue[0]; queue[0] = first; break; }
}

/* The song after queue index at, as repeat has it; -1 at the end. */
static int after(int at)
{
    if (repeat_mode == REPEAT_ONE) return at;
    if (at + 1 < qlen) return at + 1;
    return repeat_mode == REPEAT_ALL && qlen ? 0 : -1;
}

/* ------------------------------------------------------------- streaming */

static int heard_seg(void)
{
    unsigned clock = audio_stream_clock();
    int i, best = -1;
    for (i = 0; i < nseg; i++)
        if (seg[i].start <= clock) best = i;
    return best;
}

static void add_seg(int at, double offset, double length)
{
    int heard = heard_seg();
    if (nseg == SEGS) {                          /* drop the oldest that's no longer heard */
        int drop = heard > 0 ? heard - 1 : 0;
        memmove(&seg[drop], &seg[drop + 1], sizeof seg[0] * (size_t)(SEGS - drop - 1));
        nseg--;
    }
    seg[nseg].start = written;
    seg[nseg].offset = offset;
    seg[nseg].length = length;
    seg[nseg].at = at;
    nseg++;
}

/* Opens queue[at] for decoding from offset seconds in. Songs that won't open are skipped (noted in
 * problem, for a toast), going on through the queue; 0 when none of the rest will. */
static int open_at(int at, double offset)
{
    int tries;
    for (tries = 0; tries < qlen && at >= 0; tries++) {
        char why[120];
        const struct song *s = &songs[queue[at]];
        dec_close(dec);
        dec = dec_open(s->path, s->fmt, why, sizeof why);
        if (dec) {
            if (offset > 0 && !dec_seek(dec, (uint64_t)(offset * dec_rate(dec)))) offset = 0;
            rs_reset(dec_rate(dec));
            feed_at = at;
            add_seg(at, offset, dec_length(dec));
            return 1;
        }
        snprintf(problem, sizeof problem, "%s: %s", s->title, why);
        offset = 0;
        at = at + 1 < qlen ? at + 1 : repeat_mode == REPEAT_ALL ? 0 : -1;   /* never the same one again */
    }
    dec_close(dec);
    dec = NULL;
    feed_done = 1;
    return 0;
}

/* The stream from the start, playing queue[at] from offset seconds. */
static void restart(int at, double offset)
{
    audio_stream_open();
    audio_stream_pause(paused);
    written = 0;
    nseg = 0;
    feed_done = 0;
    if (!open_at(at, offset)) active = 0;
}

void player_feed(void)
{
    static int16_t in[CHUNK * 2], out[(CHUNK * 4 + 64) * 2];
    int rounds;
    if (!active) return;
    for (rounds = 0; rounds < 8 && !feed_done; rounds++) {
        int got, made, want;
        if (audio_stream_buffered() >= AHEAD || audio_stream_space() < CHUNK * 4 + 64) break;
        if (!dec) break;
        /* no more than the output buffer takes once it's made 48 kHz (an 8 kHz song makes six times as many) */
        want = (int)(CHUNK * 4 * rs.step) - 8;
        if (want > CHUNK) want = CHUNK;
        if (want < 64) want = 64;
        got = dec_read(dec, in, want);
        if (got <= 0) {                          /* the end of it: the next one follows on */
            int next = after(feed_at);
            if (rs.step != 1.0) {
                int16_t flush[4 * 2] = {0};
                made = rs_run(flush, 4, out, CHUNK * 4);   /* the last frames out of the window */
                audio_stream_write_stereo(out, made);
                written += (unsigned)made;
            }
            if (next < 0 || !open_at(next, 0)) {
                dec_close(dec);
                dec = NULL;
                feed_done = 1;
            }
            continue;
        }
        if (rs.step == 1.0) {                    /* 48 kHz already */
            audio_stream_write_stereo(in, got);
            written += (unsigned)got;
            continue;
        }
        made = rs_run(in, got, out, CHUNK * 4 + 64);
        audio_stream_write_stereo(out, made);
        written += (unsigned)made;
    }
    /* everything played: the queue is over */
    if (feed_done && !audio_stream_buffered()) {
        active = 0;
        nseg = 0;
        audio_stream_close();
    }
}

/* ------------------------------------------------------------------ API */

void player_play(const int *list, int n, int start, int shuffle)
{
    int i, first;
    load_settings();
    if (!order) order = malloc(sizeof(int) * MAX_SONGS);
    if (!queue) queue = malloc(sizeof(int) * MAX_SONGS);
    if (!order || !queue || n <= 0) return;
    if (n > MAX_SONGS) n = MAX_SONGS;
    if (start < 0 || start >= n) start = 0;
    for (i = 0; i < n; i++) order[i] = list[i];
    qlen = n;
    if (shuffle >= 0) { shuffle_on = shuffle; save_settings(); }
    first = shuffle == 1 && start == 0 ? order[random_below(n)] : order[start];
    make_queue(first);
    for (i = 0; i < qlen && queue[i] != first; i++)
        ;
    paused = 0;
    active = 1;
    restart(i < qlen ? i : 0, 0);
    player_feed();
}

int player_song(void)
{
    int s = heard_seg();
    if (!active && !nseg) return -1;
    if (s < 0) s = 0;
    return nseg && seg[s].at < qlen ? queue[seg[s].at] : -1;
}

double player_pos(void)
{
    int s = heard_seg();
    double p;
    if (s < 0) return nseg ? seg[0].offset : 0;
    p = seg[s].offset + (double)(audio_stream_clock() - seg[s].start) / OUT_RATE;
    return seg[s].length > 0 && p > seg[s].length ? seg[s].length : p;
}

double player_len(void)
{
    int s = heard_seg();
    if (s < 0) s = 0;
    return nseg ? seg[s].length : 0;
}

int player_paused(void) { return paused; }

void player_pause(int on)
{
    paused = on;
    audio_stream_pause(on);
}

void player_skip(int dir)
{
    int s = heard_seg(), at;
    if (!nseg) return;
    if (s < 0) s = 0;
    at = seg[s].at;
    if (dir < 0) {
        /* back to the start of this one, unless it's only just begun */
        if (player_pos() <= 3.0 && (at > 0 || repeat_mode == REPEAT_ALL)) at = at > 0 ? at - 1 : qlen - 1;
    } else {
        at = at + 1 < qlen ? at + 1 : repeat_mode == REPEAT_ALL || repeat_mode == REPEAT_ONE ? 0 : -1;
        if (at < 0) {                            /* past the last: it stops */
            player_stop();
            return;
        }
    }
    active = 1;
    restart(at, 0);
    player_feed();
}

void player_seek(double seconds)
{
    int s = heard_seg();
    double to, len;
    if (!nseg) return;
    if (s < 0) s = 0;
    to = player_pos() + seconds;
    len = seg[s].length;
    if (to < 0) to = 0;
    if (len > 0 && to > len - 1) { player_skip(1); return; }
    active = 1;
    restart(seg[s].at, to);
    player_feed();
}

int player_shuffle(void) { load_settings(); return shuffle_on; }
int player_repeat(void) { load_settings(); return repeat_mode; }

/* Shuffle on: the rest of the queue in a random order after the song playing; off: the list's order,
 * carrying on from it. The next song may already be in the stream, so the stream starts again here. */
void player_set_shuffle(int on)
{
    int now = player_song(), i;
    double pos = player_pos();
    shuffle_on = on;
    save_settings();
    if (!qlen || now < 0) return;
    make_queue(now);
    for (i = 0; i < qlen && queue[i] != now; i++)
        ;
    if (active) restart(i < qlen ? i : 0, pos);
}

void player_set_repeat(int r)
{
    int s = heard_seg();
    repeat_mode = r % 3;
    save_settings();
    /* what follows may already be in the stream: start again from here so it follows the new way */
    if (active && nseg && feed_at != seg[s < 0 ? 0 : s].at) restart(seg[s < 0 ? 0 : s].at, player_pos());
}

/* The songs after the one heard, in the queue's order (with repeat all, round to the start). */
int player_upcoming(int *out, int max)
{
    int s = heard_seg(), at, k, n = 0;
    if (!nseg) return 0;
    at = seg[s < 0 ? 0 : s].at;
    for (k = 1; n < max && k < qlen; k++) {
        int i = at + k;
        if (i >= qlen) {
            if (repeat_mode != REPEAT_ALL) break;
            i -= qlen;
        }
        out[n++] = queue[i];
    }
    return n;
}

const char *player_problem(void)
{
    static char said[160];
    if (!problem[0]) return NULL;
    snprintf(said, sizeof said, "%s", problem);
    problem[0] = 0;
    return said;
}

void player_stop(void)
{
    active = 0;
    nseg = 0;
    feed_done = 1;
    dec_close(dec);
    dec = NULL;
    audio_stream_close();
}
