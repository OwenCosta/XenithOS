#!/bin/bash
# Friends between two consoles, end to end, against a XenithOS Cloud of its own on this machine
# (a temporary state folder; nothing goes anywhere else). Two simulator runs with different
# profiles stand in for two consoles:
#   1. Alice asks Bob Two by his gamertag (typed on a keyboard), in Friends.
#   2. Bob Two gets the request as a toast at start-up, and says yes in Friends.
#   3. His HostMC server can be reached from the internet, and he plays Minecraft on a PC (the
#      guide's Return home leaves it running).
#   4. Alice sees him on: the home screen, the Cloud screen, and Friends with the server to join.
#   5. Bob Two opens HostMC with the server stopped: friends see HostMC, and the address goes.
#   6. Bob Two turns off "Show friends what I'm playing", then turns his console off.
# Each step is checked through XenithOS Cloud's API. Screenshots go to sim/out/friends-*.
#
#   sim/social/test-friends.sh        after build.sh (for usb/); needs python3
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/xenith-friends-test
PORT=${XSIM_CLOUD_PORT:-18795}
SERVER=127.0.0.1:$PORT
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }
api() { python3 -I "$repo/sim/social/cloud.py" "$1" "$SERVER" "${@:2}"; }
friends_of() { api post /v1/friends "token=$1"; }
shot() { [ -s "$repo/sim/out/$1.png" ]; }

[ -d "$repo/usb" ] || { echo "needs usb/ (build.sh makes it)"; exit 2; }
rm -rf "$T"
mkdir -p "$T/state" "$T/store" "$T/web"
cp "$repo/server/flathub.example.json" "$T/store/flathub.json"     # so Minecraft has its name
XENITH_PORT=$PORT XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" \
    python3 "$repo/server/xenith_cloud.py" > "$T/cloud.log" 2>&1 &
CLOUD=$!
trap 'kill $CLOUD 2>/dev/null' EXIT
api wait || { echo "XenithOS Cloud didn't start:"; cat "$T/cloud.log"; exit 2; }
A=$(api profile Alice 1357 "$T/alice.txt") && B=$(api profile "Bob Two" 2468 "$T/bob.txt") \
    || { echo "couldn't make the profiles"; exit 2; }

sim() {                          # $1: its name (sim/out/<name>), $2: the script, $3: profile.txt, then VAR=value
    local name=$1 script=$2 profile=$3
    shift 3
    env XSIM_PROFILE="$profile" "$@" "$repo/sim/run.sh" "$repo/sim/scripts/$script" "$name" "$SERVER" > "$T/$name.log" 2>&1
    echo "  ($script: the simulator finished with $?)"
    if grep -qE "Sanitizer|runtime error" "$T/$name.log"; then
        echo "  FAIL  $name: the sanitizers found something (see $T/$name.log)"
        failed=1
    fi
}

echo "== 1. Alice asks Bob Two"
sim friends-ask friends_ask.txt "$T/alice.txt" XSIM_TYPE="Bob Two|"
check "Bob Two has a request from Alice" 'friends_of "$B" | grep -q "^Alice|[^|]*|in|"'
check "Alice's list says she asked" 'friends_of "$A" | grep -q "^Bob Two|[^|]*|out|"'
check "screenshots" 'shot friends-ask/fa_friends_empty && shot friends-ask/fa_asked'

echo "== 2. Bob Two says yes"
sim friends-answer friends_answer.txt "$T/bob.txt"
check "friends, on both sides" 'friends_of "$A" | grep -q "^Bob Two|[^|]*|friend|" && friends_of "$B" | grep -q "^Alice|[^|]*|friend|"'
check "screenshots" 'shot friends-answer/fb_home_request && shot friends-answer/fb_friends_now'

echo "== 3. Bob Two's server can be reached, and he plays Minecraft on a PC"
api post /v1/hello "token=$B" app=hostmc mc=cheery-panda-61.mc.example.net:25566 > /dev/null   # as HostMC says it
sim friends-play friends_play.txt "$T/bob.txt" XSIM_KIND=pc XSIM_GUIDE=1 XSIM_OTHER=minecraft
check "Alice sees him playing Minecraft, and his server's address" \
    'friends_of "$A" | grep -q "^Bob Two|[^|]*|friend|1|minecraft|Minecraft|0|cheery-panda-61.mc.example.net:25566$"'

echo "== 4. Alice sees him on"
sim friends-online friends_online.txt "$T/alice.txt"
check "Bob Two sees Alice on (her console said hello)" 'friends_of "$B" | grep -q "^Alice|[^|]*|friend|1|"'
check "still friends (Remove friend was cancelled)" 'friends_of "$A" | grep -q "^Bob Two|[^|]*|friend|"'
check "screenshots" 'shot friends-online/fo_home && shot friends-online/fo_cloud && shot friends-online/fo_friend'

echo "== 5. Bob Two opens HostMC, with his server stopped"
sim friends-hostmc friends_hostmc.txt "$T/bob.txt" XSIM_BIN=xhostmc XSIM_KIND=pc XSIM_THREAD=1 XSIM_HOSTMC=1
check "Alice sees him in HostMC, and no server to join" 'friends_of "$A" | grep -q "^Bob Two|[^|]*|friend|1|hostmc|HostMC|0|$"'

echo "== 6. Bob Two stops showing what he plays, then turns his console off"
sim friends-sharing friends_sharing.txt "$T/bob.txt"
check "his sharing is off on XenithOS Cloud" 'api post /v1/accounts/me "token=$B" | grep -qx "sharing=0"'
check "and in his console's profile.txt" 'grep -qx "sharing=0" "$HOME/.xsim/friends-sharing/uda:/xenith/profile.txt"'
check "Alice sees him offline, without a game or a server" 'friends_of "$A" | grep -q "^Bob Two|[^|]*|friend|0|||[0-9]*|$"'

if [ "$failed" = 0 ]; then echo "all passed"; else echo "something failed: logs in $T"; fi
exit "$failed"
