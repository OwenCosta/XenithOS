#ifndef XENITH_CRASH_H
#define XENITH_CRASH_H

/* Crash reports, so a crash reaches the people who make XenithOS without anyone writing in.
 *
 * When XenithOS or one of its apps crashes, a crash file is left in CRASH_DIR, named after the
 * program ("xenithos.txt", "hostmc.txt"):
 *   PCs and Pis: linux/crash_linux.c catches SIGSEGV, SIGABRT (glibc's buffer overflow checks
 *     end in one), SIGBUS, SIGFPE and SIGILL in every program; plat_init() sets it up.
 *   Xbox: crashdump() in shared/platform_xenon.c, which libxenon calls for any exception it
 *     can't handle, in place of libxenon's own.
 *   The simulator leaves crashes to AddressSanitizer.
 * A crash file is key=value lines: program, version, what stopped it (signal= or exception=),
 * where (pc=), how long it had been running and the start-up steps it got through
 * (startlog.h), then the stack: raw addresses with the program's build id and where it was
 * loaded, and on PCs, after "--- where" and "--- trace" lines, function names and source lines
 * from addr2line and the C library's names for its own functions.
 *
 * The next time XenithOS starts, and each time it comes back from an app, crash_check() finds
 * them. With Settings > Crash reports on, they go to XenithOS Cloud by themselves (POST
 * /v1/crash); with it off, XenithOS asks first. Nothing personal goes with one: no gamertag
 * unless the person chooses to add it, and paths in their home folder are cut down to the
 * file's own name. */
#define CRASH_DIR "uda:/xenith/crash"

/* source/crash_report.c, in XenithOS itself: send what's in CRASH_DIR, or ask. at_start says
 * whether XenithOS has just started (the crash was "last time") or is back from an app. */
void crash_check(int at_start);

#endif
