#ifndef XENITH_HANDOFF_H
#define XENITH_HANDOFF_H

/* Word passed from one XenithOS program to the next one it starts.
 *
 * On the Xbox a program replaces the one before it, so what's worth keeping (the screen is
 * already set up, which app we're coming back from) has to be handed over. plat_launch()
 * passes it as the next program's arguments: HANDOFF_MARK, then key=value pairs. The next
 * program calls handoff_args() first thing in main. A program started any other way (XeLL,
 * a PC session) gets none and starts from scratch.
 *
 * Keys in use:
 *   from=<app id>          the app XenithOS is coming back from ("doom", "volt", "tube")
 *   video=WxH:overscan:hdmi  the screen mode the last program left running (Xbox) */
#define HANDOFF_MARK "xenithos-handoff"
#define HANDOFF_MAX 8

void handoff_args(int argc, char **argv);              /* first thing in main */
int  handoff_received(void);                           /* started by another XenithOS program */
/* The path this program was started from (argv[0]: "uda:/apps/volt.elf32"), "" if none was
 * given (XeLL starting xenon.elf). For crash files on the Xbox, which has no other way to tell. */
const char *handoff_self(void);
const char *handoff_get(const char *key);              /* what it passed, or NULL */
void handoff_set(const char *key, const char *value);  /* for the next program; NULL removes */
/* The next program's arguments: argv[0] = path, then the mark and the pairs. Returns argc. */
int  handoff_argv(const char *path, char **argv, int max);

#endif
