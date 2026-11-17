#!/bin/bash
# Puts MineNest (github.com/OwenCosta/minenest, the program that runs Minecraft servers for the
# MineNest app) in <dir>/bin/minenest: the newest release for this processor, checked against the
# release's SHA256SUMS. A copy that's there already stays when GitHub can't be reached, so an
# update without the internet still works. install.sh and update.sh run it.
#   get-minenest.sh <dir>           (default /usr/lib/xenithos)
# MINENEST_RELEASE_URL points it at another release folder (tests).
set -euo pipefail
DEST=${1:-/usr/lib/xenithos}
BASE=${MINENEST_RELEASE_URL:-https://github.com/OwenCosta/minenest/releases/latest/download}
case "$(uname -m)" in
    x86_64|amd64) ARCH=amd64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) echo "MineNest has no build for $(uname -m): the MineNest app won't have servers to run"; exit 0 ;;
esac
ASSET="minenest-linux-$ARCH"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if ! curl -fsSL --retry 2 "$BASE/SHA256SUMS" -o "$TMP/SHA256SUMS" || ! curl -fsSL --retry 2 "$BASE/$ASSET" -o "$TMP/$ASSET"; then
    if [ -x "$DEST/bin/minenest" ]; then echo "Couldn't reach MineNest's releases: keeping the MineNest that's here"; exit 0; fi
    echo "Couldn't download MineNest: the MineNest app gets it at the next XenithOS update"
    exit 0
fi
WANT=$(awk -v a="$ASSET" '$2 == a || $2 == "*"a {print $1}' "$TMP/SHA256SUMS")
GOT=$(sha256sum "$TMP/$ASSET" | cut -d' ' -f1)
if [ -z "$WANT" ] || [ "$WANT" != "$GOT" ]; then
    echo "MineNest's download didn't match its checksum, so it wasn't kept"
    exit 1
fi
mkdir -p "$DEST/bin"
install -m 755 "$TMP/$ASSET" "$DEST/bin/.minenest.new"
mv -f "$DEST/bin/.minenest.new" "$DEST/bin/minenest"
echo "MineNest $("$DEST/bin/minenest" about | head -1 | cut -d' ' -f2)"
