#ifndef MUSIC_H
#define MUSIC_H

/* MUSIC: your own music (MP3, OGG, FLAC, WAV) from a music folder on the USB stick, or ~/Music on
 * a PC. library.c finds and reads it, player.c plays it through the mixer, art.c draws covers,
 * decode.c opens each kind of file, music.c is the screens. */
#include <stdint.h>

#include "gfx.h"

#define MAX_SONGS   6000
#define MAX_ALBUMS  2000
#define MAX_ARTISTS 1500
#define MAX_FOLDERS 3000

enum { FMT_MP3, FMT_OGG, FMT_FLAC, FMT_WAV };

struct song {
    char path[240];
    char title[80], artist[64], album[80], album_artist[64];
    int16_t track, disc;
    uint8_t fmt, has_art;           /* a picture inside the file (ID3's APIC, FLAC's PICTURE) */
    long size, mtime;
    int album_i, artist_i, folder;
};

struct album {
    char name[80], artist[64];
    int at, n;                      /* its songs: album_songs[at .. at + n), in track order */
    int folder;                     /* where its first song is, for a cover.jpg */
    int by_artist;                  /* grouped by its album artist (else by its folder) */
    int art_song;                   /* a song with a picture inside, or -1 */
    struct img thumb;               /* its cover at the size last drawn */
    int thumb_size, thumb_tried, thumb_none;
};

struct artist {
    char name[64];
    int at, n;                      /* its songs: artist_songs[at .. at + n), by album and track */
    int albums;
};

struct folder { char path[240]; char cover[240]; };

extern struct song *songs;
extern struct album *albums;
extern struct artist *artists;
extern struct folder *folders;
extern int nsongs, nalbums, nartists, nfolders;
extern int *album_songs, *artist_songs, *songs_by_title;
extern int *albums_by_name, *artists_by_name;

/* library.c */
int  library_alloc(void);
/* Looks for music, a bit at a time: call until it returns 1, showing *found as it goes.
 * Files it has read before (uda:/xenith/music-library.txt) aren't opened again. */
void library_scan_begin(void);
int  library_scan_step(int budget_ms, int *found);
void library_free_thumbs(void);
const char *music_where(void);      /* where to put music, in words, for the empty screen */

/* decode.c: any of the four, as 16-bit stereo at the file's own rate */
struct decoder;
struct decoder *dec_open(const char *path, int fmt, char *why, int why_len);
int  dec_rate(struct decoder *d);
int  dec_read(struct decoder *d, int16_t *lr, int frames);     /* frames read, 0 at the end */
int  dec_seek(struct decoder *d, uint64_t frame);              /* 1 when it got there */
double dec_length(struct decoder *d);                          /* seconds, 0 when not known */
void dec_close(struct decoder *d);

/* player.c */
enum { REPEAT_OFF, REPEAT_ALL, REPEAT_ONE };
void player_play(const int *list, int n, int start, int shuffle);   /* a new queue of songs */
void player_feed(void);             /* keeps the sound fed: ui_set_frame_hook */
int  player_song(void);             /* the song being heard, -1 for none */
int  player_paused(void);
void player_pause(int on);
void player_skip(int dir);          /* next, or previous (the start of this one past 3 seconds) */
void player_seek(double seconds);   /* from where it is */
double player_pos(void);
double player_len(void);
int  player_shuffle(void);
void player_set_shuffle(int on);
int  player_repeat(void);
void player_set_repeat(int r);
int  player_upcoming(int *out, int max);   /* what comes next */
const char *player_problem(void);   /* a song it couldn't play, once, for a toast */
void player_stop(void);

/* art.c */
void art_draw(int album, int x, int y, int size, float radius);   /* its cover, or a made-up one */
void art_placeholder(const char *name, int x, int y, int size, float radius);
int  art_load_one(int size);        /* makes one missing cover for the albums drawn; 1 if it did */
void art_mark_drawn(int album, int size);
void art_frame_begin(void);

/* the words of a tag, as XenithOS's letters can show them */
void text_ascii(char *out, int n, const char *utf8);

#endif
