#!/bin/bash
# Achievements, end to end, against a XenithOS Cloud of its own on this machine (a temporary state
# folder, with stand-ins for yt-dlp and ffmpeg; its requests to anywhere else go to a closed port,
# so nothing leaves this machine):
#   1. An Xbox that earned DOOM achievements offline (they wait on its stick, with Tube's count of
#      videos at 9) starts XenithOS, which sends them. Another profile's stay waiting.
#   2. Tube on that Xbox: a video started and watched to the end, the tenth.
#   3. HostMC on a PC with the stand-in Minecraft server: started, a player joining, a backup.
#   4. XenithOS shows them: the count on Profile, the Achievements screen, "just earned" on Cloud.
# Each step is checked on the stick and through XenithOS Cloud's API. VOLT's and DOOM's own
# moments aren't acted out (a high score can't be scripted, and there's no DOOM game data here).
# Screenshots go to sim/out/ach-*.
#
#   sim/social/test-achievements.sh   after build.sh (for usb/); needs python3
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/xenith-achievements-test
PORT=${XSIM_CLOUD_PORT:-18797}
SERVER=127.0.0.1:$PORT
XBOX="$HOME/.xsim/ach-xbox/uda:"
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }
api() { python3 -I "$repo/sim/social/cloud.py" "$1" "$SERVER" "${@:2}"; }
has() { api get "/v1/players/$1/achievements" | cut -d'|' -f1 | sort | tr '\n' ' '; }
shot() { [ -s "$repo/sim/out/$1.png" ]; }

[ -d "$repo/usb" ] || { echo "needs usb/ (build.sh makes it)"; exit 2; }
rm -rf "$T"
mkdir -p "$T/state" "$T/store" "$T/web" "$T/xbox/xenith"

# Stand-ins for yt-dlp (a search with two videos, and a video's link) and ffmpeg (a stream of one
# picture, which ends at once)
cat > "$T/yt-dlp" <<'EOF'
#!/usr/bin/env python3
import json, sys, time
if "--flat-playlist" in sys.argv:
    print(json.dumps({"entries": [{"id": "abcdefghijk", "title": "A test video", "channel": "XenithOS", "duration": 3},
                                  {"id": "bcdefghijkl", "title": "Another one", "channel": "XenithOS", "duration": 3}]}))
else:
    print(json.dumps({"title": "A test video", "duration": 3, "url": "http://example.invalid/v?expire=%d" % (time.time() + 7200),
                      "vcodec": "avc1", "acodec": "mp4a"}))
EOF
cat > "$T/ffmpeg" <<'EOF'
#!/usr/bin/env python3
import sys
sys.stdout.buffer.write(b"RIFF\0\0\0\0AVI LIST\0\0\0\0movi00dc\4\0\0\0\xff\xd8\xff\xd9")
EOF
chmod +x "$T/yt-dlp" "$T/ffmpeg"

http_proxy=http://127.0.0.1:9 https_proxy=http://127.0.0.1:9 no_proxy= \
XENITH_PORT=$PORT XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" \
XENITH_YTDLP="$T/yt-dlp" XENITH_FFMPEG="$T/ffmpeg" \
    python3 "$repo/server/xenith_cloud.py" > "$T/cloud.log" 2>&1 &
CLOUD=$!
trap 'kill $CLOUD 2>/dev/null' EXIT
api wait || { echo "XenithOS Cloud didn't start:"; cat "$T/cloud.log"; exit 2; }
api profile Winner 1470 "$T/winner.txt" > /dev/null && RIVAL=$(api profile Rival 2580 "$T/rival.txt") \
    || { echo "couldn't make the profiles"; exit 2; }
# What an Xbox that played DOOM offline has waiting, and someone else's
printf '# Written by XenithOS\nwinner|doom-secret|0\nwinner|doom-exit|0\nwinner|#tube-videos|9\nsomeone|volt-100|0\n' \
    > "$T/xbox/xenith/achievements.txt"

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

echo "== 1. An Xbox with DOOM achievements earned offline starts XenithOS"
sim ach-xbox saves_boot.txt XSIM_PROFILE="$T/winner.txt" XSIM_UDA="$T/xbox"
check "XenithOS Cloud has them" '[ "$(has winner)" = "doom-exit doom-secret " ]'
check "marked as sent on the stick, someone else's still waiting" \
    'grep -qx "winner|doom-secret|1" "$XBOX/xenith/achievements.txt" && grep -qx "someone|volt-100|0" "$XBOX/xenith/achievements.txt"'

echo "== 2. Tube on the same Xbox: a video, to the end, the tenth"
sim ach-xbox tube_achieve.txt XSIM_BIN=xtube XSIM_THREAD=1 XSIM_KEEP=1
check "tube-first, tube-end and tube-ten" '[ "$(has winner)" = "doom-exit doom-secret tube-end tube-first tube-ten " ]'
check "the count is kept on the stick" 'grep -qx "winner|#tube-videos|10" "$XBOX/xenith/achievements.txt"'
check "screenshot" 'shot ach-xbox/ta_earned'

echo "== 3. HostMC on a PC: the server started, a player joining, a backup"
sim ach-pc hostmc.txt XSIM_PROFILE="$T/winner.txt" XSIM_BIN=xhostmc XSIM_KIND=pc XSIM_THREAD=1 XSIM_HOSTMC=1 \
    XSIM_TYPE="say Hello from HostMC|"
check "hostmc-start, hostmc-join and hostmc-backup" \
    'has winner | grep -q "hostmc-backup hostmc-join hostmc-start "'
check "not hostmc-online (its Internet was off)" '! has winner | grep -q hostmc-online'

echo "== 4. XenithOS shows them"
api post /v1/achievements/earn "token=$RIVAL" ids=volt-100 > /dev/null
sim ach-xbox ach_screens.txt XSIM_KEEP=1
check "the counts: Winner 8, the set says Rival has volt-100" \
    '[ "$(api get /v1/players/winner | grep ^achievements=)" = "achievements=8" ] && api get /v1/achievements | grep -q "^volt-100|volt|Charged up|.*|1$"'
check "just earned, newest first" 'api get /v1/achievements/recent | head -1 | grep -q "^Rival|.*|volt-100|"'
check "screenshots" 'shot ach-xbox/ac_profile && shot ach-xbox/ac_achievements && shot ach-xbox/ac_cloud'

if [ "$failed" = 0 ]; then echo "all passed"; else echo "something failed: logs in $T"; fi
exit "$failed"
