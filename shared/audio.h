#ifndef XENITH_AUDIO_H
#define XENITH_AUDIO_H

/* XenithOS sound: every effect is synthesized at start-up (no audio files), then mixed
 * into the console's 48 kHz stereo output. Call audio_update() once per frame.
 * Games can also play their own samples (audio_play_pcm), mixed in with the rest. */
#include <stdint.h>

enum {
    SFX_MOVE,       /* moving between items */
    SFX_SELECT,
    SFX_BACK,
    SFX_ERROR,
    SFX_NOTIFY,
    SFX_KEY,        /* on-screen keyboard */
    SFX_SUCCESS,
    SFX_LAUNCH,
    SFX_BOOT,       /* the start-up chime */
    SFX_SPARK,      /* VOLT */
    SFX_CRASH,      /* VOLT */
    SFX_COUNT
};

void audio_init(void);
void audio_init_opts(int ui_sounds);  /* 0: just the mixer, for games with their own sounds */
void audio_update(void);              /* only does work when there's no mixer thread */
void audio_shutdown(void);            /* stop the mixer and leave silence (before launching a program) */
void audio_resume(void);
void audio_prepare_stall(void);       /* about to block for a long time: avoid replayed audio */
int  audio_ready(void);

struct audio_stats { int threaded, thread_failed, queue_ms, chip_ms, dropouts; };
void audio_get_stats(struct audio_stats *s);
void sfx(int id);
void sfx_gain(int id, int gain_pct, int pan_pct);   /* pan -100 (left) .. 100 (right) */
void audio_set_volume(int pct);                     /* master, 0..100 */
void audio_set_ui_sounds(int on);                   /* boot chime is played by the caller regardless */
int  audio_busy(void);                              /* 1 while any sound is still playing */
int  sfx_length_ms(int id);

/* 48 kHz 16-bit samples owned by the caller (keep them alive while they play), mono or
 * interleaved stereo. Gains are 0..256 per side. Returns a handle, or -1. */
int  audio_play_pcm(const int16_t *pcm, int frames, int mono, int gain_l, int gain_r);
void audio_voice_set(int handle, int gain_l, int gain_r);
void audio_voice_stop(int handle);
int  audio_voice_playing(int handle);

/* One streamed voice for video playback: mono 48 kHz samples written as they arrive.
 * audio_stream_clock() is how many frames of it have been heard (the player's clock). */
int  audio_stream_open(void);
void audio_stream_close(void);
void audio_stream_pause(int on);                    /* paused, it's silent and the clock stops */
int  audio_stream_space(void);                       /* frames that fit right now */
int  audio_stream_buffered(void);
void audio_stream_write(const int16_t *mono, int frames);
unsigned audio_stream_clock(void);

#endif
