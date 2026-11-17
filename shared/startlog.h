#ifndef XENITH_STARTLOG_H
#define XENITH_STARTLOG_H

#include <stdint.h>

/* Where start-up time goes, so slow starts can be measured on the real machine.
 *
 * Every program calls startlog_mark() as it gets through each step. The shell calls
 * startlog_launching() right before it hands over to an app; the app calls
 * startlog_ready() once its first screen is up, which saves the steps (and the time since
 * the shell let go) to uda:/xenith/start.txt for System info to show. */

void startlog_mark(const char *step);              /* the first call is the start */
void startlog_launching(const char *app_name);
void startlog_ready(const char *app_name);

/* "1.9 s: video 0.6, USB 0.4, storage 0.2" for this program's own start. */
void startlog_summary(char *out, int n);
/* The same for the last app start, read back from uda:/xenith/start.txt. 0 if none. */
int  startlog_last_app(char *out, int n);
/* For crash files (crash.h): the steps marked so far, and when (plat_time_us). It only copies,
 * so a crash handler can call it. Returns how many. */
int  startlog_steps(const char **steps, uint64_t *at, int max);

#endif
