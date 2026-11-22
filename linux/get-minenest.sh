#!/bin/bash
# Puts MineNest's program (github.com/OwenCosta/minenest, which runs the Minecraft servers for the
# MineNest app) in <dir>/bin/minenest: the release named in minenest.sha256 beside this script,
# for this processor, kept only if its SHA-256 is the one written there. Nothing it downloads is
# run here, and it won't run as root: install.sh runs it as the XenithOS user, as update.sh is run.
# A copy that's there already stays when GitHub can't be reached, so an update without the internet
# still works. MineNest learns that XenithOS keeps it up to date (bin/minenest.managed), so its own
# Update button leaves that to XenithOS's updates.
#   get-minenest.sh <dir>           (default /usr/lib/xenithos)
# MINENEST_RELEASE_URL points it at another folder holding the same release's files (tests).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-/usr/lib/xenithos}
PIN="$HERE/minenest.sha256"
if [ "$(id -u)" = 0 ]; then
    echo "get-minenest.sh doesn't run as root: run it as the XenithOS user" >&2
    exit 1
fi
VERSION=$(sed -n 's/^version=\([0-9][0-9.]*\)$/\1/p' "$PIN")
[ -n "$VERSION" ] || { echo "$PIN doesn't name a MineNest version" >&2; exit 1; }
BASE=${MINENEST_RELEASE_URL:-https://github.com/OwenCosta/minenest/releases/download/v$VERSION}
case "$(uname -m)" in
    x86_64|amd64) ARCH=amd64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) echo "MineNest has no build for $(uname -m): the MineNest app won't have servers to run"; exit 0 ;;
esac
ASSET="minenest-linux-$ARCH"
WANT=$(awk -v a="$ASSET" '$2 == a {print $1}' "$PIN")
case "$WANT" in
    [0-9a-f]*) [ "${#WANT}" = 64 ] || { echo "$PIN has no SHA-256 for $ASSET" >&2; exit 1; } ;;
    *) echo "$PIN has no SHA-256 for $ASSET" >&2; exit 1 ;;
esac
mkdir -p "$DEST/bin"
managed() {
    printf 'XenithOS keeps MineNest up to date: new versions come with XenithOS updates (linux/minenest.sha256).\n' \
        > "$DEST/bin/minenest.managed"
}
if [ -f "$DEST/bin/minenest" ] && [ "$(sha256sum < "$DEST/bin/minenest" | cut -d' ' -f1)" = "$WANT" ]; then
    managed
    echo "MineNest $VERSION is here already"
    exit 0
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if ! curl -fsSL --proto '=https,http' --retry 2 "$BASE/$ASSET" -o "$TMP/$ASSET"; then
    if [ -x "$DEST/bin/minenest" ]; then echo "Couldn't reach MineNest's releases: keeping the MineNest that's here"; exit 0; fi
    echo "Couldn't download MineNest $VERSION: the MineNest app gets it at the next XenithOS update"
    exit 0
fi
GOT=$(sha256sum < "$TMP/$ASSET" | cut -d' ' -f1)
if [ "$GOT" != "$WANT" ]; then
    echo "MineNest $VERSION's download didn't match the SHA-256 in $PIN, so it wasn't kept" >&2
    exit 1
fi
install -m 755 "$TMP/$ASSET" "$DEST/bin/.minenest.new"
mv -f "$DEST/bin/.minenest.new" "$DEST/bin/minenest"
managed
echo "MineNest $VERSION"
