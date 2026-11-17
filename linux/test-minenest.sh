#!/bin/bash
# MineNest on a PC, in a virtual display: XenithOS starts the MineNest app inside its window, the app
# starts MineNest's own program (MINENEST_BIN, from github.com/OwenCosta/minenest) and through it a
# server (the stand-in, sim/minenest/java, as its java: nothing is downloaded). Then what a real PC
# needs: leaving the app leaves the server running, run by MineNest's program and not by the app or
# XenithOS, and opening the app again finds it. The app stops it. Then the app as a desktop app, as
# XenithOS Desktop's Apps menu would open the console version: its own window.
# Needs xvfb, xdotool, x11-apps, imagemagick and python3. Run after make -C linux, with
#   MINENEST_BIN=path/to/minenest linux/test-minenest.sh
repo="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/xos-minenest
MN=${MINENEST_BIN:-$(command -v minenest)}
[ -x "$MN" ] || { echo "MINENEST_BIN isn't set (build it from github.com/OwenCosta/minenest)"; exit 2; }
rm -rf "$T"
mkdir -p "$T/uda:/apps" "$T/uda:/xenith" "$T/shots" "$T/mn"
ln -s "$repo/linux/minenest" "$T/uda:/apps/minenest.elf32"
cp "$repo/apps/minenest/app.txt" "$T/uda:/apps/minenest.txt"
cp "$repo/art/thumbs/minenest.ximg" "$T/uda:/apps/minenest.ximg"
export MINENEST_BIN="$MN" MINENEST_HOME="$T/mn" MINENEST_JAVA="$repo/sim/minenest/java" MINENEST_FAKE_INSTALL=1
export MINENEST_CLOUD=http://127.0.0.1:9               # no XenithOS Cloud here
echo "{\"api_port\": $((41000 + RANDOM % 8000))}" > "$T/mn/minenest.json"
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }
wait_for() {
    local n=$1
    shift
    for _ in $(seq 1 "$((n * 5))"); do "$@" && return 0; sleep 0.2; done
    return 1
}
# A server already made (Paper, as MineNest makes them). The stand-in isn't Minecraft, so its
# eula.txt is the test's own.
"$MN" create "My Server" -t paper -v 26.3 --agree-eula > /dev/null || { echo "MineNest couldn't make a server"; exit 1; }

ours_x() { [ "$({ tr -d ' \n' < "/tmp/.X$1-lock"; } 2>/dev/null)" = "$XV" ] && xdotool getdisplaygeometry > /dev/null 2>&1; }
XV=
for n in $(seq 79 99); do
    if [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ]; then continue; fi
    Xvfb ":$n" -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
    XV=$!
    DISPLAY=":$n"
    export DISPLAY
    wait_for 15 ours_x "$n" && break
    kill "$XV" 2>/dev/null
    XV=
done
[ -n "$XV" ] || { echo "Xvfb didn't start"; exit 2; }
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
press() { xdotool keydown "$1"; sleep 0.08; xdotool keyup "$1"; sleep 0.25; }
shot() { xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %s\n" "$(ms)" "$1"; }
state() { "$MN" list 2>/dev/null | awk '$1 == "my-server" {print $2}'; }
running() { [ "$(state)" = running ]; }
stopped() { [ "$(state)" = stopped ]; }
joined() { running && "$MN" logs my-server -n 400 2>/dev/null | grep -q 'Steve joined the game'; }
server_pid() { pgrep -f "$repo/sim/minenest/java" | head -n 1; }
app_up() { pgrep -f 'apps/minenest.elf32' > /dev/null; }
cleanup() { "$MN" quit > /dev/null 2>&1; kill "$XV" 2>/dev/null; }

XENITHOS_WINDOW=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=dummy "$repo/linux/xenithos" 2> "$T/xenithos.log" &
XP=$!
timeout 20 xdotool search --sync --name XenithOS > /dev/null 2>&1
sleep 4.5
if ! kill -0 "$XP" 2>/dev/null; then
    echo "  FAIL  XenithOS stopped. Its log:"
    cat "$T/xenithos.log"
    cleanup
    exit 1
fi
timeout 20 xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1
shot home
echo "  $(ms) ms  starting MineNest from the home screen"
press Down
press Return
sleep 4
shot minenest
press Return                      # into the Server page: Start
press Return                      # start it
wait_for 40 joined                # the stand-in says a player joined about 5 s after it starts
sleep 2                           # for the page to show them
shot running
check "the server is running" running
check "a player joined" joined

echo "  $(ms) ms  leaving MineNest with the server running"
pid=$(server_pid)
press Escape                      # to the menu
press Escape                      # leave
wait_for 15 eval '! app_up'
sleep 1
shot home_again
check "the MineNest app has quit" "! app_up"
check "the server is still running (pid $pid)" "running && [ \"\$(server_pid)\" = \"$pid\" ]"
check "the server isn't XenithOS's child" "[ \"\$(ps -o ppid= -p $pid | tr -d ' ')\" != \"$XP\" ]"
check "the server isn't in XenithOS's session" "[ \"\$(ps -o sid= -p $pid | tr -d ' ')\" != \"\$(ps -o sid= -p $XP | tr -d ' ')\" ]"

echo "  $(ms) ms  opening MineNest again"
press Return
wait_for 15 app_up
sleep 3
shot picked_up
check "the same server is still running (pid $pid)" "[ \"\$(server_pid)\" = \"$pid\" ] && running"
press Return                      # into the Server page: Stop is selected
press Return                      # stop
wait_for 30 stopped
sleep 1.5
shot stopped
check "the server stopped" stopped
check "it said its world was saved" "\"$MN\" logs my-server -n 400 | grep -q 'All dimensions are saved'"
press Escape
press Escape
sleep 2
kill "$XP" 2>/dev/null; wait "$XP" 2>/dev/null

echo "  $(ms) ms  MineNest as a desktop app"
mkdir -p "$T/lib"
cp "$repo/linux/minenest" "$T/lib/minenest"
cp "$repo/apps/minenest/app.txt" "$T/lib/minenest.txt"
XENITHOS_DESKTOP=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=dummy "$T/lib/minenest" 2>> "$T/minenest-desktop.log" &
HP=$!
WID=$(timeout 20 xdotool search --sync --name '^MineNest$' 2>/dev/null | head -n 1)
WIDTH=0
if [ -n "$WID" ]; then eval "$(xdotool getwindowgeometry --shell "$WID")"; fi
sleep 3
shot desktop
check "its window is named MineNest" "[ -n \"$WID\" ]"
check "a window, not the whole screen (${WIDTH} of 1280)" "[ \"$WIDTH\" -lt 1280 ] && [ \"$WIDTH\" -ge 640 ]"
kill "$HP" 2>/dev/null; wait "$HP" 2>/dev/null

cleanup
echo "screenshots in $T/shots"
[ "$failed" = 0 ] && echo "all passed" || echo "FAILED"
exit "$failed"
