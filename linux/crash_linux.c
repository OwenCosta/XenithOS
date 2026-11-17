/* Crash files on PCs and Raspberry Pis (shared/crash.h).
 *
 * linux_crash_watch() is the first thing plat_init() does, in XenithOS and in every app. It works
 * out ahead of time what a crash file needs (where it goes, the program's build id and where it's
 * loaded, where addr2line is), because once a fatal signal arrives only async-signal-safe calls
 * may be made. on_crash() then:
 *   writes the file with write() under a temporary name, and renames it into place as soon as
 *   the basics are in, so a half-written one never counts;
 *   for an abort, adds the message glibc gave, when it was glibc that aborted ("*** buffer
 *   overflow detected ***", a double free, a failed assert);
 *   lists the stack with backtrace(), which was called once beforehand so it has nothing left to
 *   load (the first call would allocate);
 *   runs addr2line in a child process for function names and source lines in the program itself
 *   (a fork through the system call, so no atfork handlers run, then execve), for up to 4 s;
 *   adds backtrace_symbols_fd()'s lines, which name the shared libraries' own functions (the
 *   __chk_fail and abort of a buffer overflow check, say);
 *   then lets the signal go on: the program ends just as it would have (core dump and all), so
 *   XenithOS still sees the app it was running end with that signal.
 * alarm() is the safety net: if something in there hangs, the program still ends 10 s later.
 * The handler runs on a stack of its own, so running out of stack (on the main thread) gets a
 * crash file too.
 *
 * Tests: XENITHOS_TEST_CRASH=segv, abort, fortify (the way glibc's buffer overflow checks end),
 * stack (runs out of it), fpe, ill or bus crashes on purpose at frame XENITHOS_TEST_CRASH_FRAME
 * (default 30) of
 * plat_present(); "volt:segv" crashes only the program called volt (linux/test-crash.sh). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include "crash.h"
#include "platform_linux.h"
#include "startlog.h"
#include "theme.h"

#define MAX_FRAMES 40
#define MAX_STEPS  16

static char program[40];                      /* "xenithos", "hostmc"... */
static char final_path[720], temp_path[730];
static char exe_path[600];
static char build_id[65];
static uintptr_t load_base, prog_lo, prog_hi; /* the program itself in memory */
static char addr2line[256];
static char machine[40];
static uint64_t watch_us;                     /* when watching began, if startlog has no steps */
static volatile sig_atomic_t crashing;
static volatile pid_t crash_tid;
static char alt_stack[64 * 1024] __attribute__((aligned(16)));
static const int fatal[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
static const char *test_kind;
static int test_frame = 30, test_frames;
/* glibc keeps the message of its own fatal errors here for core dumps ("*** buffer overflow
 * detected ***: terminated", "free(): double free detected in tcache 2", a failed assert), before
 * it aborts. It's glibc's private variable, so it's only looked for (dlsym), and only read once
 * the rest of the crash file is safe. */
struct abort_msg {
    unsigned int size;
    char msg[];
};
static struct abort_msg **abort_msg;

/* ---------------------------------------------------------------- writing
 * No stdio in a signal handler: a small buffer, written out with write(). */
struct out {
    int fd, n;
    char buf[512];
};

static void put_flush(struct out *o)
{
    int done = 0;
    while (done < o->n) {
        ssize_t w = write(o->fd, o->buf + done, (size_t)(o->n - done));
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += (int)w;
    }
    o->n = 0;
}

static void put(struct out *o, const char *s)
{
    for (; s && *s; s++) {
        if (o->n == (int)sizeof o->buf) put_flush(o);
        o->buf[o->n++] = *s;
    }
}

/* v in base 10 or 16 into out (at least 24 bytes), at least min digits. */
static char *fmt_num(char *out, unsigned long long v, unsigned base, int min)
{
    char tmp[24];
    int i = 0, k = 0;
    do {
        tmp[i++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v && i < 22);
    while (i < min && i < 22) tmp[i++] = '0';
    while (i) out[k++] = tmp[--i];
    out[k] = 0;
    return out;
}

static void put_dec(struct out *o, long long v)
{
    char t[24];
    if (v < 0) {
        put(o, "-");
        v = -v;
    }
    put(o, fmt_num(t, (unsigned long long)v, 10, 1));
}

static void put_hex(struct out *o, unsigned long long v)
{
    char t[24];
    put(o, "0x");
    put(o, fmt_num(t, v, 16, 1));
}

static void put_secs(struct out *o, uint64_t us)
{
    char t[24];
    put(o, fmt_num(t, us / 1000000, 10, 1));
    put(o, ".");
    put(o, fmt_num(t, (us % 1000000) / 10000, 10, 2));
}

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* ----------------------------------------------------------- what stopped it */

static const char *sig_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    }
    return "signal";
}

/* The kernel's reason (si_code), in words. */
static const char *code_name(int sig, int code)
{
    if (code == SI_USER) return "sent by kill()";
    if (code == SI_TKILL) return "raised by the program (abort or raise)";
    switch (sig) {
    case SIGSEGV:
        if (code == SEGV_MAPERR) return "no memory at that address";
        if (code == SEGV_ACCERR) return "not allowed at that address";
        break;
    case SIGBUS:
        if (code == BUS_ADRALN) return "misaligned address";
        if (code == BUS_ADRERR) return "no such physical address (a file cut short?)";
        break;
    case SIGFPE:
        if (code == FPE_INTDIV) return "integer divided by zero";
        if (code == FPE_INTOVF) return "integer overflow";
        break;
    case SIGILL:
        if (code == ILL_ILLOPC || code == ILL_ILLOPN) return "illegal instruction";
        if (code == ILL_PRVOPC) return "privileged instruction";
        break;
    }
    return "";
}

static uintptr_t context_pc(void *ucv)
{
    ucontext_t *uc = ucv;
    if (!uc) return 0;
#if defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__)
    return (uintptr_t)uc->uc_mcontext.pc;
#elif defined(__arm__)
    return (uintptr_t)uc->uc_mcontext.arm_pc;
#else
    return 0;
#endif
}

/* ------------------------------------------------------------- addr2line */

static pid_t raw_fork(void)
{
#ifdef SYS_fork
    return (pid_t)syscall(SYS_fork);
#else
    return (pid_t)syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
#endif
}

/* Function names and source lines for the frames in the program itself, from frame first on
 * (the one that stopped): "0x1a2b: main at ../apps/hostmc/source/main.c:1349", with the
 * functions inlined there under it. addr2line reads the program through /proc, so it's the
 * very file that crashed even if an update has replaced it since. */
static void where_lines(int fd, void *const *frames, int n, int first)
{
    static char addr_text[MAX_FRAMES][24], exe[48], pid_text[24];
    char *argv[12 + MAX_FRAMES];
    char *envp[] = {"LC_ALL=C", NULL};
    struct timespec pause = {0, 20 * 1000 * 1000};
    int argc = 0, count = 0, i, status, waited;
    pid_t pid;

    if (!addr2line[0]) return;
    fmt_num(pid_text, (unsigned long long)getpid(), 10, 1);
    exe[0] = 0;
    strcat(exe, "/proc/");
    strcat(exe, pid_text);
    strcat(exe, "/exe");
    argv[argc++] = addr2line;
    argv[argc++] = "-a";
    argv[argc++] = "-f";
    argv[argc++] = "-C";
    argv[argc++] = "-i";
    argv[argc++] = "-p";
    argv[argc++] = "-e";
    argv[argc++] = exe;
    for (i = first; i < n && count < MAX_FRAMES; i++) {
        uintptr_t a = (uintptr_t)frames[i];
        if (a < prog_lo || a >= prog_hi) continue;
        if (i > first) a -= 1;               /* a return address: the call is just before it */
        addr_text[count][0] = '0';
        addr_text[count][1] = 'x';
        fmt_num(addr_text[count] + 2, (unsigned long long)(a - load_base), 16, 1);
        argv[argc++] = addr_text[count++];
    }
    if (!count) return;
    argv[argc] = NULL;
    pid = raw_fork();
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(fd, 1);
        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 2);
        }
        execve(addr2line, argv, envp);
        _exit(127);
    }
    if (pid < 0) return;
    for (waited = 0; waited < 200; waited++) {          /* up to 4 seconds */
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || (r < 0 && errno != EINTR)) return;
        nanosleep(&pause, NULL);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

/* ---------------------------------------------------------------- handler */

static void let_it_go_on(int sig)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
    raise(sig);                               /* delivered as the handler returns */
}

static void on_crash(int sig, siginfo_t *si, void *uc)
{
    void *frames[MAX_FRAMES];
    const char *steps[MAX_STEPS];
    uint64_t at[MAX_STEPS], now = now_us();
    uintptr_t pc = context_pc(uc);
    struct out o;
    int n, nsteps, first = 0, i, saved_errno = errno;
    pid_t tid = (pid_t)syscall(SYS_gettid);

    if (__atomic_exchange_n(&crashing, 1, __ATOMIC_SEQ_CST)) {
        /* Another thread crashed too: give the first one time to finish its file. The same
         * thread crashing again inside the handler just ends. */
        if (crash_tid != tid) {
            struct timespec wait = {6, 0};
            nanosleep(&wait, NULL);
        }
        let_it_go_on(sig);
        return;
    }
    crash_tid = tid;
    alarm(10);

    n = backtrace(frames, MAX_FRAMES);
    for (i = 0; i < n; i++)
        if ((uintptr_t)frames[i] == pc) {
            first = i;
            break;
        }
    if (i == n) first = n > 2 ? 2 : 0;        /* this handler, then the kernel's signal return */

    o.fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (o.fd >= 0) {
        o.n = 0;
        put(&o, "# XenithOS crash report\nprogram=");
        put(&o, program);
        put(&o, "\nversion=" XENITH_VERSION "\nmachine=");
        put(&o, machine);
        put(&o, "\nsignal=");
        put(&o, sig_name(sig));
        if (si) {
            put(&o, "\ncode=");
            put_dec(&o, si->si_code);
            if (code_name(sig, si->si_code)[0]) {
                put(&o, " ");
                put(&o, code_name(sig, si->si_code));
            }
            if (sig != SIGABRT && si->si_code > 0) {
                put(&o, "\naddress=");
                put_hex(&o, (uintptr_t)si->si_addr);
            }
        }
        if (pc) {
            put(&o, "\npc=");
            put_hex(&o, pc);
            if (pc >= prog_lo && pc < prog_hi) {
                put(&o, " (program+");
                put_hex(&o, pc - load_base);
                put(&o, ")");
            }
        }
        nsteps = startlog_steps(steps, at, MAX_STEPS);
        put(&o, "\nseconds=");
        put_secs(&o, now - (nsteps ? at[0] : watch_us));
        for (i = 0; i < nsteps; i++) {
            put(&o, "\nstep=");
            put(&o, steps[i]);
            put(&o, " ");
            put_secs(&o, at[i] - at[0]);
        }
        if (build_id[0]) {
            put(&o, "\nbuild=");
            put(&o, build_id);
        }
        put(&o, "\nbase=");
        put_hex(&o, load_base);
        put(&o, "\nsize=");
        put_hex(&o, prog_hi - prog_lo);
        put(&o, "\nexe=");
        put(&o, exe_path);
        for (i = first; i < n; i++) {
            uintptr_t a = (uintptr_t)frames[i];
            put(&o, "\nframe=");
            put_hex(&o, a);
            if (a >= prog_lo && a < prog_hi) {
                put(&o, " program+");
                put_hex(&o, a - load_base);
            }
        }
        put(&o, "\n");
        put_flush(&o);
        rename(temp_path, final_path);        /* the basics are in: it counts from here */

        if (sig == SIGABRT && abort_msg && *abort_msg && (*abort_msg)->size > sizeof(unsigned int)) {
            const char *m = (*abort_msg)->msg;
            unsigned k, max = (*abort_msg)->size - (unsigned)sizeof(unsigned int);
            char c[2] = {0, 0};
            put(&o, "message=");
            for (k = 0; k < max && k < 200 && m[k]; k++) {
                c[0] = m[k] >= 32 && m[k] < 127 ? m[k] : ' ';
                put(&o, c);
            }
            put(&o, "\n");
        }
        put(&o, "--- where\n");
        put_flush(&o);
        where_lines(o.fd, frames, n, first);
        put(&o, "--- trace\n");
        put_flush(&o);
        backtrace_symbols_fd(frames + first, n - first, o.fd);
        fsync(o.fd);
        close(o.fd);
    }
    alarm(0);
    errno = saved_errno;
    let_it_go_on(sig);
}

/* ------------------------------------------------------------------ setup */

static void read_build_id(const char *p, size_t len)
{
    size_t off = 0;
    while (off + sizeof(ElfW(Nhdr)) <= len) {
        const ElfW(Nhdr) *nh = (const ElfW(Nhdr) *)(const void *)(p + off);
        size_t name = (nh->n_namesz + 3) & ~(size_t)3, desc = (nh->n_descsz + 3) & ~(size_t)3;
        const unsigned char *d = (const unsigned char *)p + off + sizeof *nh + name;
        if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && !memcmp(p + off + sizeof *nh, "GNU", 4)) {
            unsigned i;
            for (i = 0; i < nh->n_descsz && i < 32; i++) {
                build_id[i * 2] = "0123456789abcdef"[d[i] >> 4];
                build_id[i * 2 + 1] = "0123456789abcdef"[d[i] & 15];
            }
            build_id[i * 2] = 0;
            return;
        }
        off += sizeof *nh + name + desc;
    }
}

/* The first object dl_iterate_phdr() visits is the program itself. */
static int find_program(struct dl_phdr_info *info, size_t size, void *data)
{
    int i;
    (void)size;
    (void)data;
    load_base = info->dlpi_addr;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD) {
            if (!prog_lo || lo < prog_lo) prog_lo = lo;
            if (lo + ph->p_memsz > prog_hi) prog_hi = lo + ph->p_memsz;
        } else if (ph->p_type == PT_NOTE && !build_id[0]) {
            read_build_id((const char *)lo, ph->p_memsz);
        }
    }
    return 1;
}

static void make_dirs(char *path)
{
    char *p;
    for (p = path + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(path, 0755);
            *p = '/';
        }
    mkdir(path, 0755);
}

void linux_crash_watch(void)
{
    const char *data = getenv("XENITHOS_DATA"), *home = getenv("HOME"), *s;
    char dir[640], cwd[512], *base, *p;
    struct utsname u;
    struct sigaction sa;
    stack_t ss;
    void *warm[4];
    ssize_t len;
    size_t i;

    watch_us = now_us();
    len = readlink("/proc/self/exe", exe_path, sizeof exe_path - 1);
    exe_path[len > 0 ? len : 0] = 0;
    if ((p = strstr(exe_path, " (deleted)"))) *p = 0;
    base = strrchr(exe_path, '/');
    snprintf(program, sizeof program, "%s", base ? base + 1 : exe_path);
    if ((p = strchr(program, '.'))) *p = 0;
    for (p = program; *p; p++)
        *p = isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '-';
    if (!program[0]) snprintf(program, sizeof program, "xenithos");

    /* Where plat_storage_init() puts "uda:", made absolute, since programs change folders. */
    cwd[0] = 0;
    if (data && *data && data[0] != '/' && !getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    if (data && *data) snprintf(dir, sizeof dir, "%s%s%s/" CRASH_DIR, cwd, cwd[0] ? "/" : "", data);
    else snprintf(dir, sizeof dir, "%s/.local/share/xenithos/" CRASH_DIR, home && *home ? home : "/tmp");
    make_dirs(dir);
    snprintf(final_path, sizeof final_path, "%s/%s.txt", dir, program);
    snprintf(temp_path, sizeof temp_path, "%s/.%s.tmp", dir, program);

    dl_iterate_phdr(find_program, NULL);
    if (!linux_find_program("addr2line", addr2line, sizeof addr2line)) addr2line[0] = 0;
    snprintf(machine, sizeof machine, "%s", uname(&u) == 0 ? u.machine : "?");
    abort_msg = (struct abort_msg **)dlsym(RTLD_DEFAULT, "__abort_msg");
    backtrace(warm, 4);                       /* loads what backtrace() needs, now */

    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof alt_stack;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof fatal / sizeof fatal[0]; i++) sigaction(fatal[i], &sa, NULL);

    if ((s = getenv("XENITHOS_TEST_CRASH")) && *s) {
        const char *colon = strchr(s, ':');
        /* "volt:segv": only that program, so XenithOS can start the app that crashes */
        if (!colon || ((size_t)(colon - s) == strlen(program) && !strncmp(s, program, strlen(program))))
            test_kind = colon ? colon + 1 : s;
        if ((s = getenv("XENITHOS_TEST_CRASH_FRAME")) && *s) test_frame = atoi(s);
    }
}

/* ------------------------------------------------------------------ tests */

#ifdef __GLIBC__
extern void __chk_fail(void) __attribute__((noreturn));   /* glibc has it, its headers don't say */
#endif

static volatile int deeper_still = 1;

/* Runs out of stack: 4 KB a call, for as long as there is any. */
static int __attribute__((noinline)) deeper(int n)
{
    volatile char pad[4096];
    pad[n % 4096] = (char)n;
    return deeper_still ? deeper(n + 1) + pad[0] : 0;
}

static void __attribute__((noinline)) crash_on_purpose(const char *kind)
{
    fprintf(stderr, "xenithos: crashing on purpose (%s), as XENITHOS_TEST_CRASH asks\n", kind);
    if (!strcmp(kind, "segv")) {
        volatile int *volatile bad = (volatile int *)(uintptr_t)16;
        *bad = 1;
    } else if (!strcmp(kind, "abort")) {
        abort();
    } else if (!strcmp(kind, "fortify")) {
#ifdef __GLIBC__
        /* Where glibc's checks end when one fails: HostMC's crash on Ubuntu, a realpath() into
         * a buffer smaller than PATH_MAX, went __realpath_chk, __chk_fail, abort. */
        __chk_fail();
#endif
        abort();
    } else if (!strcmp(kind, "fpe")) {
        volatile int one = 1, zero = 0, r;
        r = one / zero;                       /* ARM doesn't trap this one: raise() below */
        (void)r;
    } else if (!strcmp(kind, "stack")) {
        deeper(0);                            /* the handler has a stack of its own for this */
    }
    raise(!strcmp(kind, "ill") ? SIGILL : !strcmp(kind, "bus") ? SIGBUS :
          !strcmp(kind, "fpe") ? SIGFPE : SIGSEGV);
}

void linux_crash_test(void)
{
    if (test_kind && ++test_frames == test_frame) crash_on_purpose(test_kind);
}
