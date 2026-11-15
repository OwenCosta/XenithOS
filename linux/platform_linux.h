/* Shared by the PC and Raspberry Pi platform files: platform_sdl.c (screen, sound, input,
 * starting programs) and apps_linux.c (Flathub apps and Steam games). */
#ifndef XENITH_PLATFORM_LINUX_H
#define XENITH_PLATFORM_LINUX_H

#include <sys/types.h>

const char *linux_data_dir(void);              /* the folder that holds "uda:" */

/* A program by name: $XENITHOS_<NAME> if set (tests point it at a stand-in), else the usual
 * folders. NULL if it isn't there. */
char *linux_find_program(const char *name, char *out, int n);

/* Start argv[0] (a full path) with envp (NULL: ours), its output into log (NULL: nowhere) and
 * nothing to read, in a process group of its own when new_group is set. Only exec-safe calls
 * happen after fork(), since XenithOS's sound thread is running. */
pid_t linux_spawn(char *const argv[], char *const envp[], const char *log, int new_group);

/* What an installed app runs: argv[] points into buf. */
struct linux_app {
    char name[40];
    char buf[640];
    char *argv[20];
    int steam;                                 /* it runs in Steam (Steam itself, or a game) */
    int steam_appid;                           /* a Steam game: Steam closes again after it */
};

/* Reads "uda:/apps/<id>.app". 0 with why[] set if it can't be run. */
int linux_app_read(const char *path, struct linux_app *a, char *why, int why_len);
/* Steam in Big Picture, at a steam:// address. 0 if Steam isn't installed. */
int linux_steam_command(const char *url, struct linux_app *a);
/* Whether Steam says that game is running: 1 yes, 0 no, -1 it can't be told. */
int linux_steam_game_running(int appid);
/* What a program's log says went wrong: its last "error:" line (returns 2), or else its last
 * line (1); 0 when it's empty. In plain ASCII, for XenithOS's font. */
int  linux_log_reason(const char *log, char *out, int n);

#endif
