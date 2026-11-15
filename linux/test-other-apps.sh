#!/bin/bash
# Other programs from XenithOS on a PC (Flathub apps, Steam games), in a virtual display, with
# stand-ins for Steam, cage and Flatpak: nothing real is installed or started.
#
#   linux/test-other-apps.sh            desktop mode: XenithOS's window steps aside
#   linux/test-other-apps.sh console    console mode: XenithOS lets go of the screen for cage
#
# It makes a Steam library with one game, checks the game shows up on the home screen with
# Steam's picture, starts it, and checks that Steam is closed again once the game has ended
# (the stand-in Steam says so in registry.vdf, as the real one does). Then an app whose
# Flatpak fails straight away, to check XenithOS says why. Needs xvfb, xdotool, x11-apps,
# imagemagick and python3-pil. Run after make -C linux.
repo="$(cd "$(dirname "$0")/.." && pwd)"
MODE=${1:-desktop}
T=/tmp/xos-other-$MODE
rm -rf "$T"
mkdir -p "$T/home" "$T/data/uda:/apps" "$T/data/uda:/xenith" "$T/bin" "$T/shots"
export HOME="$T/home"

# A Steam library: Counter-Strike 2 installed, plus two of Steam's own tools (not games).
S="$HOME/.local/share/Steam"
mkdir -p "$S/steamapps" "$S/appcache/librarycache/730"
printf '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"apps"\n\t\t{\n\t\t\t"730"\t\t"1"\n\t\t}\n\t}\n}\n' "$S" > "$S/steamapps/libraryfolders.vdf"
mf() { printf '"AppState"\n{\n\t"appid"\t\t"%s"\n\t"name"\t\t"%s"\n\t"StateFlags"\t\t"%s"\n\t"UserConfig"\n\t{\n\t\t"name"\t\t"wrong"\n\t}\n}\n' "$1" "$2" "$3" > "$S/steamapps/appmanifest_$1.acf"; }
mf 730 "Counter-Strike 2" 4
mf 228980 "Steamworks Common Redistributables" 4
mf 1493710 "Proton Experimental" 4
mf 999 "Half Downloaded" 1026
python3 -I -c 'from PIL import Image, ImageDraw
im = Image.new("RGB", (460, 215), (200, 120, 30)); d = ImageDraw.Draw(im)
d.rectangle([150, 40, 310, 175], fill=(20, 30, 60)); im.save("'"$S"'/appcache/librarycache/730/header.jpg", quality=90)'

# Stand-ins. Steam: notes how it was started, says the game runs for 3 s, then that it has
# ended, then stays open (like Steam does) until it's asked to close.
cat > "$T/bin/steam" <<EOF
#!/bin/bash
echo "started: \$*" >> "$T/steam.log"
trap 'echo "asked to close" >> "$T/steam.log"; exit 0' TERM
mkdir -p "$HOME/.steam"
reg() { printf '"Registry"\n{\n\t"HKCU"\n\t{\n\t\t"Software"\n\t\t{\n\t\t\t"Valve"\n\t\t\t{\n\t\t\t\t"Steam"\n\t\t\t\t{\n\t\t\t\t\t"RunningAppID"\t\t"%s"\n\t\t\t\t\t"apps"\n\t\t\t\t\t{\n\t\t\t\t\t\t"730"\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\t"Running"\t\t"%s"\n\t\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n' "\$1" "\$2" > "$HOME/.steam/registry.vdf"; }
reg 730 1
sleep 3
reg 0 0
echo "game ended" >> "$T/steam.log"
sleep 60 & wait
echo "still open after 60 s" >> "$T/steam.log"
EOF
cat > "$T/bin/cage" <<EOF
#!/bin/bash
echo "cage: \$*" >> "$T/cage.log"
while [ "\$#" -gt 0 ] && [ "\$1" != "--" ]; do shift; done
shift
exec "\$@"
EOF
cat > "$T/bin/flatpak" <<EOF
#!/bin/bash
echo "flatpak: \$*" >> "$T/flatpak.log"
echo "error: app/\$2/x86_64/master not installed" >&2
exit 1
EOF
chmod +x "$T/bin/"*
# An app Flatpak says is installed (but fails to start), and one it says isn't (its tile goes).
printf 'name=Broken App\nflatpak=org.example.Broken\nargs=\n' > "$T/data/uda:/apps/broken.app"
mkdir -p "$HOME/.local/share/flatpak/app/org.example.Broken/x86_64/stable/active"
ln -s x86_64/stable "$HOME/.local/share/flatpak/app/org.example.Broken/current"
printf 'name=Gone App\nflatpak=org.example.Gone\nargs=\n' > "$T/data/uda:/apps/gone.app"

Xvfb :78 -screen 0 1280x720x24 -nolisten tcp > /dev/null 2>&1 &
XV=$!
sleep 1
export DISPLAY=:78
start=$(date +%s%N)
ms() { echo $(( ($(date +%s%N) - start) / 1000000 )); }
press() { xdotool keydown "$1"; sleep 0.08; xdotool keyup "$1"; }
shot() { xwd -root -silent | convert xwd:- "$T/shots/$1.png"; printf "%6d ms  %-14s brightness %s\n" "$(ms)" "$1" "$(convert "$T/shots/$1.png" -colorspace Gray -format '%[fx:int(mean*255)]' info:)"; }
if [ "$MODE" = console ]; then
    export XENITHOS_FULLSCREEN=1 XENITHOS_CAGE="$T/bin/cage"
else
    export XENITHOS_WINDOW=1
fi
XENITHOS_DATA="$T/data" XENITHOS_STEAM="$T/bin/steam" XENITHOS_FLATPAK="$T/bin/flatpak" SDL_AUDIODRIVER=dummy \
    "$repo/linux/xenithos" 2> "$T/xenithos.log" &
sleep 4.5
shot home
echo "  steam-730.app: $(tr '\n' ' ' < "$T/data/uda:/apps/steam-730.app" 2>/dev/null || echo MISSING)"
echo "  picture: $(stat -c '%s bytes' "$T/data/uda:/apps/steam-730.ximg" 2>/dev/null || echo MISSING)"
others=""
for f in "$T/data/uda:/apps"/steam-*.app; do [ "$(basename "$f")" = steam-730.app ] || others="$others $(basename "$f")"; done
echo "  other games listed (should be none):$others"
echo "  gone.app removed: $([ -e "$T/data/uda:/apps/gone.app" ] && echo NO || echo yes)"
xdotool search --sync --name XenithOS windowfocus > /dev/null 2>&1
press Down; sleep 0.5
shot app_row
# The row is in the order the folder lists them: find Counter-Strike 2 by trying each.
echo "  $(ms) ms  starting the selected app"
press Return
sleep 1.5; shot starting
for _ in $(seq 1 30); do
    sleep 0.5
    if grep -q "asked to close" "$T/steam.log" 2>/dev/null || grep -q "flatpak:" "$T/flatpak.log" 2>/dev/null; then break; fi
done
sleep 2.5; shot after
echo "  steam.log: $(tr '\n' '|' < "$T/steam.log" 2>/dev/null)"
echo "  cage.log: $(tr '\n' '|' < "$T/cage.log" 2>/dev/null)"
echo "  flatpak.log: $(tr '\n' '|' < "$T/flatpak.log" 2>/dev/null)"
press Escape; sleep 0.6
press Right; sleep 0.6
echo "  $(ms) ms  starting the next app"
press Return
for _ in $(seq 1 30); do
    sleep 0.5
    if grep -q "asked to close" "$T/steam.log" 2>/dev/null && grep -q "flatpak:" "$T/flatpak.log" 2>/dev/null; then break; fi
done
sleep 2.5; shot after2
echo "  steam.log: $(tr '\n' '|' < "$T/steam.log" 2>/dev/null)"
echo "  cage.log: $(tr '\n' '|' < "$T/cage.log" 2>/dev/null)"
echo "  flatpak.log: $(tr '\n' '|' < "$T/flatpak.log" 2>/dev/null)"
echo "  xenithos still running: $(pgrep -f "$repo/linux/xenithos" > /dev/null && echo yes || echo NO)"
XP=$(pgrep -f "$repo/linux/xenithos" | head -1)
[ -n "$XP" ] && kill "$XP"
kill "$XV" 2>/dev/null; wait "$XV" 2>/dev/null
echo "screenshots in $T/shots"
echo "log: $(tr '\n' ' ' < "$T/xenithos.log" | cut -c1-400)"
