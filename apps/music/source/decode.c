/* DECODE: each kind of file as 16-bit stereo at its own rate (player.c makes it 48 kHz).
 *
 * MP3 through dr_mp3, FLAC through dr_flac, Ogg Vorbis through stb_vorbis (all three public domain
 * and vendored in shared/ like stb_image), WAV here: 8, 16, 24 and 32-bit PCM and 32-bit float,
 * little-endian whatever the machine is (the Xbox is big-endian). Everything streams from the file;
 * nothing is read whole. Mono is played on both sides; more than two channels, the front two.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music.h"

/* The libraries, in this file only. Their own code is theirs: warnings about it aren't ours. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
/* libxenon's compiler says it's Linux, but its C library has no <endian.h>, which dr_flac then
 * includes. dr_flac only uses it to know the byte order, and finds that out by itself without. */
#if defined(__linux__) && !defined(XENITH_LINUX) && !defined(XENITH_SIM)
#undef __linux__
#define MUSIC_LINUX_BACK
#endif
#include "dr_flac.h"
#ifdef MUSIC_LINUX_BACK
#define __linux__ 1
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.h"
#pragma GCC diagnostic pop

struct decoder {
    int fmt, rate, channels;
    double length;
    drmp3 *mp3;
    drflac *flac;
    stb_vorbis *ogg;
    FILE *f;                         /* WAV */
    long data_at, data_len, data_pos;
    int wav_kind, bits, block;       /* 1 PCM, 3 float */
    int16_t *tmp;                    /* the file's own channels, before they're made stereo */
    int tmp_frames;
};

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[1] << 8 | p[0]); }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* ---------------------------------------------------------------- WAV */

static int wav_open(struct decoder *d, const char *path, char *why, int n)
{
    unsigned char h[40];
    long pos = 12;
    int have_fmt = 0, k;
    if (!(d->f = fopen(path, "rb"))) { snprintf(why, n, "It couldn't be read."); return 0; }
    if (fread(h, 1, 12, d->f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        snprintf(why, n, "It isn't a WAV file inside.");
        return 0;
    }
    for (k = 0; k < 64; k++) {
        long len;
        if (fseek(d->f, pos, SEEK_SET) || fread(h, 1, 8, d->f) != 8) break;
        len = (long)le32(h + 4);
        if (!memcmp(h, "fmt ", 4) && len >= 16) {
            if (fread(h + 8, 1, len > 32 ? 32 : (size_t)len, d->f) < 16) break;
            d->wav_kind = le16(h + 8);
            d->channels = le16(h + 10);
            d->rate = (int)le32(h + 12);
            d->block = le16(h + 20);
            d->bits = le16(h + 22);
            if (d->wav_kind == 0xFFFE && len >= 40) d->wav_kind = le16(h + 32);   /* extensible: its sub-format */
            have_fmt = 1;
        } else if (!memcmp(h, "data", 4)) {
            d->data_at = pos + 8;
            d->data_len = len;
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!have_fmt || !d->data_at) { snprintf(why, n, "It's missing the parts a WAV file needs."); return 0; }
    if (!((d->wav_kind == 1 && (d->bits == 8 || d->bits == 16 || d->bits == 24 || d->bits == 32)) ||
          (d->wav_kind == 3 && d->bits == 32)) || d->channels < 1 || d->channels > 8 || d->rate < 4000 || d->rate > 192000
            || d->block != d->channels * d->bits / 8) {
        snprintf(why, n, "It's a kind of WAV Music can't play (%d-bit, %d channels).", d->bits, d->channels);
        return 0;
    }
    d->length = (double)d->data_len / d->block / d->rate;
    return fseek(d->f, d->data_at, SEEK_SET) == 0;
}

static int wav_read(struct decoder *d, int16_t *out, int frames)
{
    unsigned char buf[4096];
    int done = 0, bytes = d->bits / 8;
    while (done < frames) {
        long left = (d->data_len - d->data_pos) / d->block;
        int want = frames - done, got, i, c;
        if (want > (int)sizeof buf / d->block) want = (int)sizeof buf / d->block;
        if (want > left) want = (int)left;
        if (want <= 0) break;
        got = (int)(fread(buf, 1, (size_t)want * d->block, d->f) / d->block);
        if (got <= 0) break;
        for (i = 0; i < got; i++)
            for (c = 0; c < d->channels; c++) {
                const unsigned char *p = buf + i * d->block + c * bytes;
                int v;
                if (d->wav_kind == 3) {
                    union { uint32_t u; float f; } x;
                    x.u = le32(p);
                    v = (int)(x.f * 32767.0f);
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;
                } else if (bytes == 1) v = (p[0] - 128) << 8;
                else if (bytes == 2) v = (int16_t)le16(p);
                else v = (int16_t)le16(p + bytes - 2);        /* 24 and 32-bit: the top 16 bits */
                d->tmp[(done + i) * d->channels + c] = (int16_t)v;
            }
        done += got;
        d->data_pos += (long)got * d->block;
    }
    return done;
}

/* --------------------------------------------------------- MP3's length */

/* dr_mp3 can only count an MP3's frames by reading it all, so its length comes from its first
 * frame: the Xing (or Info) and VBRI headers say how many frames there are, otherwise it's taken
 * as one bitrate throughout. */
static double mp3_length(const char *path)
{
    static const int rates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
    static const int kbps[2][15] = {{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
                                    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
    unsigned char b[4096];
    FILE *f = fopen(path, "rb");
    long start = 0, size, got;
    int i;
    double len = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (fseek(f, 0, SEEK_SET) == 0 && fread(b, 1, 10, f) == 10 && !memcmp(b, "ID3", 3))
        start = 10 + ((long)(b[6] & 127) << 21 | (b[7] & 127) << 14 | (b[8] & 127) << 7 | (b[9] & 127));
    if (fseek(f, start, SEEK_SET) == 0 && (got = (long)fread(b, 1, sizeof b, f)) > 64)
        for (i = 0; i + 4 < got; i++) {
            int ver, layer, br, sr, mono, spf, side, rate;
            if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
            ver = (b[i + 1] >> 3) & 3;              /* 3 MPEG 1, 2 MPEG 2, 0 MPEG 2.5 */
            layer = (b[i + 1] >> 1) & 3;            /* 1 is layer III */
            br = b[i + 2] >> 4;
            sr = (b[i + 2] >> 2) & 3;
            mono = (b[i + 3] >> 6) == 3;
            if (ver == 1 || layer != 1 || br == 0 || br == 15 || sr == 3) continue;
            rate = rates[ver == 3 ? 0 : ver == 2 ? 1 : 2][sr];
            spf = ver == 3 ? 1152 : 576;
            side = ver == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
            if (i + 4 + side + 12 < got && (!memcmp(b + i + 4 + side, "Xing", 4) || !memcmp(b + i + 4 + side, "Info", 4))) {
                const unsigned char *x = b + i + 4 + side;
                if (be32(x + 4) & 1) { len = (double)be32(x + 8) * spf / rate; break; }
            }
            if (i + 4 + 32 + 18 < got && !memcmp(b + i + 4 + 32, "VBRI", 4)) {
                len = (double)be32(b + i + 4 + 32 + 14) * spf / rate;
                break;
            }
            len = (double)(size - start - i) * 8 / (kbps[ver == 3 ? 0 : 1][br] * 1000.0);
            break;
        }
    fclose(f);
    return len;
}

/* --------------------------------------------------------------- common */

struct decoder *dec_open(const char *path, int fmt, char *why, int n)
{
    struct decoder *d = calloc(1, sizeof *d);
    int ok = 0;
    if (!d) { snprintf(why, n, "There isn't enough memory."); return NULL; }
    d->fmt = fmt;
    switch (fmt) {
    case FMT_MP3:
        if ((d->mp3 = malloc(sizeof *d->mp3)) && drmp3_init_file(d->mp3, path, NULL)) {
            d->rate = (int)d->mp3->sampleRate;
            d->channels = (int)d->mp3->channels;
            d->length = mp3_length(path);
            ok = 1;
        } else {
            free(d->mp3);
            d->mp3 = NULL;
            snprintf(why, n, "It isn't an MP3 Music can play.");
        }
        break;
    case FMT_FLAC:
        if ((d->flac = drflac_open_file(path, NULL))) {
            d->rate = (int)d->flac->sampleRate;
            d->channels = d->flac->channels;
            if (d->flac->totalPCMFrameCount && d->rate) d->length = (double)d->flac->totalPCMFrameCount / d->rate;
            ok = 1;
        } else snprintf(why, n, "It isn't a FLAC file Music can play.");
        break;
    case FMT_OGG: {
        int err = 0;
        if ((d->ogg = stb_vorbis_open_filename(path, &err, NULL))) {
            stb_vorbis_info info = stb_vorbis_get_info(d->ogg);
            unsigned total = stb_vorbis_stream_length_in_samples(d->ogg);
            d->rate = (int)info.sample_rate;
            d->channels = 2;                     /* stb_vorbis mixes it down (or up) itself */
            if (total && d->rate) d->length = (double)total / d->rate;
            ok = 1;
        } else snprintf(why, n, "It isn't an Ogg Vorbis file Music can play. Opus isn't played yet.");
        break;
    }
    case FMT_WAV:
        ok = wav_open(d, path, why, n);
        break;
    }
    if (ok && (d->rate < 4000 || d->rate > 192000 || d->channels < 1)) {
        snprintf(why, n, "It says it plays at %d Hz, which Music can't do.", d->rate);
        ok = 0;
    }
    if (!ok) { dec_close(d); return NULL; }
    return d;
}

int dec_rate(struct decoder *d) { return d->rate; }
double dec_length(struct decoder *d) { return d->length; }

int dec_read(struct decoder *d, int16_t *lr, int frames)
{
    int got = 0, i;
    if (d->fmt == FMT_OGG)                       /* already stereo */
        return stb_vorbis_get_samples_short_interleaved(d->ogg, 2, lr, frames * 2);
    if (d->channels == 2 && d->fmt != FMT_WAV) {
        if (d->mp3) return (int)drmp3_read_pcm_frames_s16(d->mp3, (drmp3_uint64)frames, lr);
        return (int)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, lr);
    }
    if (d->tmp_frames < frames) {
        int16_t *t = realloc(d->tmp, sizeof(int16_t) * (size_t)frames * (size_t)d->channels);
        if (!t) return 0;
        d->tmp = t;
        d->tmp_frames = frames;
    }
    if (d->mp3) got = (int)drmp3_read_pcm_frames_s16(d->mp3, (drmp3_uint64)frames, d->tmp);
    else if (d->flac) got = (int)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, d->tmp);
    else if (d->f) got = wav_read(d, d->tmp, frames);
    for (i = 0; i < got; i++) {
        const int16_t *p = d->tmp + i * d->channels;
        lr[i * 2] = p[0];
        lr[i * 2 + 1] = d->channels > 1 ? p[1] : p[0];
    }
    return got;
}

int dec_seek(struct decoder *d, uint64_t frame)
{
    switch (d->fmt) {
    case FMT_MP3: return drmp3_seek_to_pcm_frame(d->mp3, (drmp3_uint64)frame) != 0;
    case FMT_FLAC: return drflac_seek_to_pcm_frame(d->flac, (drflac_uint64)frame) != 0;
    case FMT_OGG: return stb_vorbis_seek(d->ogg, (unsigned)frame) != 0;
    case FMT_WAV: {
        long at = (long)frame * d->block;
        if (at > d->data_len) at = d->data_len - d->data_len % d->block;
        d->data_pos = at;
        return fseek(d->f, d->data_at + at, SEEK_SET) == 0;
    }
    }
    return 0;
}

void dec_close(struct decoder *d)
{
    if (!d) return;
    if (d->mp3) { drmp3_uninit(d->mp3); free(d->mp3); }
    if (d->flac) drflac_close(d->flac);
    if (d->ogg) stb_vorbis_close(d->ogg);
    if (d->f) fclose(d->f);
    free(d->tmp);
    free(d);
}
