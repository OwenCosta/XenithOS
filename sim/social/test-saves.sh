#!/bin/bash
# Cloud saves between two machines signed in to one profile, end to end, against a XenithOS Cloud
# of its own on this machine (a temporary state folder; nothing goes anywhere else). Simulator runs
# stand in for an Xbox and a PC, each with a stick of its own that's kept between its runs
# (XSIM_KEEP), and DOOM's saves are files with a save's name in the first 24 bytes like DOOM's:
#   1. The Xbox starts with DOOM saves from before profiles had their own folder: they're copied
#      into the profile's folder and go up, with DOOM's and XenithOS's settings.
#   2. A PC signed in to the same profile gets them, and turns Interface sounds off.
#   3. Slot 1 changes on both; the PC's reaches XenithOS Cloud first.
#   4. The Xbox starts: the clash waits for DOOM, which asks which to keep; the Xbox's is kept.
#      The PC's Interface sounds setting comes down too.
#   5. The PC starts again and gets the Xbox's save.
#   6. Slot 1 is deleted from XenithOS Cloud, on the Xbox; 7. the PC keeps its copy without
#      sending it back.
# Each step is checked on the sticks and through XenithOS Cloud's API. Screenshots go to
# sim/out/saves-*.
#
#   sim/social/test-saves.sh        after build.sh (for usb/); needs python3
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/xenith-saves-test
PORT=${XSIM_CLOUD_PORT:-18796}
SERVER=127.0.0.1:$PORT
XBOX="$HOME/.xsim/saves-xbox/uda:"
PC="$HOME/.xsim/saves-pc/uda:"
SLOT=doom/profiles/saver/.savegame/doomsav0.dsg
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }
api() { python3 -I "$repo/sim/social/cloud.py" "$1" "$SERVER" "${@:2}"; }
listing() { api post /v1/saves "token=$TOKEN"; }
row() { listing | grep "^file=$1|$2|"; }                 # file=app|name|rev|size|sha256|age|from|label
field() { row "$1" "$2" | cut -d'|' -f"$3"; }
sha() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
shot() { [ -s "$repo/sim/out/$1.png" ]; }
dsg() {                          # a stand-in DOOM save: its name in 24 bytes, a version, then some game
    python3 -c 'import sys; n = sys.argv[1].encode(); sys.stdout.buffer.write(n + bytes(24 - len(n)) + b"version 109" + sys.argv[2].encode() * 2000)' \
        "$1" "$2" > "$3"
}

[ -d "$repo/usb" ] || { echo "needs usb/ (build.sh makes it)"; exit 2; }
rm -rf "$T"
mkdir -p "$T/state" "$T/store" "$T/web" "$T/guest/doom/.savegame"
XENITH_PORT=$PORT XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" \
    python3 "$repo/server/xenith_cloud.py" > "$T/cloud.log" 2>&1 &
CLOUD=$!
trap 'kill $CLOUD 2>/dev/null' EXIT
api wait || { echo "XenithOS Cloud didn't start:"; cat "$T/cloud.log"; exit 2; }
TOKEN=$(api profile Saver 8642 "$T/saver.txt") || { echo "couldn't make the profile"; exit 2; }
dsg "BEFORE BOSS" "E1M8" "$T/guest/doom/.savegame/doomsav0.dsg"
printf 'mouse_sensitivity 5\nsfx_volume 8\nmusic_volume 8\n' > "$T/guest/doom/default.cfg"

sim() {                          # $1: its name (sim/out/<name>), $2: the script, then VAR=value
    local name=$1 script=$2
    shift 2
    env "$@" "$repo/sim/run.sh" "$repo/sim/scripts/$script" "$name" "$SERVER" > "$T/$name-$script.log" 2>&1
    echo "  ($script: the simulator finished with $?)"
    if grep -qE "Sanitizer|runtime error" "$T/$name-$script.log"; then
        echo "  FAIL  $name: the sanitizers found something (see $T/$name-$script.log)"
        failed=1
    fi
}

echo "== 1. The Xbox starts, with DOOM saves from before profiles had their own folder"
sim saves-xbox saves_first.txt XSIM_PROFILE="$T/saver.txt" XSIM_UDA="$T/guest"
check "slot 1 is on XenithOS Cloud, with its name and where it came from" \
    'row doom doomsav0.dsg | grep -q "|1|.*|Simulator|BEFORE BOSS$"'
check "and DOOM's settings, and XenithOS's" '[ "$(field doom default.cfg 3)" = 1 ] && [ "$(field xenithos settings.txt 3)" = 1 ]'
check "copied into the profile's folder, the old one left as it was" \
    '[ "$(sha "$XBOX/$SLOT")" = "$(sha "$T/guest/doom/.savegame/doomsav0.dsg")" ] && [ -f "$XBOX/doom/.savegame/doomsav0.dsg" ]'
check "the same bytes went up" '[ "$(field doom doomsav0.dsg 5)" = "$(sha "$XBOX/$SLOT")" ]'
check "screenshot" 'shot saves-xbox/sa_cloud_saves'

echo "== 2. A PC signed in to the same profile gets them, and turns Interface sounds off"
sim saves-pc saves_second.txt XSIM_PROFILE="$T/saver.txt" XSIM_KIND=pc
check "slot 1 came down, the same" '[ -n "$(sha "$PC/$SLOT")" ] && [ "$(sha "$PC/$SLOT")" = "$(sha "$XBOX/$SLOT")" ]'
check "and DOOM's settings" 'cmp -s "$PC/doom/profiles/saver/default.cfg" "$XBOX/doom/profiles/saver/default.cfg"'
check "Interface sounds went up, off" 'api post /v1/saves/get "token=$TOKEN" app=xenithos name=settings.txt | grep -qx "ui_sounds=0"'

echo "== 3. Slot 1 changes on both; the PC's reaches XenithOS Cloud first"
dsg "AFTER BOSS" "E2M1" "$XBOX/$SLOT"
dsg "ON PC" "E1M9" "$PC/$SLOT"
sim saves-pc saves_boot.txt XSIM_KIND=pc XSIM_KEEP=1
check "the PC's went up" 'row doom doomsav0.dsg | grep -q "|2|.*|ON PC$"'

echo "== 4. The Xbox starts: the clash waits for DOOM, which asks; the Xbox's is kept"
sim saves-xbox saves_clash.txt XSIM_KEEP=1
check "the Xbox's slot 1 is on XenithOS Cloud now" \
    'row doom doomsav0.dsg | grep -q "|3|.*|AFTER BOSS$" && [ "$(field doom doomsav0.dsg 5)" = "$(sha "$XBOX/$SLOT")" ]'
check "the PC's Interface sounds setting came to the Xbox" 'grep -qx "ui_sounds=0" "$XBOX/xenith/settings.txt"'
check "screenshots" 'shot saves-xbox/sc_toast && shot saves-xbox/sc_which'

echo "== 5. The PC starts again and gets the Xbox's save"
sim saves-pc saves_boot.txt XSIM_KIND=pc XSIM_KEEP=1
check "the PC has the Xbox's slot 1" '[ "$(sha "$PC/$SLOT")" = "$(sha "$XBOX/$SLOT")" ]'

echo "== 6. Slot 1 is deleted from XenithOS Cloud, on the Xbox"
sim saves-xbox saves_delete.txt XSIM_KEEP=1
check "gone from XenithOS Cloud" 'listing | grep -q "^gone=doom|doomsav0.dsg|4|" && ! row doom doomsav0.dsg > /dev/null'
check "the Xbox keeps its copy" '[ -f "$XBOX/$SLOT" ]'
check "screenshot" 'shot saves-xbox/sd_deleted'

echo "== 7. The PC starts again: it keeps its copy, and doesn't send it back"
sim saves-pc saves_boot.txt XSIM_KIND=pc XSIM_KEEP=1
check "still gone from XenithOS Cloud" 'listing | grep -q "^gone=doom|doomsav0.dsg|4|" && ! row doom doomsav0.dsg > /dev/null'
check "the PC keeps its copy" '[ -f "$PC/$SLOT" ]'

if [ "$failed" = 0 ]; then echo "all passed"; else echo "something failed: logs in $T"; fi
exit "$failed"
