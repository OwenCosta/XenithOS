#!/bin/bash
# HostMC on a PC, in a virtual display: XenithOS starts HostMC inside its window, HostMC starts a
# server (the simulator's stand-in, sim/hostmc/java: nothing is downloaded), and then the part
# that only a real PC has: leaving HostMC must leave the server running, as a process of its own
# that HostMC isn't the parent of, and opening HostMC again must pick it up. Then HostMC stops it.
# Then HostMC as a desktop app, as XenithOS Desktop's Apps menu opens it: its own window, clicks,
# typing into its text box and its console's command line, and the server outliving the window.
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
wait_for() {                     # $1: seconds, then a command that has to come true by then
    local n=$1
    shift
    for _ in $(seq 1 "$((n * 5))"); do "$@" && return 0; sleep 0.2; done
    return 1
}

# A display of its own: the first free number from :79, once it takes connections. (Not Xvfb's
# -displayfd, which fails where /tmp/.X11-unix is read-only, as under WSLg.) Its lock file has to
# name this Xvfb, so a display something else already serves is never used.
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
server_pid() { grep '^pid=' "$T/hostmc/server/hostmc.pid" 2>/dev/null | cut -d= -f2; }
server_alive() { p=$(server_pid); [ -n "$p" ] && [ "$(readlink "/proc/$p/cwd" 2>/dev/null)" = "$(realpath "$T/hostmc/server")" ]; }
joined() { server_alive && grep -q 'Steve joined the game' "$T/hostmc/server/console.log" 2>/dev/null; }
hostmc_up() { pgrep -f 'apps/hostmc.elf32' > /dev/null; }

XENITHOS_WINDOW=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=dummy "$repo/linux/xenithos" 2> "$T/xenithos.log" &
XP=$!
timeout 20 xdotool search --sync --name XenithOS > /dev/null 2>&1      # its window, then the start-up animation
sleep 4.5
if ! kill -0 "$XP" 2>/dev/null; then
    echo "  FAIL  XenithOS stopped. Its log:"
    cat "$T/xenithos.log"
    kill "$XV" 2>/dev/null
    exit 1
fi
timeout 20 xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1
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
wait_for 30 joined                # the stand-in says a player joined about 5 s after it starts
sleep 1.5                         # for HostMC's page to show them
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
wait_for 15 eval '! hostmc_up'
sleep 1
shot home_again
check "HostMC has quit" "! hostmc_up"
check "the server is still running (pid $pid)" server_alive
check "the server isn't HostMC's child (parent $(ps -o ppid= -p "$pid" 2>/dev/null | tr -d ' '))" \
      "[ \"\$(ps -o ppid= -p $pid | tr -d ' ')\" != \"$XP\" ]"
check "the server isn't in XenithOS's session" "[ \"\$(ps -o sid= -p $pid | tr -d ' ')\" != \"\$(ps -o sid= -p $XP | tr -d ' ')\" ]"

echo "  $(ms) ms  opening HostMC again"
press Return
wait_for 15 hostmc_up
sleep 2
shot picked_up
check "HostMC found the same server (pid $pid) and didn't start another" "[ \"\$(server_pid)\" = \"$pid\" ] && server_alive"
press Return                      # into the Server page: Stop is selected
press Return                      # stop
wait_for 20 eval '! server_alive'
sleep 1.5
shot stopped
check "the server stopped" "! server_alive"
check "it said its world was saved" "grep -q 'All dimensions are saved' '$T/hostmc/server/console.log'"
press Escape
press Escape
sleep 2
shot back_home
kill "$XP" 2>/dev/null; wait "$XP" 2>/dev/null

# HostMC as a desktop app (XenithOS Desktop's Apps menu starts it with XENITHOS_DESKTOP=1): a window
# of its own named after the app, which the mouse clicks and the keyboard types into. The window
# shows the 1280x720 picture scaled, so a click at picture point (x, y) goes to (x * W / 1280, y * H / 720).
echo "  $(ms) ms  HostMC as a desktop app"
mkdir -p "$T/lib"
cp "$repo/linux/hostmc" "$T/lib/hostmc"
cp "$repo/apps/hostmc/app.txt" "$T/lib/hostmc.txt"    # its name, as /usr/lib/xenithos/hostmc.txt
desktop_app() {
    WID= WIDTH=0 HEIGHT=0
    XENITHOS_DESKTOP=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=dummy "$T/lib/hostmc" 2>> "$T/hostmc-desktop.log" &
    HP=$!
    WID=$(timeout 20 xdotool search --sync --name '^HostMC$' 2>/dev/null | head -n 1)
    [ -n "$WID" ] || return 0
    xdotool windowfocus "$WID" > /dev/null 2>&1
    eval "$(xdotool getwindowgeometry --shell "$WID")"
    sleep 2                       # its first frames
}
at() { xdotool mousemove --window "$WID" $(( $1 * WIDTH / 1280 )) $(( $2 * HEIGHT / 720 )); sleep 0.15; }
click() { at "$1" "$2"; xdotool click 1; sleep 0.6; }
typed_motd() { grep -qx 'motd=Typed on a desktop' "$T/hostmc/server/server.properties" 2>/dev/null; }
said() { grep -q '\[Server\] Hello from xdotool' "$T/hostmc/server/console.log" 2>/dev/null; }
desktop_app
check "its window is named HostMC" "[ -n \"$WID\" ]"
check "a window, not the whole screen (${WIDTH}x${HEIGHT} of 1280x720)" "[ \"$WIDTH\" -lt 1280 ] && [ \"$WIDTH\" -ge 640 ]"
click 190 308                     # Settings, in the menu
click 500 160                     # the message of the day: the text box
shot desktop_text_box
xdotool type --delay 40 "Typed on a desktop"
xdotool key Return
wait_for 10 typed_motd
shot desktop_settings
check "the message typed in the text box is the server's (server.properties)" typed_motd
click 190 140                     # Server
click 450 270                     # Start the server
wait_for 30 joined
sleep 1
shot desktop_running
check "a click started the server" server_alive
click 190 196                     # Console: its command line takes the typing
sleep 0.5
xdotool type --delay 40 "say Hello from xdotool"
xdotool key Return
wait_for 10 said
shot desktop_console
check "a command typed on the console's command line reached the server" said
pid=$(server_pid)
kill -TERM "$HP"                  # as closing the window does (SDL's quit)
wait "$HP" 2>/dev/null
check "closing the window left the server running (pid $pid)" "[ \"\$(server_pid)\" = \"$pid\" ] && server_alive"
desktop_app
check "HostMC opened again found it" "[ \"\$(server_pid)\" = \"$pid\" ] && server_alive"
click 190 140                     # Server
click 450 270                     # Stop the server
wait_for 20 eval '! server_alive'
sleep 1
shot desktop_stopped
check "a click stopped it" "! server_alive"
xdotool key Escape
sleep 0.3
xdotool key Escape                # back to the menu, then close
wait_for 10 eval "! kill -0 $HP 2>/dev/null"
check "Esc from the menu closed HostMC" "! kill -0 $HP 2>/dev/null"
kill "$HP" 2>/dev/null; wait "$HP" 2>/dev/null

kill "$XV" 2>/dev/null; wait "$XV" 2>/dev/null
if server_alive; then echo "  (stopping the stand-in server left running, pid $(server_pid))"; kill "$(server_pid)"; failed=1; fi
if [ "$failed" != 0 ] && [ -s "$T/xenithos.log" ]; then echo "XenithOS's log (HostMC's too):"; tail -n 40 "$T/xenithos.log"; fi
if [ "$failed" != 0 ] && [ -s "$T/hostmc-desktop.log" ]; then echo "HostMC's log as a desktop app:"; tail -n 40 "$T/hostmc-desktop.log"; fi
echo "screenshots in $T/shots"
exit $failed
