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
# 6. The pointer moves with the controller (XENITHOS_TEST_STICK holds the left stick over).
# 7. A game window that makes itself full screen without asking (override-redirect, what GLFW
#    does for Minecraft when it sees no window manager): it must get the keyboard, and the
#    controller's keys must reach it. The window manager must also say it's there (EWMH).
# 8. A window that can't be resized asking for full screen (_NET_WM_STATE): it must fill it.
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

# A stand-in game window: "or" is override-redirect and fills the screen; "fs" is 640x400, can't
# be resized, and asks for full screen with _NET_WM_STATE. It prints the keys it gets, and its
# size whenever that changes (into XenithOS's app.log).
cat > "$T/xwin.c" <<'EOF'
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    const char *mode = argc > 1 ? argv[1] : "or", *title = argc > 2 ? argv[2] : "Minecraft-test";
    XSetWindowAttributes at;
    Window w;
    Atom type;
    int s, i, or_ = !strcmp(mode, "or"), fmt;
    unsigned long n = 0, left;
    unsigned char *p = NULL;
    if (!d) return 1;
    s = DefaultScreen(d);
    /* What GLFW looks for to decide there's a window manager (EWMH) */
    if (XGetWindowProperty(d, DefaultRootWindow(d), XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False), 0, 1, False,
                           XA_WINDOW, &type, &fmt, &n, &left, &p) == Success && p && n == 1)
        printf("window manager: yes\n");
    else
        printf("window manager: none\n");
    if (p) XFree(p);
    memset(&at, 0, sizeof at);
    at.override_redirect = or_;
    at.event_mask = KeyPressMask | StructureNotifyMask;
    at.background_pixel = 0x3366cc;
    w = XCreateWindow(d, DefaultRootWindow(d), 0, 0, or_ ? DisplayWidth(d, s) : 640, or_ ? DisplayHeight(d, s) : 400, 0,
                      CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWEventMask | CWBackPixel, &at);
    XStoreName(d, w, title);
    if (!or_) {
        XSizeHints h;
        memset(&h, 0, sizeof h);
        h.flags = PMinSize | PMaxSize;
        h.min_width = h.max_width = 640;
        h.min_height = h.max_height = 400;
        XSetWMNormalHints(d, w, &h);
    }
    XMapWindow(d, w);
    XFlush(d);
    if (!or_) {
        XEvent e;
        sleep(1);
        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = w;
        e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = 1;
        e.xclient.data.l[1] = (long)XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
        e.xclient.data.l[3] = 1;
        XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFlush(d);
    }
    for (i = 0; i < 6000; i++) {
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
            if (e.type == KeyPress) printf("key %s\n", XKeysymToString(XLookupKeysym(&e.xkey, 0)));
            if (e.type == ConfigureNotify) printf("size %dx%d\n", e.xconfigure.width, e.xconfigure.height);
            fflush(stdout);
        }
        usleep(10000);
    }
    return 0;
}
EOF
cc "$T/xwin.c" -lX11 -o "$T/bin/xwin" || exit 1
printf 'name=Wide game\nexec=%s/bin/xwin or Minecraft-or\npointer=1\ngame=Minecraft\n' "$T" > "$T/data/uda:/apps/d-wide.app"
printf 'name=Windowed game\nexec=%s/bin/xwin fs Windowed-fs\n' "$T" > "$T/data/uda:/apps/e-windowed.app"

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
echo "== 7. a game window that makes itself full screen (override-redirect)"
for i in 1 2 3; do press Right; done   # Point, played last, is first: Clock, Craft, then Wide game
press Return
sleep 3; shot wide
echo "  the game sees: $(grep -m1 'window manager' "$T/data/uda:/xenith/app.log")"
echo "  presses of D the game got: $(grep -c '^key d' "$T/data/uda:/xenith/app.log")"
sleep 1.5
for i in 1 2 3 4 5; do press Down; done
press Return
sleep 3; shot wide_closed
echo "== 8. a window that asks for full screen"
for i in 1 2 3 4; do press Right; done
press Return
sleep 3; shot windowed
echo "  its sizes: $(grep '^size' "$T/data/uda:/xenith/app.log" | tr '\n' ' ')"
sleep 1.5
for i in 1 2 3 4 5; do press Down; done
press Return
sleep 3; shot windowed_closed
cleanup
echo "screenshots in $T/shots"
echo "log: $(grep -v '^$' "$T/xenithos.log" | tail -6 | tr '\n' ' ' | cut -c1-400)"
