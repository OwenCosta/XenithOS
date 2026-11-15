#!/bin/bash
# XenithOS for PCs in a virtual display: boot, start an app, leave it, and check the screen
# never goes black and the sound still works afterwards. Needs xvfb, xdotool, x11-apps and
# imagemagick. Usage: linux/test-in-window.sh [volt|doom] (after make -C linux).
repo="$(cd "$(dirname "$0")/.." && pwd)"
APP=${1:-volt}
T=/tmp/xos-test-$APP
rm -rf "$T"; mkdir -p "$T/uda:/apps" "$T/uda:/xenith" "$T/uda:/data" "$T/shots"
ln -s "$repo/linux/$APP" "$T/uda:/apps/$APP.elf32"
printf 'name=%s\nversion=test\n' "$(echo "$APP" | tr a-z A-Z)" > "$T/uda:/apps/$APP.txt"
[ "$APP" = doom ] && cp "${XENITHOS_WAD:-$HOME/.xsim/freedoom1.wad}" "$T/uda:/data/"
Xvfb :77 -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:77
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
XENITHOS_WINDOW=1 XENITHOS_DATA="$T" SDL_AUDIODRIVER=disk SDL_AUDIO_DRIVER=disk \
  SDL_DISKAUDIOFILE="$T/audio.raw" SDL_AUDIO_DISK_OUTPUT_FILE="$T/audio.raw" "$repo/linux/xenithos" 2> "$T/xenithos.log" &
XP=$!
press() { xdotool keydown "$1"; sleep 0.08; xdotool keyup "$1"; }
shot() { xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %-10s brightness %s\n" "$(ms)" "$1" "$(convert "$T/shots/$1.png" -colorspace Gray -format '%[fx:int(mean*255)]' info:)"; }
sleep 0.5; shot boot1
sleep 1.0; shot boot2
sleep 2.8; shot home
xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1
press Down; sleep 0.4
echo "  $(ms) ms  pressed A on $APP"; press Return
for i in 1 2 3 4 5 6 7 8; do sleep 0.1; shot "in$i"; done
sleep 2.5; shot app
[ "$APP" = doom ] && { echo "  $(ms) ms  Esc in DOOM (menu, with its sound)"; press Escape; sleep 1.2; shot doommenu; press Escape; sleep 0.8; }
echo "  $(ms) ms  pressed Back to leave $APP"; if [ "$APP" = doom ]; then xdotool keydown Tab; sleep 1.6; xdotool keyup Tab; else press Tab; fi
[ "$APP" = volt ] && press Escape
for i in 1 2 3 4 5 6 7 8; do sleep 0.1; shot "out$i"; done
sleep 1.0; shot back
echo "  $(ms) ms  moving across the home screen (each move has a sound)"
press Up; sleep 0.5
for i in 1 2 3; do press Right; sleep 0.45; done
press Left; sleep 1.2
echo "  $(ms) ms  done; xenithos still running: $(kill -0 $XP 2>/dev/null && echo yes || echo NO)"
kill $XP 2>/dev/null; wait $XP 2>/dev/null
kill $XV 2>/dev/null; wait $XV 2>/dev/null
f="$T/audio.raw"; [ -s "$f" ] || f=$(ls "$T"/*.raw "$T"/sdlaudio* 2>/dev/null | head -1)
python3 -I - "$f" <<'PY'
import array, sys
path = sys.argv[1] if len(sys.argv) > 1 else ""
try:
    raw = open(path, "rb").read()
except OSError:
    print("no sound file was written"); sys.exit()
s = array.array("h", raw[:len(raw) // 4 * 4])
rate, win = 48000, 12000                 # quarter seconds
print(f"sound file: {len(s) // 2 / rate:.1f} s")
line = []
for i in range(0, len(s) // 2, win):
    seg = s[i * 2:(i + win) * 2]
    rms = (sum(v * v for v in seg[::8]) / max(1, len(seg[::8]))) ** 0.5
    line.append("#" if rms > 300 else "+" if rms > 40 else ".")
print("loudness every 0.25 s (# loud, + quiet, . silent):")
print("".join(line))
PY
echo "screenshots in $T/shots"
echo "log: $(tr '\n' ' ' < "$T/xenithos.log" | cut -c1-300)"
