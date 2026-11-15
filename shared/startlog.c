/* Start-up timing. See startlog.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "startlog.h"

#define LAUNCH_FILE "uda:/xenith/launch.txt"
#define START_FILE  "uda:/xenith/start.txt"
#define MAXSTEPS 12

static struct { const char *step; uint64_t at; } marks[MAXSTEPS];
static int nmarks;

void startlog_mark(const char *step)
{
    if (nmarks < MAXSTEPS) {
        marks[nmarks].step = step;
        marks[nmarks].at = plat_time_us();
        nmarks++;
    }
}

void startlog_launching(const char *app_name)
{
    FILE *f = fopen(LAUNCH_FILE, "w");
    if (!f) return;
    fprintf(f, "name=%s\nat=%llu\n", app_name, (unsigned long long)plat_time_us());
    fclose(f);
}

static int tenths(uint64_t a, uint64_t b)
{
    return b > a ? (int)((b - a + 50000) / 100000) : 0;
}

/* "video 0.6, USB 0.4": each step's time, skipping ones too short to matter. */
static void steps_text(char *out, int n)
{
    int i, used = 0;
    out[0] = 0;
    for (i = 1; i < nmarks && used < n - 1; i++) {
        int t = tenths(marks[i - 1].at, marks[i].at);
        if (!t) continue;
        used += snprintf(out + used, n - used, "%s%s %d.%d", used ? ", " : "", marks[i].step, t / 10, t % 10);
    }
}

void startlog_summary(char *out, int n)
{
    char steps[160];
    int t;
    if (nmarks < 2) { snprintf(out, n, "Not measured"); return; }
    t = tenths(marks[0].at, marks[nmarks - 1].at);
    steps_text(steps, sizeof steps);
    snprintf(out, n, "%d.%d s%s%s", t / 10, t % 10, steps[0] ? ": " : "", steps);
}

void startlog_ready(const char *app_name)
{
    FILE *f;
    char line[96], steps[160];
    unsigned long long at = 0;
    int loader = -1, total;

    startlog_mark("ready");
    f = fopen(LAUNCH_FILE, "r");
    if (f) {
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "at=", 3)) at = strtoull(line + 3, NULL, 10);
        fclose(f);
        remove(LAUNCH_FILE);
    }
    /* The timebase keeps counting across programs, so the shell's number still means something. */
    if (at && marks[0].at > at) loader = tenths(at, marks[0].at);
    total = tenths(at && marks[0].at > at ? at : marks[0].at, marks[nmarks - 1].at);
    steps_text(steps, sizeof steps);

    f = fopen(START_FILE, "w");
    if (!f) return;
    fprintf(f, "name=%s\ntotal=%d\nloader=%d\nsteps=%s\n", app_name, total, loader, steps);
    fclose(f);
}

int startlog_last_app(char *out, int n)
{
    FILE *f = fopen(START_FILE, "r");
    char line[200], name[40] = "", steps[160] = "";
    int total = -1, loader = -1;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5)) snprintf(name, sizeof name, "%s", line + 5);
        else if (!strncmp(line, "total=", 6)) total = atoi(line + 6);
        else if (!strncmp(line, "loader=", 7)) loader = atoi(line + 7);
        else if (!strncmp(line, "steps=", 6)) snprintf(steps, sizeof steps, "%s", line + 6);
    }
    fclose(f);
    if (total < 0 || !name[0]) return 0;
    if (loader >= 0)
        snprintf(out, n, "%s %d.%d s: loading %d.%d%s%s", name, total / 10, total % 10, loader / 10, loader % 10,
                 steps[0] ? ", " : "", steps);
    else
        snprintf(out, n, "%s %d.%d s%s%s", name, total / 10, total % 10, steps[0] ? ": " : "", steps);
    return 1;
}
