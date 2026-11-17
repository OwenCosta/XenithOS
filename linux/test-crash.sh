#!/bin/bash
# Crash reports on a PC (shared/crash.h), in a virtual display, against a local XenithOS Cloud:
#   1. VOLT crashes inside XenithOS (XENITHOS_TEST_CRASH=volt:segv). With Settings > Send crash
#      reports at "Always", XenithOS sends the report as it comes back, and says so.
#   2. XenithOS itself aborts the way glibc's buffer overflow checks end (xenithos:fortify, which is
#      how HostMC died on Ubuntu). With "Ask first" it asks at the next start, and "Send the report"
#      sends it.
#   3. VOLT crashes again, and the answer is "Don't send it": nothing is sent, and the file goes.
# The reports must say where it stopped (function and source line, from addr2line) and have nothing
# from the home folder in them, so the test's data folder is in the home folder.
# Needs xvfb, xdotool, x11-apps, imagemagick, curl, python3 and binutils. Run after make -C linux.
repo="$(cd "$(dirname "$0")/.." && pwd)"
T="$HOME/.cache/xenithos-crash-test"
D="$T/data"
PORT=8797
rm -rf "$T"
mkdir -p "$D/uda:/apps" "$D/uda:/xenith" "$T/shots" "$T/state"
ln -s "$repo/linux/volt" "$D/uda:/apps/volt.elf32"
cp "$repo/apps/volt/app.txt" "$D/uda:/apps/volt.txt"
cp "$repo/art/thumbs/volt.ximg" "$D/uda:/apps/volt.ximg"
echo "server=127.0.0.1:$PORT" > "$D/uda:/xenith/config.txt"
settings() { printf 'volume=0\nui_sounds=0\nboot_sound=0\nauto_connect=1\ncrash_reports=%s\n' "$1" > "$D/uda:/xenith/settings.txt"; }
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }

XENITH_PORT=$PORT XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" \
    XENITH_ADMIN_KEY=t3st python3 "$repo/server/xenith_cloud.py" > "$T/cloud.log" 2>&1 &
CLOUD=$!
Xvfb :78 -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:78 XENITHOS_WINDOW=1 XENITHOS_DATA="$D" SDL_AUDIODRIVER=dummy
cleanup() { kill "$XP" "$CLOUD" "$XV" 2> /dev/null; }
trap cleanup EXIT

crashes() { curl -s -H "X-Xenith-Key: t3st" "http://127.0.0.1:$PORT/v1/crash"; }
report() { curl -s -H "X-Xenith-Key: t3st" "http://127.0.0.1:$PORT/v1/crash/$1"; }
arrived() { for _ in $(seq 1 50); do [ "$(crashes | wc -l)" -ge "$1" ] && return; sleep 0.2; done; }
press() { xdotool keydown "$1"; sleep 0.08; xdotool keyup "$1"; sleep 0.3; }
shot() { xwd -root -silent | convert xwd:- "$T/shots/$1.png"; echo "  shot  $T/shots/$1.png"; }
# XenithOS in the virtual display, with these settings in its environment (VAR=value ...)
start() { env "$@" "$repo/linux/xenithos" 2>> "$T/xenithos.log" & XP=$!; sleep 6; xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1; }
stop() { kill "$XP" 2> /dev/null; wait "$XP" 2> /dev/null; }
private() { ! grep -q -e "$HOME" -e "/home/" -e "$(whoami)" <<< "$1"; }

echo "1. VOLT crashes inside XenithOS, and its report goes by itself"
settings 1
start XENITHOS_TEST_CRASH=volt:segv XENITHOS_TEST_CRASH_FRAME=60
shot home
press Down                                   # "Jump back in": VOLT
press Return
arrived 1                                    # VOLT starts, crashes a second in, the report goes
sleep 0.5
shot sent_by_itself                          # with a toast to say so
rows=$(crashes)
r1=$(report 1)
check "VOLT's report arrived" "grep -q '^1|[0-9]*|volt|' <<< \"\$rows\""
check "it says what: SIGSEGV at address 0x10" "grep -q '^signal=SIGSEGV' <<< \"\$r1\" && grep -q '^address=0x10' <<< \"\$r1\""
check "it says where: crash_on_purpose, linux/crash_linux.c" \
      "grep -q '|crash_on_purpose at linux/crash_linux.c:[0-9]*|' <<< \"\$rows\""
check "it has the start-up steps and the build id" "grep -q '^step=ready ' <<< \"\$r1\" && grep -q '^build=[0-9a-f]\{40\}' <<< \"\$r1\""
check "it went without a gamertag" "[ \"\$(head -1 <<< \"\$rows\" | cut -d'|' -f8)\" = Anonymous ]"
check "nothing from the home folder in it" "private \"\$r1\""
check "VOLT's crash file went once it was sent" "[ ! -e '$D/uda:/xenith/crash/volt.txt' ]"
check "XenithOS is still running" "kill -0 $XP"
stop

echo "2. XenithOS aborts in a buffer overflow check; next start it asks"
settings 0
timeout 60 env XENITHOS_TEST_CRASH=xenithos:fortify XENITHOS_TEST_CRASH_FRAME=200 "$repo/linux/xenithos" 2>> "$T/xenithos.log"
code=$?
check "XenithOS still ends with SIGABRT as it would have (exit $code)" "[ $code = 134 ]"
check "it left a crash file with glibc's message" \
      "grep -q '^message=\*\*\* buffer overflow detected \*\*\*' '$D/uda:/xenith/crash/xenithos.txt'"
start
shot asks
press Return                                 # Send the report
arrived 2
sleep 0.5
shot sent_after_asking
rows=$(crashes)
check "XenithOS's report arrived: SIGABRT, buffer overflow detected" \
      "grep -q '^2|[0-9]*|xenithos|[^|]*|[^|]*|SIGABRT, buffer overflow detected|' <<< \"\$rows\""
check "nothing from the home folder in it" "private \"\$(report 2)\""
check "its crash file went" "[ ! -e '$D/uda:/xenith/crash/xenithos.txt' ]"
stop

echo "3. VOLT crashes again, and the answer is no"
start XENITHOS_TEST_CRASH=volt:abort XENITHOS_TEST_CRASH_FRAME=60
press Down
press Return
sleep 4
shot asks_again
check "VOLT's crash file is there while it asks" "[ -e '$D/uda:/xenith/crash/volt.txt' ]"
press Down                                   # Don't send it
press Return
sleep 1
shot not_sent
check "nothing more arrived" "[ \"\$(crashes | wc -l)\" = 2 ]"
check "the crash file went anyway" "[ ! -e '$D/uda:/xenith/crash/volt.txt' ]"
stop

[ $failed = 0 ] && echo "all passed" || echo "some FAILED (logs in $T)"
exit $failed
