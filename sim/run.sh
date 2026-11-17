#!/bin/bash
# Run the XenithOS simulator with an input script.
#
#   sim/run.sh <script.txt> <name> [server]
#
# Uses usb/ (made by build.sh) as the USB stick, or the folder XSIM_USB names. Screenshots and
# audio.wav end up in sim/out/<name>/. Pass a server ("127.0.0.1:8795") to test the online screens.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
script="$(realpath "$1")"
name="$2"
server="${3:-}"
usb="${XSIM_USB:-$here/../usb}"
[ -d "$usb" ] || { echo "no $usb: run build.sh first (sim/test-scripts.sh stages a stand-in without it)" >&2; exit 2; }

work="$HOME/.xsim/$name"
# XSIM_KEEP=1: the same stick as the last run with this name (a console used again), else a fresh one
if [ "$XSIM_KEEP" = 1 ] && [ -d "$work/uda:" ]; then
    rm -rf "$work/out"
else
    rm -rf "$work"
    mkdir -p "$work/uda:"
    cp -r "$usb/." "$work/uda:/"
fi
mkdir -p "$work/uda:/xenith"
# XSIM_UDA: a folder of files to put on the stick as well (saves, say)
[ -n "$XSIM_UDA" ] && cp -r "$XSIM_UDA"/. "$work/uda:/"
{
    echo "server=$server"
    echo "name=SIM"
    [ -n "$XSIM_KEY" ] && echo "key=$XSIM_KEY"
} > "$work/uda:/xenith/config.txt"
[ -n "$XSIM_PROFILE" ] && cp "$XSIM_PROFILE" "$work/uda:/xenith/profile.txt"
# XSIM_ROUTE: a route.txt to start with (the home-network route, shared/config.c)
[ -n "$XSIM_ROUTE" ] && cp "$XSIM_ROUTE" "$work/uda:/xenith/route.txt"
# XSIM_APPS: that many more (empty) apps, for rows that have to scroll
if [ -n "$XSIM_APPS" ]; then
    for i in $(seq 1 "$XSIM_APPS"); do
        : > "$work/uda:/apps/game$i.elf32"
        printf 'name=Game %s\nversion=1.0\n' "$i" > "$work/uda:/apps/game$i.txt"
    done
fi
# XSIM_OTHER: Flathub apps installed, by Store id ("roblox minecraft"): uda:/apps/<id>.app with
# the name and thumbnail the Store would give them (XSIM_KIND=pc)
if [ -n "$XSIM_OTHER" ]; then
    for id in $XSIM_OTHER; do
        app_name=$(python3 -c "import json,sys; print(next((a['name'] for a in json.load(open(sys.argv[1])) if a['id']==sys.argv[2]), sys.argv[2]))" \
                   "$here/../server/flathub.example.json" "${id%%:*}")
        # "id:Its_Name" names one the catalog doesn't have (underscores for spaces)
        case "$id" in *:*) app_name=$(echo "${id#*:}" | tr '_' ' '); id="${id%%:*}";; esac
        printf 'name=%s\nflatpak=org.example.%s\n' "$app_name" "$id" > "$work/uda:/apps/$id.app"
        [ -f "$here/../art/thumbs/$id.ximg" ] && cp "$here/../art/thumbs/$id.ximg" "$work/uda:/apps/$id.ximg"
    done
fi
# XSIM_CRASH: crash files (space-separated) as programs leave them (shared/crash.h), for XenithOS
# to find at the start, as if those had crashed last time
if [ -n "$XSIM_CRASH" ]; then
    mkdir -p "$work/uda:/xenith/crash"
    # shellcheck disable=SC2086
    for c in $XSIM_CRASH; do cp "$c" "$work/uda:/xenith/crash/"; done
fi
# XSIM_PHOTOS: a folder of pictures for Photos (copied to uda:/photos)
if [ -n "$XSIM_PHOTOS" ]; then
    mkdir -p "$work/uda:/photos"
    cp -r "$XSIM_PHOTOS"/. "$work/uda:/photos/"
fi
# XSIM_WAD: one or more WADs (space-separated) for DOOM
if [ -n "$XSIM_WAD" ]; then
    mkdir -p "$work/uda:/doom"
    # shellcheck disable=SC2086
    for w in $XSIM_WAD; do cp "$w" "$work/uda:/doom/"; done
fi
# XSIM_MINENEST=1: MineNest's own program (MINENEST_BIN, built from github.com/OwenCosta/minenest)
# in a folder of its own, with a stand-in for the Minecraft server (sim/minenest/java) and nothing
# downloaded (MINENEST_FAKE_INSTALL), and one server made already ("My Server", Paper).
if [ -n "${XSIM_MINENEST:-}" ]; then
    if [ ! -x "${MINENEST_BIN:-}" ]; then echo "XSIM_MINENEST needs MINENEST_BIN: the minenest program"; exit 2; fi
    export MINENEST_HOME="$work/minenest" MINENEST_JAVA="$here/minenest/java" MINENEST_FAKE_INSTALL=1
    export MINENEST_CLOUD=http://127.0.0.1:9                # no XenithOS Cloud for it
    mkdir -p "$MINENEST_HOME"
    echo "{\"api_port\": $((41000 + RANDOM % 8000))}" > "$MINENEST_HOME/minenest.json"
    # The stand-in isn't Minecraft, so there's no EULA to agree to: its eula.txt is the test's.
    "$MINENEST_BIN" create "My Server" -t paper -v 26.3 --agree-eula > /dev/null
fi

# XSIM_KIND=pc: XenithOS's own apps as a PC has them, built in a folder standing in for
# /usr/lib/xenithos with their names and pictures, for the Store to list and install.
# XSIM_UNINSTALLED="doom volt": ones that are built but not on the home screen.
if [ "$XSIM_KIND" = pc ]; then
    mkdir -p "$work/lib"
    for app in volt doom tube photos minenest; do
        : > "$work/lib/$app"
        [ -f "$here/../apps/$app/app.txt" ] && cp "$here/../apps/$app/app.txt" "$work/lib/$app.txt"
        [ -f "$here/../art/thumbs/$app.ximg" ] && cp "$here/../art/thumbs/$app.ximg" "$work/lib/$app.ximg"
    done
    export XSIM_LIB="$work/lib"
    # shellcheck disable=SC2086
    for app in ${XSIM_UNINSTALLED:-}; do rm -f "$work/uda:/apps/$app".*; done
fi

bin="${XSIM_BIN:-xsim}"
make -s -C "$here" "$bin"
cd "$work"
# XSIM_ARGS: arguments for the program, e.g. "xenithos-handoff from=volt" to start XenithOS as
# if coming back from an app (shared/handoff.h). It comes from the environment.
# shellcheck disable=SC2086,SC2153
status=0
XSIM_SCRIPT="$script" XSIM_OUT="$work/out" ASAN_OPTIONS=detect_leaks=0 "$here/$bin" $XSIM_ARGS || status=$?

# MineNest's program, and any server a script left running, stop.
if [ -n "${XSIM_MINENEST:-}" ]; then "$MINENEST_BIN" quit > /dev/null 2>&1 || true; sleep 2; fi

mkdir -p "$here/out/$name"
cp "$work"/out/* "$here/out/$name/"
echo "results in sim/out/$name"
exit "$status"
