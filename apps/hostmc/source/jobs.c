/* Child processes for HostMC: downloads through curl (the Xbox-style HTTP client has no TLS,
 * and every source HostMC uses is HTTPS), checksums through coreutils, unpacking through tar.
 * They run while the screen keeps moving. Only exec-safe calls happen after fork(), since
 * XenithOS's sound thread may be running. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "gfx.h"
#include "hostmc.h"
#include "theme.h"
#include "ui.h"

#define JOB_LOG HM_DIR "/jobs.log"

/* A program's full path from PATH, found before fork(). */
const char *find_program(const char *name, char *out, int n)
{
    const char *path = getenv("PATH");
    char dirs[1024], *d, *save = NULL;
    if (strchr(name, '/')) { snprintf(out, n, "%s", name); return out; }
    snprintf(dirs, sizeof dirs, "%s", path && *path ? path : "/usr/local/bin:/usr/bin:/bin");
    for (d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(out, n, "%s/%s", d, name);
        if (access(out, X_OK) == 0) return out;
    }
    return NULL;
}

int job_spawn(struct job *j, const char *const argv[], const char *out, const char *err)
{
    char prog[600];
    int in_fd, out_fd, err_fd, fd;
    sigset_t none;
    struct sigaction dfl;
    pid_t pid;
    memset(j, 0, sizeof *j);
    if (!find_program(argv[0], prog, sizeof prog)) return 0;
    in_fd = open("/dev/null", O_RDONLY);
    out_fd = out ? open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600) : open("/dev/null", O_WRONLY);
    err_fd = open(err ? err : JOB_LOG, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (in_fd < 0 || out_fd < 0 || err_fd < 0) {
        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
        if (err_fd >= 0) close(err_fd);
        return 0;
    }
    sigemptyset(&none);
    memset(&dfl, 0, sizeof dfl);
    dfl.sa_handler = SIG_DFL;
    pid = fork();
    if (pid == 0) {
        dup2(in_fd, 0);
        dup2(out_fd, 1);
        dup2(err_fd, 2);
        for (fd = 3; fd < 1024; fd++) close(fd);
        sigaction(SIGPIPE, &dfl, NULL);         /* XenithOS ignores it; the tools expect it */
        sigprocmask(SIG_SETMASK, &none, NULL);
        execv(prog, (char *const *)argv);
        _exit(127);
    }
    close(in_fd);
    close(out_fd);
    close(err_fd);
    if (pid < 0) return 0;
    j->pid = pid;
    j->running = 1;
    return 1;
}

int job_done(struct job *j)
{
    int st;
    pid_t r;
    if (!j->running) return 1;
    r = waitpid(j->pid, &st, WNOHANG);
    if (r == j->pid) {
        j->running = 0;
        j->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
        return 1;
    }
    if (r < 0 && errno != EINTR) {
        j->running = 0;
        j->status = -1;
        return 1;
    }
    return 0;
}

/* Our own child, so its PID can't have been reused while it's unreaped. */
void job_stop(struct job *j)
{
    int i;
    if (!j->running) return;
    kill(j->pid, SIGTERM);
    for (i = 0; i < 100 && !job_done(j); i++) plat_sleep_us(10000);
    if (j->running) {
        kill(j->pid, SIGKILL);
        waitpid(j->pid, NULL, 0);
        j->running = 0;
    }
    j->status = -2;
}

/* ui_loading_progress's layout, with the bytes so far under the bar and a hint to cancel. */
static void busy_frame(const char *title, const char *detail, long long done, long long total, int can_cancel)
{
    int cy = SCR_H / 2, w = 300, x = (SCR_W - w) / 2;
    ui_bg();
    ui_spinner(SCR_W / 2, cy - 44, 24, C_ACCENT);
    gfx_str_c(FONT_LABEL, SCR_W / 2, cy + 6, C_TEXT, title);
    if (detail && *detail) gfx_str_c(FONT_BODY, SCR_W / 2, cy + 46, C_SUB, detail);
    if (total > 0) {
        char a[24], b[24], line[64];
        float f = (float)done / (float)total;
        if (f > 1) f = 1;
        if (f < 0) f = 0;
        gfx_fill_rrect((float)x, cy + 96.0f, (float)w, 4, 2, C_SURFACE2, 255);
        if (f > 0) gfx_fill_rrect((float)x, cy + 96.0f, w * f, 4, 2, C_ACCENT, 255);
        human_size(done < 0 ? 0 : done, a, sizeof a);
        human_size(total, b, sizeof b);
        snprintf(line, sizeof line, "%s of %s", a, b);
        gfx_str_c(FONT_SMALL, SCR_W / 2, cy + 112, C_DIM, line);
    }
    if (can_cancel) ui_hints("B=Cancel");
    ui_end_frame();
}

int job_run(const char *const argv[], const char *out, const char *title, const char *detail,
            const char *watch, long long total, int can_cancel)
{
    return job_run_err(argv, out, NULL, title, detail, watch, total, can_cancel);
}

/* job_run with what the program says on stderr kept in err (made empty first). */
int job_run_err(const char *const argv[], const char *out, const char *err, const char *title, const char *detail,
                const char *watch, long long total, int can_cancel)
{
    struct job j;
    if (err) unlink(err);
    if (!job_spawn(&j, argv, out, err)) return -1;
    for (;;) {
        if (job_done(&j)) return j.status;
        ui_poll();
        if (can_cancel && PRESSED(b)) {
            sfx(SFX_BACK);
            job_stop(&j);
            return -2;
        }
        hm_frame();
        busy_frame(title, detail, watch ? file_size(watch) : 0, total, can_cancel);
    }
}

/* What curl's exit code means, for people. */
static void curl_why(int code, char *why, int n)
{
    switch (code) {
    case 5: case 6: snprintf(why, n, "This PC can't reach the internet right now. Check the network and try again."); break;
    case 7: case 28: case 56: snprintf(why, n, "The download site didn't answer in time. Try again in a minute."); break;
    case 22: snprintf(why, n, "The download site answered with an error. Try again later."); break;
    case 23: snprintf(why, n, "It couldn't be saved. Is the disk full?"); break;
    case 35: case 60: snprintf(why, n, "The secure connection failed. Is the PC's clock right?"); break;
    case -1: snprintf(why, n, "curl isn't installed. Run the XenithOS install command once more."); break;
    default: snprintf(why, n, "The download failed (curl said %d). Try again.", code);
    }
}

int fetch(const char *url, const char *out, const char *title, const char *detail, long long total, int can_cancel)
{
    char part[720];
    int code;
    const char *argv[] = {"curl", "-fsSL", "--retry", "2", "--connect-timeout", "20", "-A", HM_AGENT, "-o", part, url, NULL};
    snprintf(part, sizeof part, "%s.part", out);
    unlink(part);
    code = job_run(argv, NULL, title, detail, part, total, can_cancel);
    if (code == 0 && rename(part, out) == 0) return 0;
    unlink(part);
    return code == 0 ? 23 : code;
}

static char fetch_why_buf[200];

char *fetch_text(const char *url, const char *cache, int max_age_s, const char *title, char *why, int why_len)
{
    struct stat s;
    char *text;
    int code;
    if (max_age_s > 0 && stat(cache, &s) == 0 && time(NULL) - s.st_mtime < max_age_s && (text = file_read(cache, NULL)))
        return text;
    mkdirs(HM_CACHE);
    code = fetch(url, cache, title, NULL, 0, 1);
    if (code == 0 && (text = file_read(cache, NULL))) return text;
    if (code == -2) {
        snprintf(why, why_len, "Cancelled.");
        return NULL;
    }
    /* Offline: an older copy is better than nothing. */
    if (code != 22 && (text = file_read(cache, NULL))) return text;
    curl_why(code, fetch_why_buf, sizeof fetch_why_buf);
    snprintf(why, why_len, "%s", fetch_why_buf);
    return NULL;
}

int checksum_ok(const char *algo, const char *path, const char *hex)
{
    char tool[16], out[720], got[130];
    const char *argv[] = {tool, path, NULL};
    char *text;
    size_t hl = strlen(hex);
    int code, ok;
    if (!hl) return 1;                          /* nothing to check against */
    snprintf(tool, sizeof tool, "%ssum", algo);
    snprintf(out, sizeof out, "%s.sum", path);
    code = job_run(argv, out, "Checking the download", NULL, NULL, 0, 0);
    text = code == 0 ? file_read(out, NULL) : NULL;
    unlink(out);
    if (!text) return 0;
    snprintf(got, sizeof got, "%.*s", (int)strcspn(text, " \n"), text);
    free(text);
    ok = strlen(got) == hl && !strncasecmp(got, hex, hl);
    return ok;
}

/* For install.c and playit.c: curl's reason as text. */
void fetch_reason(int code, char *why, int n)
{
    curl_why(code, why, n);
}

/* ------------------------------------------------- programs left running */

/* Field 22 of /proc/<pid>/stat: when it started, in clock ticks since boot. */
unsigned long long proc_ticks(pid_t pid)
{
    char path[64], buf[1024], *p;
    FILE *f;
    size_t n;
    int field;
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    if (!(f = fopen(path, "r"))) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    if (!(p = strrchr(buf, ')')) || !p[1]) return 0;     /* the name can hold spaces and brackets */
    for (p += 2, field = 3; field < 22 && *p; p++)
        if (*p == ' ') field++;
    return strtoull(p, NULL, 10);
}

/* 1 if pid is still the program HostMC started: alive, started at the recorded time, and
 * running in dir (an absolute path). Nothing is signalled without this. */
int proc_ours(pid_t pid, unsigned long long ticks, const char *dir)
{
    char link[64], cwd[720];
    ssize_t n;
    if (pid <= 1 || kill(pid, 0) != 0) return 0;
    if (ticks && proc_ticks(pid) != ticks) return 0;
    snprintf(link, sizeof link, "/proc/%d/cwd", (int)pid);
    n = readlink(link, cwd, sizeof cwd - 1);
    if (n <= 0) return 0;                                /* gone (a zombie has no folder) */
    cwd[n] = 0;
    return !strcmp(cwd, dir);
}

/* Starts argv[0] (a full path) in dir with in_fd and out_fd as its input and output, detached
 * from HostMC: a session of its own, and adopted by init through a double fork, so it outlives
 * HostMC and XenithOS closing HostMC. Returns its PID, or 0. */
pid_t spawn_detached(const char *dir, char *const argv[], int in_fd, int out_fd)
{
    int pipefd[2], fd;
    pid_t mid, prog = 0;
    sigset_t none;
    struct sigaction dfl;
    if (pipe(pipefd) != 0) return 0;
    sigemptyset(&none);
    memset(&dfl, 0, sizeof dfl);
    dfl.sa_handler = SIG_DFL;
    mid = fork();
    if (mid == 0) {
        pid_t g;
        setsid();
        g = fork();
        if (g == 0) {
            if (chdir(dir) != 0) _exit(126);
            dup2(in_fd, 0);
            dup2(out_fd, 1);
            dup2(out_fd, 2);
            for (fd = 3; fd < 1024; fd++) close(fd);
            sigaction(SIGPIPE, &dfl, NULL);
            sigaction(SIGTERM, &dfl, NULL);
            sigaction(SIGINT, &dfl, NULL);
            sigaction(SIGHUP, &dfl, NULL);
            sigprocmask(SIG_SETMASK, &none, NULL);
            execv(argv[0], argv);
            {
                static const char msg[] = "HostMC: the program couldn't start\n";
                if (write(2, msg, sizeof msg - 1) < 0) { }
            }
            _exit(127);
        }
        if (write(pipefd[1], &g, sizeof g) != (ssize_t)sizeof g) { }
        _exit(0);
    }
    close(pipefd[1]);
    if (mid > 0) {
        if (read(pipefd[0], &prog, sizeof prog) != (ssize_t)sizeof prog) prog = 0;
        while (waitpid(mid, NULL, 0) < 0 && errno == EINTR) { }
    }
    close(pipefd[0]);
    return mid > 0 && prog > 0 ? prog : 0;
}
