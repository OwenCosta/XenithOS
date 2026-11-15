#!/bin/bash
# Console mode's own X server and window manager, in a virtual display (needs xvfb, xdotool,
# x11-apps, imagemagick and libsdl2-dev). Run after make -C linux.
#
# 1. XenithOS on the bare console starts its X server through startx: a stand-in startx runs
#    it in a 1920x1080 Xvfb, and the kiosk-starting marker must be gone once it's up.
# 2. It manages apps' windows: xclock (resizable) must fill the screen, a window that can't be
#    resized must be centred over XenithOS, and an app that takes a few seconds to open a window
#    must leave XenithOS's "Starting" screen moving meanwhile. Each app is closed the way
#    holding Guide does (XENITHOS_TEST_CLOSE), and XenithOS must have the screen again.
# 3. A startx that fails: the next start must note kiosk-failed and not try again.
repo="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/xos-console-test
# Display :84 must be free: a test left over on it would get the key presses and screenshots.
if [ -e /tmp/.X84-lock ] && [ -d "/proc/$(tr -d ' ' < /tmp/.X84-lock)" ]; then
    echo "display :84 is in use (PID $(tr -d ' ' < /tmp/.X84-lock)): stop that first"
    exit 1
fi
rm -f /tmp/.X84-lock
rm -rf "$T"
mkdir -p "$T/home/.config/xenithos" "$T/data/uda:/apps" "$T/data/uda:/xenith" "$T/bin" "$T/shots"
export HOME="$T/home"

# A window that can't be resized, and an app that waits before opening one
cat > "$T/fixed.c" <<'EOF'
#include <SDL.h>
int main(int argc, char **argv)
{
    SDL_Window *w; SDL_Renderer *r; int i;
    (void)argc; (void)argv;
    SDL_Init(SDL_INIT_VIDEO);
    w = SDL_CreateWindow("fixed", 50, 50, 640, 400, 0);
    r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    for (i = 0; i < 3000; i++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) return 0;
        SDL_SetRenderDrawColor(r, 220, 90, 40, 255);
        SDL_RenderClear(r);
        SDL_RenderPresent(r);
        SDL_Delay(16);
    }
    return 0;
}
EOF
# shellcheck disable=SC2046  # sdl2-config gives several words on purpose
cc "$T/fixed.c" $(sdl2-config --cflags --libs) -o "$T/bin/fixed" || exit 1
printf '#!/bin/bash\nsleep 3\nexec xclock -update 1\n' > "$T/bin/slow"
chmod +x "$T/bin/slow"
printf 'name=Clock\nexec=/usr/bin/xclock -update 1\n' > "$T/data/uda:/apps/a-clock.app"
printf 'name=Fixed\nexec=%s/bin/fixed\n' "$T" > "$T/data/uda:/apps/b-fixed.app"
printf 'name=Slow\nexec=%s/bin/slow\n' "$T" > "$T/data/uda:/apps/c-slow.app"

# The stand-in startx: XenithOS's own X server, as a 1920x1080 Xvfb on :84
cat > "$T/bin/startx" <<EOF
#!/bin/bash
echo "startx \$*" >> "$T/startx.log"
client="\$1"
Xvfb :84 -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
xv=\$!
echo \$xv > "$T/xvfb.pid"
sleep 1
DISPLAY=:84 "\$client"
kill \$xv
EOF
chmod +x "$T/bin/startx"

# Whatever happens, nothing this test started is left running.
cleanup() {
    for f in "$T/xenithos.pid" "$T/xvfb.pid"; do
        [ -f "$f" ] && kill "$(cat "$f")" 2> /dev/null
    done
}
trap cleanup EXIT
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
shot() { DISPLAY=:84 xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %-12s brightness %3s  windows: %s\n" "$(ms)" "$1" "$(convert "$T/shots/$1.png" -colorspace Gray -format '%[fx:int(mean*255)]' info:)" "$(DISPLAY=:84 xdotool search --onlyvisible --name . 2>/dev/null | while read -r w; do printf '%s %s ' "$(DISPLAY=:84 xdotool getwindowname "$w")" "$(DISPLAY=:84 xdotool getwindowgeometry "$w" | awk '/Position|Geometry/ {printf "%s ", $2}')"; done)"; }
press() { DISPLAY=:84 xdotool keydown "$1"; sleep 0.08; DISPLAY=:84 xdotool keyup "$1"; }

echo "== 1. bare console -> startx"
env -u DISPLAY -u WAYLAND_DISPLAY XENITHOS_SESSION=1 XENITHOS_STARTX="$T/bin/startx" XENITHOS_DATA="$T/data" \
    XENITHOS_TEST_CLOSE=7 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=kmsdrm "$repo/linux/xenithos" 2> "$T/xenithos.log" &
sleep 5
pgrep -f "^$repo/linux/xenithos" | head -1 > "$T/xenithos.pid"
shot home
echo "  startx was run as: $(cat "$T/startx.log" 2>/dev/null)"
echo "  kiosk-starting gone: $([ -e "$HOME/.config/xenithos/kiosk-starting" ] && echo NO || echo yes)"
echo "  keyboard focus: $(DISPLAY=:84 xdotool getwindowfocus getwindowname 2>/dev/null)"
press Down; sleep 0.5
for app in clock fixed slow; do
    echo "== 2. $app"
    press Return
    sleep 2; shot "${app}_2s"
    sleep 3; shot "${app}_5s"
    sleep 5; shot "${app}_back"
    press Right; sleep 0.5
done
cleanup
sleep 1.5

echo "== 3. a startx that fails"
printf '#!/bin/bash\necho "startx failed" >> %s/startx.log\nexit 1\n' "$T" > "$T/bin/startx"
rm -f "$HOME/.config/xenithos/kiosk-failed"
for run in 1 2; do
    env -u DISPLAY -u WAYLAND_DISPLAY XENITHOS_SESSION=1 XENITHOS_STARTX="$T/bin/startx" XENITHOS_DATA="$T/data" \
        SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen XENITHOS_SHOT="$T/shots/bare$run.png" XENITHOS_SHOT_FRAME=30 \
        "$repo/linux/xenithos" 2>> "$T/xenithos.log"
    echo "  run $run: marker $([ -e "$HOME/.config/xenithos/kiosk-starting" ] && echo present || echo gone), failed: $(cat "$HOME/.config/xenithos/kiosk-failed" 2>/dev/null | tr -d '\n')"
done
echo "  startx log: $(tr '\n' '|' < "$T/startx.log")"
echo "screenshots in $T/shots"
echo "log: $(grep -v '^$' "$T/xenithos.log" | tail -4 | tr '\n' ' ' | cut -c1-300)"
