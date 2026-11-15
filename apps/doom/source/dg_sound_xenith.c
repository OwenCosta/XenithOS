/* DOOM sound effects through the XenithOS mixer (shared/audio.c).
 *
 * Doom's effects are 8-bit unsigned mono at 11025 Hz (a few at 22050), stored as DMX
 * lumps: an 8-byte header (format 3, sample rate, length), then the samples with 16 bytes
 * of padding at each end. Each one is turned into 48 kHz 16-bit samples the first time it
 * plays and kept after that. The mixer runs on its own hardware thread, so effects start
 * the moment Doom asks for them.
 *
 * Music isn't played yet: Doom's songs need an FM synthesizer (OPL) emulator. */
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "audio.h"
#include "platform.h"

/* Settings Doom's config code binds when sound is enabled. */
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

#define CHANNELS 16
static int handle_of[CHANNELS];
static boolean sfx_prefix;

struct converted { int16_t *pcm; int frames; };

static snddevice_t devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

static boolean x_init(boolean use_sfx_prefix)
{
    int i;
    sfx_prefix = use_sfx_prefix;
    for (i = 0; i < CHANNELS; i++) handle_of[i] = -1;
    return audio_ready();
}

static void x_stop(int ch)
{
    if (ch < 0 || ch >= CHANNELS) return;
    if (handle_of[ch] > 0) audio_voice_stop(handle_of[ch]);
    handle_of[ch] = -1;
}

static void x_shutdown(void)
{
    int i;
    for (i = 0; i < CHANNELS; i++) x_stop(i);
}

static int x_lump(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link) sfx = sfx->link;
    if (sfx_prefix) M_snprintf(name, sizeof name, "ds%s", DEH_String(sfx->name));
    else M_StringCopy(name, DEH_String(sfx->name), sizeof name);
    return W_CheckNumForName(name);
}

static struct converted *convert(sfxinfo_t *sfx)
{
    struct converted *c;
    const unsigned char *d;
    int lump, len, rate, n, out, i;
    uint32_t pos, step;

    if (sfx->driver_data) return sfx->driver_data;
    lump = sfx->lumpnum >= 0 ? sfx->lumpnum : x_lump(sfx);
    if (lump < 0) return NULL;
    len = W_LumpLength(lump);
    d = W_CacheLumpNum(lump, PU_STATIC);
    if (len < 8 || d[0] != 3 || d[1] != 0) { W_ReleaseLumpNum(lump); return NULL; }
    rate = d[2] | (d[3] << 8);
    n = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24);
    if (n > len - 8 || n <= 48 || rate < 4000 || rate > 48000) { W_ReleaseLumpNum(lump); return NULL; }
    d += 8 + 16;
    n -= 32;

    out = (int)((long long)n * PLAT_AUDIO_RATE / rate);
    c = malloc(sizeof *c + sizeof(int16_t) * (size_t)out);
    if (!c) { W_ReleaseLumpNum(lump); return NULL; }
    c->pcm = (int16_t *)(c + 1);
    c->frames = out;
    step = (uint32_t)(((uint64_t)rate << 16) / PLAT_AUDIO_RATE);
    for (i = 0, pos = 0; i < out; i++, pos += step) {          /* linear interpolation */
        int k = (int)(pos >> 16), f = (int)(pos & 0xffff);
        int a = d[k], b = k + 1 < n ? d[k + 1] : a;
        int v = a + (((b - a) * f) >> 16);
        c->pcm[i] = (int16_t)((v - 128) * 256);
    }
    W_ReleaseLumpNum(lump);
    sfx->driver_data = c;
    return c;
}

/* Doom's volume is 0..127 and separation 0..254 (128 is the middle), as in Chocolate Doom.
 * Scaled down a little so several effects at once don't clip. */
static void gains(int vol, int sep, int *gl, int *gr)
{
    int l = ((254 - sep) * vol) / 127, r = (sep * vol) / 127;
    if (l < 0) l = 0;
    if (l > 255) l = 255;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    *gl = l * 200 / 255;
    *gr = r * 200 / 255;
}

static int x_start(sfxinfo_t *sfx, int ch, int vol, int sep)
{
    struct converted *c;
    int gl, gr;
    if (ch < 0 || ch >= CHANNELS) return -1;
    x_stop(ch);
    c = convert(sfx);
    if (!c) return -1;
    gains(vol, sep, &gl, &gr);
    handle_of[ch] = audio_play_pcm(c->pcm, c->frames, 1, gl, gr);
    return handle_of[ch] > 0 ? ch : -1;
}

static void x_params(int ch, int vol, int sep)
{
    int gl, gr;
    if (ch < 0 || ch >= CHANNELS || handle_of[ch] <= 0) return;
    gains(vol, sep, &gl, &gr);
    audio_voice_set(handle_of[ch], gl, gr);
}

static boolean x_playing(int ch)
{
    if (ch < 0 || ch >= CHANNELS || handle_of[ch] <= 0) return false;
    return audio_voice_playing(handle_of[ch]) ? true : false;
}

static void x_update(void)
{
}

static void x_cache(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;          /* converted the first time each one plays instead */
    (void)num_sounds;
}

sound_module_t DG_sound_module = {
    devices, sizeof devices / sizeof *devices,
    x_init, x_shutdown, x_lump, x_update, x_params, x_start, x_stop, x_playing, x_cache,
};

/* ------------------------------------------------------- music (silent) */

static boolean m_init(void) { return true; }
static void m_shutdown(void) { }
static void m_volume(int volume) { (void)volume; }
static void m_pause(void) { }
static void m_resume(void) { }
static void *m_register(void *data, int len) { (void)data; (void)len; return NULL; }
static void m_unregister(void *handle) { (void)handle; }
static void m_play(void *handle, boolean looping) { (void)handle; (void)looping; }
static void m_stop(void) { }
static boolean m_playing(void) { return false; }
static void m_poll(void) { }

music_module_t DG_music_module = {
    NULL, 0,
    m_init, m_shutdown, m_volume, m_pause, m_resume, m_register, m_unregister, m_play, m_stop, m_playing, m_poll,
};
