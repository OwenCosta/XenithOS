#ifndef XENITH_AUDIO_H
#define XENITH_AUDIO_H

/* XenithOS sound: every effect is synthesized at start-up (no audio files), then mixed
 * into the console's 48 kHz stereo output. Call audio_update() once per frame. */

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
void audio_update(void);
void sfx(int id);
void sfx_gain(int id, int gain_pct, int pan_pct);   /* pan -100 (left) .. 100 (right) */
void audio_set_volume(int pct);                     /* master, 0..100 */
void audio_set_ui_sounds(int on);                   /* boot chime is played by the caller regardless */
int  audio_busy(void);                              /* 1 while any sound is still playing */
int  sfx_length_ms(int id);

#endif
