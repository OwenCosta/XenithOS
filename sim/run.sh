#!/bin/bash
# Run the XenithOS simulator with an input script.
#
#   sim/run.sh <script.txt> <name> [server]
#
# Uses usb/ (made by build.sh) as the USB stick. Screenshots and audio.wav end up in
# sim/out/<name>/. Pass a server ("127.0.0.1:8795") to test the online screens.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
script="$(realpath "$1")"
name="$2"
server="${3:-}"

work="$HOME/.xsim/$name"
rm -rf "$work"
mkdir -p "$work/uda:"
cp -r "$here/../usb/." "$work/uda:/"
mkdir -p "$work/uda:/xenith"
{
    echo "server=$server"
    echo "name=SIM"
    [ -n "$XSIM_KEY" ] && echo "key=$XSIM_KEY"
} > "$work/uda:/xenith/config.txt"
[ -n "$XSIM_PROFILE" ] && cp "$XSIM_PROFILE" "$work/uda:/xenith/profile.txt"
# XSIM_SETTINGS: settings.txt to start with, its lines separated by spaces
# ("theme=ocean wallpaper=peaks")
if [ -n "$XSIM_SETTINGS" ]; then
    for kv in $XSIM_SETTINGS; do echo "$kv"; done > "$work/uda:/xenith/settings.txt"
fi
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
# XSIM_PHOTOS: a folder of pictures for Photos (copied to uda:/photos)
if [ -n "$XSIM_PHOTOS" ]; then
    mkdir -p "$work/uda:/photos"
    cp -r "$XSIM_PHOTOS"/. "$work/uda:/photos/"
fi
# XSIM_MUSIC: a folder of music for Music (copied to uda:/music)
if [ -n "$XSIM_MUSIC" ]; then
    mkdir -p "$work/uda:/music"
    cp -r "$XSIM_MUSIC"/. "$work/uda:/music/"
fi
# XSIM_WAD: one or more WADs (space-separated) for DOOM
if [ -n "$XSIM_WAD" ]; then
    mkdir -p "$work/uda:/doom"
    # shellcheck disable=SC2086
    for w in $XSIM_WAD; do cp "$w" "$work/uda:/doom/"; done
fi
# XSIM_HOSTMC=1: HostMC with a stand-in for the Minecraft server (sim/hostmc/java) and what it
# would have downloaded already in place (sim/hostmc/data), so nothing is downloaded
if [ -n "$XSIM_HOSTMC" ]; then
    mkdir -p "$work/hostmc"
    cp -r "$here/hostmc/data/." "$work/hostmc/"
    echo "stand-in" > "$work/hostmc/server/server.jar"
    export HOSTMC_JAVA="$here/hostmc/java"
fi

# XSIM_KIND=pc: XenithOS's own apps as a PC has them, built in a folder standing in for
# /usr/lib/xenithos with their names and pictures, for the Store to list and install.
# XSIM_UNINSTALLED="doom volt": ones that are built but not on the home screen.
if [ "$XSIM_KIND" = pc ]; then
    mkdir -p "$work/lib"
    for app in volt doom tube photos hostmc music; do
        : > "$work/lib/$app"
        [ -f "$here/../apps/$app/app.txt" ] && cp "$here/../apps/$app/app.txt" "$work/lib/$app.txt"
        [ -f "$here/../art/thumbs/$app.ximg" ] && cp "$here/../art/thumbs/$app.ximg" "$work/lib/$app.ximg"
    done
    export XSIM_LIB="$work/lib"
    # shellcheck disable=SC2086
    for app in ${XSIM_UNINSTALLED:-}; do rm -f "$work/uda:/apps/$app".*; done
fi

# Bluetooth (XSIM_KIND=pc) always talks to the stand-in, never to this PC's own: two controllers
# to pair, one already paired, a TV. XSIM_BT=none or blocked: no adapter, or one switched off.
# XSIM_BLUETOOTHCTL: another program in its place (a path that isn't there: BlueZ missing).
export XENITHOS_BLUETOOTHCTL="${XSIM_BLUETOOTHCTL:-$here/bluetooth/bluetoothctl}"

bin="${XSIM_BIN:-xsim}"
make -s -C "$here" "$bin"
cd "$work"
# XSIM_ARGS: arguments for the program, e.g. "xenithos-handoff from=volt" to start XenithOS as
# if coming back from an app (shared/handoff.h). It comes from the environment.
# shellcheck disable=SC2086,SC2153
status=0
XSIM_SCRIPT="$script" XSIM_OUT="$work/out" ASAN_OPTIONS=detect_leaks=0 "$here/$bin" $XSIM_ARGS || status=$?

# A server HostMC left running (a script that didn't stop it): stopped, once its PID is
# checked to be the process running in that server folder.
pidfile="$work/hostmc/server/hostmc.pid"
if [ -f "$pidfile" ]; then
    pid=$(grep '^pid=' "$pidfile" | cut -d= -f2)
    if [ -n "$pid" ] && [ "$(readlink "/proc/$pid/cwd" 2>/dev/null)" = "$(realpath "$work/hostmc/server")" ]; then
        echo "stopping the server HostMC left running (pid $pid)"
        kill "$pid" || true
    fi
fi

mkdir -p "$here/out/$name"
cp "$work"/out/* "$here/out/$name/"
echo "results in sim/out/$name"
exit "$status"
