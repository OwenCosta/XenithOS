#!/bin/bash
# The guide in console mode, in a virtual display (needs xvfb, xdotool, x11-apps and
# imagemagick). Run after make -C linux.
#
# 1. An app opens over XenithOS. XENITHOS_TEST_GUIDE acts out tapping the Xbox button 4 seconds
#    in: the panel must come up in front, over a picture of the app.
# 2. Return home: XenithOS's home screen in front, the app still running behind it.
# 3. Choosing it again goes back to it (and the guide comes up again, from the test setting).
# 4. Close game in the guide: the app goes, and XenithOS has the screen.
# 5. A launcher with a game in it ("game=" in its .app, like Prism Launcher and Minecraft): the
#    controller is the pointer until a window titled like the game is in front, then it plays
#    the game as a keyboard and mouse.
repo="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/xos-guide-test
if [ -e /tmp/.X85-lock ] && [ -d "/proc/$(tr -d ' ' < /tmp/.X85-lock)" ]; then
    echo "display :85 is in use (PID $(tr -d ' ' < /tmp/.X85-lock)): stop that first"
    exit 1
fi
rm -f /tmp/.X85-lock
rm -rf "$T"
mkdir -p "$T/home/.config/xenithos" "$T/data/uda:/apps" "$T/data/uda:/xenith" "$T/bin" "$T/shots"
export HOME="$T/home"

printf 'name=Clock\nexec=/usr/bin/xclock -update 1\n' > "$T/data/uda:/apps/a-clock.app"
printf 'name=Craft\nexec=/usr/bin/xclock -update 1 -title Minecraft-test\npointer=1\ngame=Minecraft\n' > "$T/data/uda:/apps/b-craft.app"
printf 'name=Point\nexec=/usr/bin/xclock -update 1\npointer=1\n' > "$T/data/uda:/apps/c-point.app"

cat > "$T/bin/startx" <<EOF
#!/bin/bash
client="\$1"
Xvfb :85 -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
xv=\$!
echo \$xv > "$T/xvfb.pid"
sleep 1
DISPLAY=:85 "\$client"
kill \$xv
EOF
chmod +x "$T/bin/startx"

cleanup() {
    for f in "$T/xenithos.pid" "$T/xvfb.pid"; do
        [ -f "$f" ] && kill "$(cat "$f")" 2> /dev/null
    done
}
trap cleanup EXIT
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
shot() { DISPLAY=:85 xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %-14s focus: %-16s clocks running: %s\n" "$(ms)" "$1" "$(DISPLAY=:85 xdotool getwindowfocus getwindowname 2>/dev/null)" "$(pgrep -c -x xclock)"; }
press() { DISPLAY=:85 xdotool keydown "$1"; sleep 0.08; DISPLAY=:85 xdotool keyup "$1"; sleep 0.15; }

env -u DISPLAY -u WAYLAND_DISPLAY XENITHOS_SESSION=1 XENITHOS_STARTX="$T/bin/startx" XENITHOS_DATA="$T/data" \
    XENITHOS_TEST_GUIDE=4 XENITHOS_TEST_STICK=60,0 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=kmsdrm \
    "$repo/linux/xenithos" 2> "$T/xenithos.log" &
sleep 6
pgrep -f "^$repo/linux/xenithos" | head -1 > "$T/xenithos.pid"
shot home
press Down
echo "== 1. the guide over an app"
press Return
sleep 2.5; shot clock
sleep 3; shot guide
echo "== 2. Return home"
press Down
press Return
sleep 1.5; shot home_running
echo "== 3. back to it"
press Return
sleep 1.5; shot guide_again
echo "== 4. Close game"
for i in 1 2 3 4 5; do press Down; done
press Return
sleep 3; shot closed
echo "== 5. a launcher's game window"
press Right
press Return
sleep 3.5; shot craft
grep "the controller" "$T/xenithos.log" | sed 's/^/  log: /'
sleep 1.5
for i in 1 2 3 4 5; do press Down; done
press Return
sleep 3; shot craft_closed
echo "== 6. the controller as the pointer (XENITHOS_TEST_STICK holds the left stick right)"
press Right      # Craft, played last, is first now: Clock, then Point
press Right
press Return
DISPLAY=:85 xdotool mousemove 200 540
sleep 1.5; a=$(DISPLAY=:85 xdotool getmouselocation | cut -d' ' -f1)
sleep 1.5; b=$(DISPLAY=:85 xdotool getmouselocation | cut -d' ' -f1)
echo "  pointer went from $a to $b"
sleep 2
for i in 1 2 3 4 5; do press Down; done
press Return
sleep 3; shot point_closed
cleanup
echo "screenshots in $T/shots"
echo "log: $(grep -v '^$' "$T/xenithos.log" | tail -6 | tr '\n' ' ' | cut -c1-400)"
