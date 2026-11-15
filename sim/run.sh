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

bin="${XSIM_BIN:-xsim}"
make -s -C "$here" "$bin"
cd "$work"
XSIM_SCRIPT="$script" XSIM_OUT="$work/out" ASAN_OPTIONS=detect_leaks=0 "$here/$bin"

mkdir -p "$here/out/$name"
cp "$work"/out/* "$here/out/$name/"
echo "results in sim/out/$name"
