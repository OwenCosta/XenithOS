#!/bin/bash
# HostMC on a PC, in a virtual display: XenithOS starts HostMC inside its window, HostMC starts a
# server (the simulator's stand-in, sim/hostmc/java: nothing is downloaded), and then the part
# that only a real PC has: leaving HostMC must leave the server running, as a process of its own
# that HostMC isn't the parent of, and opening HostMC again must pick it up. Then HostMC stops it.
# Needs xvfb, xdotool, x11-apps, imagemagick and python3. Run after make -C linux.
repo="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/xos-hostmc
rm -rf "$T"
mkdir -p "$T/uda:/apps" "$T/uda:/xenith" "$T/shots" "$T/hostmc"
ln -s "$repo/linux/hostmc" "$T/uda:/apps/hostmc.elf32"
cp "$repo/apps/hostmc/app.txt" "$T/uda:/apps/hostmc.txt"
cp "$repo/art/thumbs/hostmc.ximg" "$T/uda:/apps/hostmc.ximg"
cp -r "$repo/sim/hostmc/data/." "$T/hostmc/"
echo "stand-in" > "$T/hostmc/server/server.jar"
export HOSTMC_JAVA="$repo/sim/hostmc/java"
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }

Xvfb :79 -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:79
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
press() { xdotool keydown "$1"; sleep 0.08; xdotool keyup "$1"; sleep 0.25; }
shot() { xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %s\n" "$(ms)" "$1"; }
server_pid() { grep '^pid=' "$T/hostmc/server/hostmc.pid" 2>/dev/null | cut -d= -f2; }
server_alive() { p=$(server_pid); [ -n "$p" ] && [ "$(readlink "/proc/$p/cwd" 2>/dev/null)" = "$(realpath "$T/hostmc/server")" ]; }

XENITHOS_WINDOW=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=dummy "$repo/linux/xenithos" 2> "$T/xenithos.log" &
XP=$!
sleep 4.5
xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1
shot home
echo "  $(ms) ms  starting HostMC from the home screen"
press Down
press Return
sleep 3
shot hostmc
press Return                      # into the Server page
press Return                      # Start: the EULA first
sleep 1
shot eula
press Return                      # I agree
sleep 9
shot running
check "the server is running" server_alive
check "players joined (console.log)" "grep -q 'Steve joined the game' '$T/hostmc/server/console.log'"

echo "  $(ms) ms  leaving HostMC with the server running"
pid=$(server_pid)
press Escape                      # to the menu
press Escape                      # leave: the first time, it says the server keeps running
sleep 1
shot keeps_running
press Return
sleep 3
shot home_again
check "HostMC has quit" "! pgrep -f 'apps/hostmc.elf32' > /dev/null"
check "the server is still running (pid $pid)" server_alive
check "the server isn't HostMC's child (parent $(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' '))" \
      "[ \"\$(ps -o ppid= -p $pid | tr -d ' ')\" != \"$XP\" ]"
check "the server isn't in XenithOS's session" "[ \"\$(ps -o sid= -p $pid | tr -d ' ')\" != \"\$(ps -o sid= -p $XP | tr -d ' ')\" ]"

echo "  $(ms) ms  opening HostMC again"
press Return
sleep 3
shot picked_up
check "HostMC found the same server (pid $pid) and didn't start another" "[ \"\$(server_pid)\" = \"$pid\" ] && server_alive"
press Return                      # into the Server page: Stop is selected
press Return                      # stop
sleep 4
shot stopped
check "the server stopped" "! server_alive"
check "it said its world was saved" "grep -q 'All dimensions are saved' '$T/hostmc/server/console.log'"
press Escape
press Escape
sleep 2
shot back_home

kill "$XP" 2>/dev/null; wait "$XP" 2>/dev/null
kill "$XV" 2>/dev/null; wait "$XV" 2>/dev/null
if server_alive; then echo "  (stopping the stand-in server left running, pid $(server_pid))"; kill "$(server_pid)"; failed=1; fi
echo "screenshots in $T/shots"
exit $failed
