/* Synthesized XenithOS sounds and a small mixer. See audio.h. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "platform.h"

#define RATE    PLAT_AUDIO_RATE
#define BLOCK   PLAT_AUDIO_BLOCK
#define TARGET_THREAD (BLOCK * 3)    /* ~32 ms ahead when the mixer has its own thread */
#define TARGET_MAIN   (BLOCK * 6)    /* ~64 ms ahead when it shares the main thread */
#define VOICES  16
#define TAB     4096
#define PI      3.14159265f

struct sound { const int16_t *pcm; int frames; int mono; };   /* stereo is interleaved */
struct voice { const struct sound *s; int pos, gl, gr; int id, handle; struct sound ext; };

static struct sound sounds[SFX_COUNT];
static float sine[TAB];
static uint32_t rng = 0x12345678;

/* Shared between the main thread and the mixer thread. */
static volatile int master = 256, ui_on = 1, ready, threaded, inited;
static volatile int voices_active, queue_frames, hw_frames, underruns;

/* The mixer thread counts its steps in beat. If the count stops moving, the main thread
 * takes the mixing back (give_up), so a stuck thread can't leave the console silent. */
static volatile unsigned beat;
static volatile int give_up, thread_failed;
static unsigned seen_beat;
static uint64_t seen_beat_at;

/* Requests from the main thread: it writes cmd_head, the mixer writes cmd_tail. */
#define NCMD 64
enum { CMD_SFX, CMD_PCM, CMD_SET, CMD_STOP };
struct cmd { int type, id, gl, gr, handle, frames, mono; const int16_t *pcm; };
static struct cmd cmdq[NCMD];
static volatile unsigned cmd_head, cmd_tail;

/* A streamed voice (video and music playback): a ring of stereo 48 kHz frames, two samples each
 * (mono is written to both). The main thread writes s_head, the mixer reads from s_tail;
 * s_played counts what the mixer has taken. All three count frames. */
#define SRING (RATE * 2)
static int16_t *sring;
static volatile unsigned s_head, s_tail, s_played;
static volatile int s_open, s_paused;

/* Only the mixer touches these. */
static struct voice voices[VOICES];
static uint64_t est_time;
static int est_frames;
static long long submitted;

/* ------------------------------------------------------------- synthesis */

static float osc(float phase)        /* phase in cycles */
{
    float p = phase - floorf(phase);
    return sine[(int)(p * TAB) & (TAB - 1)];
}

static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)((int32_t)rng) / 2147483648.0f;
}

/* A float stereo buffer that tones get added into. */
struct buf { float *l, *r; int n; };

static struct buf buf_new(float seconds)
{
    struct buf b;
    b.n = (int)(seconds * RATE);
    b.l = calloc(b.n, sizeof(float));
    b.r = calloc(b.n, sizeof(float));
    return b;
}

static void add(struct buf *b, int i, float v, float pan)   /* pan -1..1 */
{
    if (i < 0 || i >= b->n) return;
    b->l[i] += v * (pan > 0 ? 1 - pan : 1);
    b->r[i] += v * (pan < 0 ? 1 + pan : 1);
}

/* Sine with optional pitch glide, a short attack and an exponential decay. */
static void tone(struct buf *b, float start, float len, float f0, float f1, float attack, float decay,
                 float gain, float harm2, float pan)
{
    int i0 = (int)(start * RATE), n = (int)(len * RATE), i;
    float phase = 0, phase2 = 0;
    for (i = 0; i < n; i++) {
        float t = (float)i / RATE, f = f0 + (f1 - f0) * (t / len);
        float env = (t < attack ? t / attack : 1.0f) * expf(-t / decay);
        float fade = i > n - 64 ? (n - i) / 64.0f : 1.0f;
        phase += f / RATE;
        phase2 += 2 * f / RATE;
        add(b, i0 + i, (osc(phase) + harm2 * osc(phase2)) * env * gain * fade, pan);
    }
}

/* FM bell: a bright attack that mellows as the modulation fades. */
static void bell(struct buf *b, float start, float f, float decay, float gain, float pan)
{
    int i0 = (int)(start * RATE), n = (int)(decay * 6 * RATE), i;
    float pc = 0, pm = 0;
    for (i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float index = 1.6f * expf(-t / 0.12f);
        float env = (t < 0.002f ? t / 0.002f : 1.0f) * expf(-t / decay);
        pm += f * 3.5f / RATE;
        pc += f / RATE;
        add(b, i0 + i, osc(pc + index * osc(pm) / (2 * PI)) * env * gain, pan);
    }
}

/* Swept, filtered noise for the launch whoosh and the crash. */
static void whoosh(struct buf *b, float start, float len, float c0, float c1, float rise, float gain)
{
    int i0 = (int)(start * RATE), n = (int)(len * RATE), i;
    float lp = 0;
    for (i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float cut = c0 * powf(c1 / c0, t / len);
        float k = 1 - expf(-2 * PI * cut / RATE);
        float env = t < rise ? t / rise : expf(-(t - rise) / ((len - rise) * 0.35f));
        lp += (noise() - lp) * k;
        add(b, i0 + i, lp * env * gain, 0);
    }
}

/* Ping-pong echo, for the boot chime. */
static void echo(struct buf *b, float delay, float fb, int repeats)
{
    int d = (int)(delay * RATE), r, i;
    for (r = 1; r <= repeats; r++) {
        float g = powf(fb, (float)r);
        for (i = b->n - 1; i >= d * r; i--) {
            float src = 0.5f * (b->l[i - d * r] + b->r[i - d * r]);
            if (r & 1) b->r[i] += src * g;
            else b->l[i] += src * g;
        }
    }
}

static void finish(int id, struct buf *b, float peak)
{
    float mx = 1e-6f;
    int i;
    for (i = 0; i < b->n; i++) {
        if (fabsf(b->l[i]) > mx) mx = fabsf(b->l[i]);
        if (fabsf(b->r[i]) > mx) mx = fabsf(b->r[i]);
    }
    if (peak > 0 && mx > peak) {
        float k = peak / mx;
        for (i = 0; i < b->n; i++) { b->l[i] *= k; b->r[i] *= k; }
    }
    int16_t *pcm = malloc(sizeof(int16_t) * 2 * b->n);
    if (pcm)
        for (i = 0; i < b->n; i++) {
            float l = b->l[i], r = b->r[i];
            if (l > 1) l = 1;
            if (l < -1) l = -1;
            if (r > 1) r = 1;
            if (r < -1) r = -1;
            pcm[i * 2] = (int16_t)(l * 32767);
            pcm[i * 2 + 1] = (int16_t)(r * 32767);
        }
    sounds[id].frames = pcm ? b->n : 0;
    sounds[id].pcm = pcm;
    free(b->l);
    free(b->r);
}

static void make_sounds(void)
{
    struct buf b;

    /* A soft tick with a little downward glide. */
    b = buf_new(0.05f);
    tone(&b, 0, 0.05f, 1350, 1050, 0.0015f, 0.009f, 0.30f, 0.25f, 0);
    finish(SFX_MOVE, &b, 0);

    /* Two notes going up. */
    b = buf_new(0.30f);
    tone(&b, 0.000f, 0.12f, 880.0f, 880.0f, 0.002f, 0.040f, 0.30f, 0.30f, 0);
    tone(&b, 0.035f, 0.26f, 1318.5f, 1318.5f, 0.002f, 0.070f, 0.30f, 0.30f, 0);
    finish(SFX_SELECT, &b, 0);

    /* Two notes going down. */
    b = buf_new(0.28f);
    tone(&b, 0.000f, 0.12f, 1046.5f, 1046.5f, 0.002f, 0.040f, 0.24f, 0.25f, 0);
    tone(&b, 0.035f, 0.24f, 784.0f, 784.0f, 0.002f, 0.065f, 0.24f, 0.25f, 0);
    finish(SFX_BACK, &b, 0);

    /* Two low bumps. */
    b = buf_new(0.30f);
    tone(&b, 0.00f, 0.10f, 210, 185, 0.003f, 0.050f, 0.36f, 0.35f, 0);
    tone(&b, 0.13f, 0.12f, 190, 165, 0.003f, 0.060f, 0.36f, 0.35f, 0);
    finish(SFX_ERROR, &b, 0);

    /* Three bells. */
    b = buf_new(1.6f);
    bell(&b, 0.00f, 1318.5f, 0.32f, 0.22f, -0.3f);
    bell(&b, 0.08f, 1661.2f, 0.32f, 0.20f, 0.0f);
    bell(&b, 0.16f, 1975.5f, 0.40f, 0.20f, 0.3f);
    finish(SFX_NOTIFY, &b, 0.75f);

    b = buf_new(0.025f);
    tone(&b, 0, 0.025f, 2400, 2200, 0.001f, 0.004f, 0.16f, 0, 0);
    finish(SFX_KEY, &b, 0);

    /* The guide: two soft notes gliding up as it slides in, and down as it goes. */
    b = buf_new(0.34f);
    tone(&b, 0.000f, 0.16f, 587.3f, 784.0f, 0.004f, 0.060f, 0.24f, 0.18f, -0.25f);
    tone(&b, 0.060f, 0.26f, 880.0f, 1174.7f, 0.004f, 0.110f, 0.22f, 0.16f, 0.25f);
    finish(SFX_GUIDE_OPEN, &b, 0);

    b = buf_new(0.32f);
    tone(&b, 0.000f, 0.14f, 1174.7f, 880.0f, 0.004f, 0.055f, 0.20f, 0.16f, 0.25f);
    tone(&b, 0.055f, 0.24f, 784.0f, 587.3f, 0.004f, 0.100f, 0.22f, 0.18f, -0.25f);
    finish(SFX_GUIDE_CLOSE, &b, 0);

    /* A rising arpeggio. */
    b = buf_new(1.8f);
    bell(&b, 0.00f, 1046.5f, 0.35f, 0.20f, -0.2f);
    bell(&b, 0.06f, 1318.5f, 0.35f, 0.20f, -0.1f);
    bell(&b, 0.12f, 1568.0f, 0.35f, 0.20f, 0.1f);
    bell(&b, 0.18f, 2093.0f, 0.45f, 0.18f, 0.2f);
    finish(SFX_SUCCESS, &b, 0.75f);

    /* An opening whoosh with a rising tone under it. */
    b = buf_new(0.55f);
    whoosh(&b, 0, 0.55f, 400, 6000, 0.30f, 0.55f);
    tone(&b, 0.05f, 0.45f, 220, 880, 0.15f, 0.30f, 0.12f, 0.2f, 0);
    finish(SFX_LAUNCH, &b, 0.6f);

    /* The boot chime: a low thump, a rolled Dmaj9 chord, three bells, and a ping-pong echo. */
    {
        static const float chord[5] = {146.83f, 220.00f, 329.63f, 369.99f, 554.37f};
        static const float pans[5] = {-0.15f, 0.15f, -0.3f, 0.3f, 0.0f};
        int v, d;
        b = buf_new(3.8f);
        tone(&b, 0.0f, 0.9f, 110, 55, 0.004f, 0.35f, 0.45f, 0.0f, 0);
        for (v = 0; v < 5; v++)
            for (d = -1; d <= 1; d += 2)        /* two slightly detuned voices per note */
                tone(&b, 0.07f * v, 3.4f - 0.07f * v, chord[v] * (1 + d * 0.0012f), chord[v] * (1 + d * 0.0012f),
                     0.12f, 1.6f, 0.075f, 0.15f, pans[v] + d * 0.1f);
        bell(&b, 0.32f, 880.0f, 0.70f, 0.10f, -0.5f);
        bell(&b, 0.44f, 1318.5f, 0.70f, 0.09f, 0.5f);
        bell(&b, 0.56f, 1480.0f, 0.80f, 0.08f, 0.0f);
        echo(&b, 0.21f, 0.28f, 3);
        finish(SFX_BOOT, &b, 0.8f);
    }

    /* VOLT: a quick rising blip with a tiny bell on top. */
    b = buf_new(0.25f);
    tone(&b, 0, 0.07f, 1200, 2000, 0.002f, 0.030f, 0.24f, 0.2f, 0);
    bell(&b, 0.03f, 2637.0f, 0.06f, 0.10f, 0);
    finish(SFX_SPARK, &b, 0);

    /* VOLT: a crunchy crash. */
    b = buf_new(0.6f);
    whoosh(&b, 0, 0.5f, 3000, 200, 0.004f, 0.9f);
    tone(&b, 0, 0.45f, 120, 50, 0.003f, 0.15f, 0.45f, 0.3f, 0);
    finish(SFX_CRASH, &b, 0.8f);
}

/* ----------------------------------------------------------------- mixer */

/* The sound chip plays a ring of PLAT_AUDIO_RING_BLOCKS blocks. If nobody refills it in
 * time it can replay old blocks, which sounds like fast repeating pops. So the mixer runs on
 * its own hardware thread when the platform has one, refilling about every millisecond.
 * It also keeps its own clock-based count of what's queued and never trusts the chip's
 * number when it's lower, so it can't write faster than the chip plays. */

static void run_cmd(const struct cmd *c)
{
    int i, slot = -1, oldest = 0;
    if (c->type == CMD_SET || c->type == CMD_STOP) {
        for (i = 0; i < VOICES; i++)
            if (voices[i].s && voices[i].handle == c->handle) {
                if (c->type == CMD_STOP) voices[i].s = NULL;
                else { voices[i].gl = c->gl; voices[i].gr = c->gr; }
            }
        return;
    }
    for (i = 0; i < VOICES; i++) {
        if (!voices[i].s && slot < 0) slot = i;
        if (voices[i].s && c->type == CMD_SFX && c->id == SFX_MOVE && voices[i].id == SFX_MOVE) { slot = i; break; }   /* the tick restarts */
        if (voices[i].s && voices[i].pos > voices[oldest].pos) oldest = i;
    }
    if (slot < 0) slot = oldest;
    if (c->type == CMD_PCM) {
        voices[slot].ext.pcm = c->pcm;
        voices[slot].ext.frames = c->frames;
        voices[slot].ext.mono = c->mono;
        voices[slot].s = &voices[slot].ext;
        voices[slot].id = -1;
    } else {
        voices[slot].s = &sounds[c->id];
        voices[slot].id = c->id;
    }
    voices[slot].handle = c->handle;
    voices[slot].pos = 0;
    voices[slot].gl = c->gl;
    voices[slot].gr = c->gr;
}

static void mixer_step(void)
{
    static int16_t out[BLOCK * 2];
    static int32_t acc[BLOCK * 2];
    uint64_t now = plat_time_us();
    int target = threaded ? TARGET_THREAD : TARGET_MAIN, guard = 8, hw, q, v, i, active = 0;

    while (cmd_tail != cmd_head) {
        __sync_synchronize();
        run_cmd(&cmdq[cmd_tail % NCMD]);
        __sync_synchronize();
        cmd_tail = cmd_tail + 1;
    }

    if (est_time) {
        long long played = (long long)(now - est_time) * RATE / 1000000;
        est_frames = played >= est_frames ? 0 : est_frames - (int)played;
    }
    est_time = now;
    hw = plat_audio_queued();
    hw_frames = hw;
    if (hw < 0 || hw > (PLAT_AUDIO_RING_BLOCKS - 4) * BLOCK) hw = 0;   /* not believable: go by the clock */
    q = hw > est_frames ? hw : est_frames;
    if (q == 0 && submitted) underruns = underruns + 1;

    while (q < target && guard--) {
        memset(acc, 0, sizeof acc);
        if (s_open && !s_paused && sring) {
            unsigned head = s_head, tail = s_tail, n = head - tail, k;
            __sync_synchronize();
            if (n > BLOCK) n = BLOCK;
            for (k = 0; k < n; k++) {
                unsigned at = (tail + k) % SRING;
                acc[k * 2] += sring[at * 2];
                acc[k * 2 + 1] += sring[at * 2 + 1];
            }
            __sync_synchronize();
            s_tail = tail + n;
            s_played = s_played + n;
        }
        for (v = 0; v < VOICES; v++) {
            struct voice *vo = &voices[v];
            int n;
            if (!vo->s) continue;
            n = vo->s->frames - vo->pos;
            if (n > BLOCK) n = BLOCK;
            if (vo->s->mono) {
                const int16_t *p = vo->s->pcm + vo->pos;
                for (i = 0; i < n; i++) {
                    acc[i * 2] += (p[i] * vo->gl) >> 8;
                    acc[i * 2 + 1] += (p[i] * vo->gr) >> 8;
                }
            } else {
                for (i = 0; i < n; i++) {
                    acc[i * 2] += (vo->s->pcm[(vo->pos + i) * 2] * vo->gl) >> 8;
                    acc[i * 2 + 1] += (vo->s->pcm[(vo->pos + i) * 2 + 1] * vo->gr) >> 8;
                }
            }
            vo->pos += n;
            if (vo->pos >= vo->s->frames) vo->s = NULL;
        }
        for (i = 0; i < BLOCK * 2; i++) {
            int32_t s = (acc[i] * master) >> 8;
            out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        plat_audio_submit(out, BLOCK);
        submitted += BLOCK;
        q += BLOCK;
        est_frames += BLOCK;
    }
    for (v = 0; v < VOICES; v++) if (voices[v].s) active++;
    voices_active = active;
    queue_frames = q;
}

/* Overwrite the whole ring with silence so nothing old can be replayed. */
static void fill_silence(void)
{
    static const int16_t zero[BLOCK * 2];
    int i;
    for (i = 0; i < PLAT_AUDIO_RING_BLOCKS; i++) plat_audio_submit(zero, BLOCK);
    est_frames = 0;
    est_time = 0;
}

void audio_shutdown(void)
{
    if (!ready) return;
    ready = 0;                                   /* sfx() is ignored from here on */
    if (threaded) {
        plat_thread_stop();
        threaded = 0;
    }
    memset(voices, 0, sizeof voices);
    cmd_tail = cmd_head;
    voices_active = 0;
    fill_silence();
}

/* What the spare hardware thread runs. */
static void thread_mix(void)
{
    if (give_up) return;
    beat = beat + 1;
    mixer_step();
}

static void start_mixer(void)
{
    ready = 1;
    threaded = !thread_failed;
    give_up = 0;
    seen_beat_at = 0;
    if (!threaded || !plat_thread_start(thread_mix)) {
        threaded = 0;
        mixer_step();
    }
}

void audio_resume(void)
{
    if (ready || !inited) return;
    start_mixer();
}

void audio_prepare_stall(void)
{
    /* Only matters without the mixer thread: about to block for a while, so stop
     * the sounds and leave nothing but silence in the ring. */
    if (!ready || threaded) return;
    memset(voices, 0, sizeof voices);
    cmd_tail = cmd_head;
    voices_active = 0;
    fill_silence();
}

void audio_init_opts(int ui_sounds)
{
    int i;
    if (inited) return;
    if (ui_sounds) {
        for (i = 0; i < TAB; i++) sine[i] = sinf(2 * PI * i / TAB);
        make_sounds();
    }
    plat_audio_init();
    plat_set_launch_hooks(audio_shutdown, audio_resume);
    inited = 1;
    start_mixer();
}

void audio_init(void)
{
    audio_init_opts(1);
}

void audio_update(void)
{
    if (!ready) return;
    if (threaded) {
        uint64_t now = plat_time_us();
        unsigned b = beat;
        if (b != seen_beat || !seen_beat_at) {
            seen_beat = b;
            seen_beat_at = now;
            return;
        }
        if (now - seen_beat_at < 250000) return;
        /* The mixer thread hasn't run for a quarter of a second: mix here from now on. */
        give_up = 1;
        __sync_synchronize();
        threaded = 0;
        thread_failed = 1;
    }
    mixer_step();
}

/* --------------------------------------------------- sounds from games */

/* Which handles are still sounding, worked out on the main thread from when each started
 * and how long it is, so a sound counts as playing from the moment it's asked for. */
#define NPLAY 32
static struct { int handle; uint64_t end; } playing[NPLAY];
static int next_handle = 1;

static int push_cmd(const struct cmd *c)
{
    if (!ready || cmd_head - cmd_tail >= NCMD) return 0;
    cmdq[cmd_head % NCMD] = *c;
    __sync_synchronize();
    cmd_head = cmd_head + 1;
    return 1;
}

int audio_play_pcm(const int16_t *pcm, int frames, int mono, int gain_l, int gain_r)
{
    struct cmd c;
    int i, slot = 0;
    if (!pcm || frames <= 0) return -1;
    memset(&c, 0, sizeof c);
    c.type = CMD_PCM;
    c.pcm = pcm;
    c.frames = frames;
    c.mono = mono;
    c.gl = gain_l;
    c.gr = gain_r;
    c.handle = next_handle++;
    if (next_handle > 1000000000) next_handle = 1;
    if (!push_cmd(&c)) return -1;
    for (i = 0; i < NPLAY; i++)
        if (playing[i].end < playing[slot].end) slot = i;
    playing[slot].handle = c.handle;
    playing[slot].end = plat_time_us() + (uint64_t)frames * 1000000 / RATE + 20000;
    return c.handle;
}

void audio_voice_set(int handle, int gain_l, int gain_r)
{
    struct cmd c;
    if (handle <= 0) return;
    memset(&c, 0, sizeof c);
    c.type = CMD_SET;
    c.handle = handle;
    c.gl = gain_l;
    c.gr = gain_r;
    push_cmd(&c);
}

void audio_voice_stop(int handle)
{
    struct cmd c;
    int i;
    if (handle <= 0) return;
    memset(&c, 0, sizeof c);
    c.type = CMD_STOP;
    c.handle = handle;
    push_cmd(&c);
    for (i = 0; i < NPLAY; i++)
        if (playing[i].handle == handle) playing[i].end = 0;
}

int audio_voice_playing(int handle)
{
    int i;
    uint64_t now = plat_time_us();
    for (i = 0; i < NPLAY; i++)
        if (playing[i].handle == handle) return playing[i].end > now;
    return 0;
}

int audio_ready(void)
{
    return ready;
}

/* ------------------------------------------------------- streamed voice */

int audio_stream_open(void)
{
    if (!sring) sring = malloc(sizeof(int16_t) * 2 * SRING);
    if (!sring) return 0;
    s_open = 0;
    __sync_synchronize();
    s_tail = s_head;
    s_played = 0;
    __sync_synchronize();
    s_open = 1;
    return 1;
}

void audio_stream_close(void)
{
    s_open = 0;
    s_paused = 0;
    __sync_synchronize();
}

void audio_stream_pause(int on)
{
    s_paused = on;
    __sync_synchronize();
}

int audio_stream_space(void)
{
    return SRING - 1 - (int)(s_head - s_tail);
}

int audio_stream_buffered(void)
{
    return (int)(s_head - s_tail);
}

void audio_stream_write(const int16_t *mono, int frames)
{
    unsigned head = s_head;
    int i, room = audio_stream_space();
    if (!sring || frames <= 0) return;
    if (frames > room) frames = room;
    for (i = 0; i < frames; i++) {
        unsigned at = (head + i) % SRING;
        sring[at * 2] = sring[at * 2 + 1] = mono[i];
    }
    __sync_synchronize();
    s_head = head + frames;
}

void audio_stream_write_stereo(const int16_t *lr, int frames)
{
    unsigned head = s_head;
    int i, room = audio_stream_space();
    if (!sring || frames <= 0) return;
    if (frames > room) frames = room;
    for (i = 0; i < frames; i++) {
        unsigned at = (head + i) % SRING;
        sring[at * 2] = lr[i * 2];
        sring[at * 2 + 1] = lr[i * 2 + 1];
    }
    __sync_synchronize();
    s_head = head + frames;
}

/* Frames of the stream actually heard so far: what the mixer took, less what is still
 * queued on its way to the speakers. A video player uses this as its clock. */
unsigned audio_stream_clock(void)
{
    unsigned played = s_played, queued = (unsigned)queue_frames;
    return played > queued ? played - queued : 0;
}

void audio_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    master = pct * 256 / 100;
}

void audio_set_ui_sounds(int on)
{
    ui_on = on;
}

int sfx_length_ms(int id)
{
    if (id < 0 || id >= SFX_COUNT) return 0;
    return sounds[id].frames * 1000 / RATE;
}

void sfx_gain(int id, int gain_pct, int pan_pct)
{
    struct cmd c;
    if (!ready || id < 0 || id >= SFX_COUNT || !sounds[id].pcm) return;
    if (!ui_on && id != SFX_BOOT) return;
    if (pan_pct < -100) pan_pct = -100;
    if (pan_pct > 100) pan_pct = 100;
    memset(&c, 0, sizeof c);
    c.type = CMD_SFX;
    c.id = id;
    c.gl = gain_pct * 256 / 100 * (pan_pct > 0 ? 100 - pan_pct : 100) / 100;
    c.gr = gain_pct * 256 / 100 * (pan_pct < 0 ? 100 + pan_pct : 100) / 100;
    push_cmd(&c);                                /* dropped if the mixer is far behind */
}

void sfx(int id)
{
    sfx_gain(id, 100, 0);
}

int audio_busy(void)
{
    return ready && (cmd_head != cmd_tail || voices_active > 0);
}

void audio_get_stats(struct audio_stats *s)
{
    s->threaded = threaded;
    s->thread_failed = thread_failed;
    s->queue_ms = queue_frames * 1000 / RATE;
    s->chip_ms = hw_frames * 1000 / RATE;
    s->dropouts = underruns;
}
