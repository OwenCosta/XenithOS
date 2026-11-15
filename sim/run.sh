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
# XSIM_APPS: that many more (empty) apps, for rows that have to scroll
if [ -n "$XSIM_APPS" ]; then
    for i in $(seq 1 "$XSIM_APPS"); do
        : > "$work/uda:/apps/game$i.elf32"
        printf 'name=Game %s\nversion=1.0\n' "$i" > "$work/uda:/apps/game$i.txt"
    done
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

bin="${XSIM_BIN:-xsim}"
make -s -C "$here" "$bin"
cd "$work"
# XSIM_ARGS: arguments for the program, e.g. "xenithos-handoff from=volt" to start XenithOS as
# if coming back from an app (shared/handoff.h). It comes from the environment.
# shellcheck disable=SC2086,SC2153
XSIM_SCRIPT="$script" XSIM_OUT="$work/out" ASAN_OPTIONS=detect_leaks=0 "$here/$bin" $XSIM_ARGS

mkdir -p "$here/out/$name"
cp "$work"/out/* "$here/out/$name/"
echo "results in sim/out/$name"
