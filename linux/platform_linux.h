/* Shared by the PC and Raspberry Pi platform files: platform_sdl.c (screen, sound, input,
 * starting programs) and apps_linux.c (Flathub apps and Steam games). */
#ifndef XENITH_PLATFORM_LINUX_H
#define XENITH_PLATFORM_LINUX_H

#include <stdint.h>
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
    int pointer;                               /* starts with the controller as the pointer
                                                  (Prism Launcher, or "pointer=1" in the .app) */
    char game_title[24];                       /* a launcher's game: while a window with a
                                                  title starting like this is in front, the
                                                  controller plays it as a keyboard and mouse */
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

/* Console mode's window manager (kiosk.c): XenithOS runs on its own X server and manages the
 * windows apps open over it. start() takes XenithOS's own window, after it's on screen; 0 if
 * another window manager is running (a desktop). poll() handles what's waiting. */
int  kiosk_wm_start(unsigned long self_window);
int  kiosk_wm_running(void);
void kiosk_wm_poll(void);
int  kiosk_wm_windows(void);                 /* apps' windows on screen now */
void kiosk_wm_top_title(char *out, int n);   /* the title of the one in front, "" if none */
void kiosk_wm_show_self(void);               /* XenithOS's window on top with the keyboard again */
void kiosk_wm_hold(int on);                  /* 1: XenithOS's window stays in front of the app's */
void kiosk_wm_keep_focus(void);              /* the keyboard back to the app in front, if it's lost */
int  kiosk_wm_capture(uint32_t *out, int w, int h);   /* the screen, scaled; 0 if it can't */
/* The pointer, through XTEST: 0 from can_point() when libXtst isn't there. Buttons are X's
 * (1 left, 2 middle, 3 right, 4/5 scroll up/down, 6/7 left/right); keys are X keysyms. */
int  kiosk_wm_can_point(void);
void kiosk_wm_pointer_move(int dx, int dy);
void kiosk_wm_pointer_button(int button, int down);
void kiosk_wm_key(unsigned long keysym, int down);
int  kiosk_wm_pointer_hidden(void);          /* 1 hidden (a game playing), 0 shown (a menu), -1 can't tell */

#endif
