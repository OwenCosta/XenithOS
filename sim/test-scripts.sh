#!/bin/bash
# Every script in sim/scripts, headless, each with the switches its header gives it and the tiled
# framebuffer check on (XSIM_TILED=1), as CI runs them. It fails on:
#   - an AddressSanitizer or UBSan report,
#   - a tiled framebuffer check that doesn't say 0 wrong pixels, or doesn't run at all,
#   - a run that doesn't end with its script (the simulator's exit status, or a hang),
#   - XenithOS Cloud not ending up with what a script did there (a profile, a bug report, the
#     home-network route), or logging an error while consoles used it.
# Scripts that go online get XenithOS Clouds started here on spare ports: one with a stand-in
# Store (the files server/store.example.json lists, as stand-ins) and sim/store's Flathub and
# Steam lists, and one with only those lists, as store_pc.txt and store_steam.txt expect.
# Scripts another test runs (their header names sim/hostmc/test-*.sh) are left to it, and DOOM's
# needs a WAD (XSIM_WAD=path/to/freedoom1.wad).
#
#   sim/test-scripts.sh [name...]        all of them, or only these (sim/test-scripts.sh boot tour)
#
# The USB stick is a stand-in for build.sh's usb/ (XenithOS and the apps as empty files, with their
# app.txt files and thumbnails; the simulator never runs them), so no Xbox build is needed.
# XSIM_USB=usb uses the real one. Screenshots and each run's log end up in sim/out/test-<name>/.
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
LIMIT=${XSIM_LIMIT:-900}          # seconds a run may take (the real-time ones take a minute or two)
T=$(mktemp -d "${TMPDIR:-/tmp}/xsim-test.XXXXXX")
failed=0 ran=0 skipped=0 pids="" done_names=" "
only=" $* "

cleanup() {
    for p in $pids; do kill "$p" 2>/dev/null; done
    wait 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

wanted() { [ "$only" = "  " ] || [[ "$only" == *" $1 "* ]]; }
fail() { echo "  FAIL  $1"; failed=1; }
spare_port() { python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }

# HTTP to a XenithOS Cloud started here: get PORT PATH, admin PORT PATH [BODY], post PORT PATH key=value...
get() { python3 -I -c 'import sys, urllib.request
sys.stdout.write(urllib.request.urlopen("http://127.0.0.1:%s%s" % (sys.argv[1], sys.argv[2]), timeout=10).read().decode())' "$@"; }
admin() { python3 -I -c 'import sys, urllib.request
data = sys.argv[4].encode() if len(sys.argv) > 4 else None
req = urllib.request.Request("http://127.0.0.1:%s%s" % (sys.argv[1], sys.argv[2]), data, headers={"X-Xenith-Key": sys.argv[3]})
sys.stdout.write(urllib.request.urlopen(req, timeout=10).read().decode())' "$1" "$2" "$KEY" "${@:3}"; }
post() { python3 -I -c 'import sys, urllib.parse, urllib.request
body = urllib.parse.urlencode([tuple(p.split("=", 1)) for p in sys.argv[3:]]).encode()
sys.stdout.write(urllib.request.urlopen("http://127.0.0.1:%s%s" % (sys.argv[1], sys.argv[2]), body, timeout=10).read().decode())' "$@"; }

# build.sh's usb/: XenithOS, and each app with an Xbox Makefile with its app.txt and picture.
stage_usb() {
    mkdir -p "$1/apps" "$1/xenith"
    : > "$1/xenon.elf"
    cp "$repo/config.example.txt" "$1/xenith/config.txt"
    for app in "$repo"/apps/*/; do
        name=$(basename "$app")
        [ -f "$app/Makefile" ] || continue
        : > "$1/apps/$name.elf32"
        if [ -f "$app/app.txt" ]; then cp "$app/app.txt" "$1/apps/$name.txt"; fi
        if [ -f "$repo/art/thumbs/$name.ximg" ]; then cp "$repo/art/thumbs/$name.ximg" "$1/apps/$name.ximg"; fi
    done
}

# A Store folder: sim/store's Flathub and Steam lists with their pictures, and with $2 = full,
# server/store.example.json's items with stand-ins for their files. Its XenithOS updates are
# this version (shared/theme.h), as on a Store that's up to date, so none is waiting.
stage_store() {
    mkdir -p "$1"
    cp "$here/store/flathub.json" "$here/store/steam.json" "$1/"
    if [ "$2" = full ]; then
        python3 -I -c 'import json, os, re, sys
src, out, theme = sys.argv[1:]
version = re.search(r"#define XENITH_VERSION \"([^\"]+)\"", open(theme).read()).group(1)
items = json.load(open(src))
for item in items:
    if item["kind"] in ("system", "pc"):
        item["version"] = version
    with open(os.path.join(out, item["file"]), "w") as f:
        f.write("stand-in for %s\n" % item["file"])
with open(os.path.join(out, "store.json"), "w") as f:
    json.dump(items, f, indent=1)' "$repo/server/store.example.json" "$1" "$repo/shared/theme.h"
    fi
    for id in $(python3 -I -c 'import json, os, sys
ids = [a["id"] for name in ("store.json", "flathub.json", "steam.json") if os.path.exists(os.path.join(sys.argv[1], name))
       for a in json.load(open(os.path.join(sys.argv[1], name)))]
print(" ".join(ids))' "$1"); do
        for ext in ximg png; do
            if [ -f "$repo/art/thumbs/$id.$ext" ]; then cp "$repo/art/thumbs/$id.$ext" "$1/"; fi
        done
    done
}

# Starts a XenithOS Cloud: $1 its name (and log), $2 its Store folder, $3 its port, then
# VAR=value... for it.
cloud() {
    local name=$1 store=$2 port=$3
    shift 3
    mkdir -p "$T/$name/state" "$T/$name/downloads"
    ( cd "$T/$name" && exec env XENITH_PORT="$port" XENITH_STATE_DIR="$T/$name/state" XENITH_STORE_DIR="$store" \
        XENITH_WEB_DIR="$repo/server/web" XENITH_DOWNLOAD_DIR="$T/$name/downloads" XENITH_ADMIN_KEY="$KEY" "$@" \
        python3 -I "$repo/server/xenith_cloud.py" ) > "$T/cloud-$name.log" 2>&1 &
    pids="$pids $!"
    for _ in $(seq 1 50); do get "$port" /v1/ping > /dev/null 2>&1 && return 0; sleep 0.2; done
    fail "XenithOS Cloud ($name) didn't start:"
    cat "$T/cloud-$name.log"
    return 1
}

skip() { echo "== $1"; echo "  skip  $2"; skipped=$((skipped + 1)); done_names="$done_names$1 "; }

# Runs one script: $1 its name, $2 the server ("" for none), then VAR=value... for the simulator.
# RUN_OK is 1 when it passed.
run() {
    local name=$1 server=$2 log status problems counts bad
    shift 2
    RUN_OK=0
    done_names="$done_names$name "
    wanted "$name" || return 0
    echo "== $name"
    ran=$((ran + 1))
    log="$T/sim-$name.log"
    # The stand-in Stores here are unsigned, so the simulator acts as a build without a Store key
    # (source/store_key.h has XenithOS Cloud's); sim/store/test-store.sh checks the signing itself.
    env XSIM_TILED=1 XSIM_USB="$usb" XSIM_STORE_KEY="${XSIM_STORE_KEY:-none}" UBSAN_OPTIONS=print_stacktrace=1 "$@" \
        timeout -k 10 "$LIMIT" bash "$here/run.sh" "$here/scripts/$name.txt" "test-$name" ${server:+"$server"} > "$log" 2>&1
    status=$?
    problems=""
    if [ "$status" = 124 ] || [ "$status" = 137 ]; then problems="it didn't finish within $LIMIT s"
    elif [ "$status" != 0 ]; then problems="the simulator finished with $status"; fi
    if grep -qE 'runtime error:|ERROR: (Address|Leak)Sanitizer|SUMMARY: (Address|UndefinedBehavior)Sanitizer' "$log"; then
        problems="${problems:+$problems; }a sanitizer report"
    fi
    counts=$(grep -oE 'tiled framebuffer check \([^)]*\): [0-9]+ wrong pixels' "$log")
    bad=$(printf '%s\n' "$counts" | grep -vE ': 0 wrong pixels$' | grep -c 'wrong pixels')
    if [ -z "$counts" ]; then problems="${problems:+$problems; }no tiled framebuffer check ran"
    elif [ "$bad" != 0 ]; then problems="${problems:+$problems; }wrong pixels ($(printf '%s\n' "$counts" | grep -vE ': 0 wrong pixels$' | head -3 | tr '\n' ' '))"; fi
    mkdir -p "$here/out/test-$name"
    cp "$log" "$here/out/test-$name/sim.log"
    if [ -n "$problems" ]; then
        fail "$name: $problems (log: sim/out/test-$name/sim.log)"
        if grep -qE 'runtime error:|ERROR: (Address|Leak)Sanitizer' "$log"; then
            grep -E -A 25 'runtime error:|ERROR: (Address|Leak)Sanitizer' "$log" | head -60
        else
            tail -n 15 "$log"
        fi
    else
        echo "  ok    $name ($(printf '%s\n' "$counts" | wc -l) tiled checks, 0 wrong pixels)"
        RUN_OK=1
    fi
}

command -v python3 > /dev/null || { echo "needs python3"; exit 2; }
make -s -C "$here" || { echo "the simulator didn't build"; exit 2; }
KEY=$(python3 -I -c 'import secrets; print(secrets.token_hex(16))')
if [ -n "$XSIM_USB" ]; then usb=$(realpath "$XSIM_USB"); else usb="$T/usb"; stage_usb "$usb"; fi
mkdir -p "$T/photos"
cp "$repo"/art/logo*.png "$repo"/art/thumbs/{doom,volt,tube,hostmc,minecraft}.png "$T/photos/"

echo "== offline"
run boot ""
run chime ""
run tour ""
run home_row "" XSIM_APPS=8
run apps_list "" XSIM_KIND=pc XSIM_GUIDE=1 XSIM_OTHER="roblox minecraft" XSIM_APPS=6
run apps_remove "" XSIM_KIND=pc XSIM_OTHER="kodi minecraft"
run guide "" XSIM_KIND=pc XSIM_GUIDE=1 XSIM_OTHER="roblox minecraft"
run placeholder "" XSIM_KIND=pc XSIM_OTHER="minecraft prism-launcher:Prism_Launcher"
run volt "" XSIM_BIN=xvolt
run photos "" XSIM_BIN=xphotos XSIM_PHOTOS="$T/photos"
if [ -n "$XSIM_WAD" ]; then
    run doom "" XSIM_BIN=xdoom XSIM_WAD="$XSIM_WAD"
elif wanted doom; then
    skip doom "DOOM needs a WAD: XSIM_WAD=path/to/freedoom1.wad"
fi
run hostmc "" XSIM_BIN=xhostmc XSIM_KIND=pc XSIM_THREAD=1 XSIM_HOSTMC=1 XSIM_TYPE="say Hello from HostMC|"

online=0
for s in online feedback gallery store_pc store_steam store_route tube store_local; do wanted "$s" && online=1; done
if [ "$online" = 1 ]; then
    echo "== online (XenithOS Cloud on spare ports)"
    stage_store "$T/store-full" full
    stage_store "$T/store-lists" lists
    FULL=$(spare_port) LISTS=$(spare_port)
    # The full one also says where it is on its own network (127.0.0.1 here), for store_route.txt.
    cloud full "$T/store-full" "$FULL" XENITH_LAN_ADDR="127.0.0.1:$FULL"
    cloud lists "$T/store-lists" "$LISTS"
    PROFILE="$T/profile.txt"
    post "$FULL" /v1/accounts/create name=Gallery pin=4321 avatar=1202001000 > "$PROFILE" 2>/dev/null
    grep -q '^token=' "$PROFILE" || fail "XenithOS Cloud didn't make the profile the gallery and Tube scripts sign in with"
    PUBLIC=$(hostname -I 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+(\.[0-9]+){3}$/) { print $i; exit }}')

    run online "127.0.0.1:$FULL"
    if [ "$RUN_OK" = 1 ] && ! get "$FULL" /v1/players/Owen 2>/dev/null | grep -q '^name=Owen'; then
        fail "online: XenithOS Cloud doesn't have the profile the script made (Owen)"
    fi
    run feedback "127.0.0.1:$FULL" XSIM_TYPE="The Store froze when I pressed Y twice|"
    if [ "$RUN_OK" = 1 ] && ! admin "$FULL" /v1/feedback 2>/dev/null | grep -q 'The Store froze when I pressed Y twice'; then
        fail "feedback: XenithOS Cloud didn't keep the report the script sent"
    fi
    run gallery "127.0.0.1:$FULL" XSIM_PROFILE="$PROFILE"
    run store_pc "127.0.0.1:$LISTS" XSIM_KIND=pc
    run store_steam "127.0.0.1:$LISTS" XSIM_KIND=pc XSIM_INSTALLED=com.valvesoftware.Steam
    if [ -n "$PUBLIC" ]; then
        run store_route "$PUBLIC:$FULL" XSIM_KIND=pc
        if [ "$RUN_OK" = 1 ] && ! grep -qx "lan=127.0.0.1:$FULL" "$HOME/.xsim/test-store_route/uda:/xenith/route.txt" 2>/dev/null; then
            fail "store_route: XenithOS didn't take XenithOS Cloud's address on its own network (route.txt)"
        fi
    elif wanted store_route; then
        skip store_route "needs a network address other than 127.0.0.1, to stand in for XenithOS Cloud's public one"
    fi
    # Tube (with yt-dlp and ffmpeg on this machine, it really asks YouTube; without them, Tube is off)
    run tube "127.0.0.1:$FULL" XSIM_BIN=xtube XSIM_THREAD=1 XSIM_PROFILE="$PROFILE"
    # Last, since every console that starts after it shows the news: a long one, for the toast's wrapping.
    if wanted store_local; then
        admin "$FULL" /v1/news "XenithOS Cloud has news: the Store now lists XenithOS's own apps on PCs, with HostMC, and DOOM comes with Freedoom as one package." > /dev/null ||
            fail "XenithOS Cloud didn't take the news post"
    fi
    run store_local "127.0.0.1:$FULL" XSIM_KIND=pc XSIM_UNINSTALLED=doom
    for name in full lists; do
        if grep -q 'Traceback' "$T/cloud-$name.log" 2>/dev/null; then
            fail "XenithOS Cloud ($name) logged an error:"
            grep -B 2 -A 12 'Traceback' "$T/cloud-$name.log" | head -40
        fi
    done
fi

# New scripts nobody has given switches yet run with none; another test's are left to it.
for f in "$here"/scripts/*.txt; do
    name=$(basename "$f" .txt)
    [[ "$done_names" == *" $name "* ]] && continue
    if grep -qE '^#.*sim/hostmc/test-[a-z]+\.sh' "$f"; then
        wanted "$name" && skip "$name" "run by $(grep -oE 'sim/hostmc/test-[a-z]+\.sh' "$f" | head -1)"
        continue
    fi
    if wanted "$name"; then echo "  (no switches known for $name.txt: add them to sim/test-scripts.sh if it needs any)"; fi
    run "$name" ""
done

echo
echo "$ran run, $skipped skipped: $([ "$failed" = 0 ] && echo "all passed" || echo "FAILED")"
exit $failed
