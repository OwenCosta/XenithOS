#!/bin/bash
# Builds this XenithOS source and puts it in place of the installed version, as you rather
# than root. XenithOS's Store runs it on PCs and Raspberry Pis after downloading a new version
# (the "XenithOS update" item); install.sh makes /usr/lib/xenithos yours so this needs no sudo.
# The old programs are only replaced once the new ones have built.
#   update.sh <XenithOS data folder>      (default ~/.local/share/xenithos)
# Exit codes: 0 updated, 3 /usr/lib/xenithos isn't yours yet (run install.sh once more),
# anything else: the build failed (its output is in update.log next to XenithOS's config).
set -euo pipefail
SRC=$(cd "$(dirname "$0")/.." && pwd)
DEST=${XENITHOS_LIB:-/usr/lib/xenithos}
DATA=${1:-${XENITHOS_DATA:-$HOME/.local/share/xenithos}}

if [ ! -w "$DEST" ]; then
    echo "$DEST isn't writable by $(id -un): run the install command once more"
    exit 3
fi

echo "Building $(grep -o 'XENITH_VERSION "[0-9.]*"' "$SRC/shared/theme.h") in $SRC"
make -C "$SRC/linux" -j"$(nproc)" xenithos volt doom tube photos hostmc

echo "Installing into $DEST"
for b in xenithos volt doom tube photos hostmc; do
    install -m 755 "$SRC/linux/$b" "$DEST/.$b.new"
    mv -f "$DEST/.$b.new" "$DEST/$b"
done

# The apps' names, versions and thumbnails, as XenithOS shows them
mkdir -p "$DATA/uda:/apps"
for app in volt doom tube photos hostmc; do
    if [ -f "$SRC/apps/$app/app.txt" ]; then cp "$SRC/apps/$app/app.txt" "$DATA/uda:/apps/$app.txt"; fi
    if [ -f "$SRC/art/thumbs/$app.ximg" ]; then cp "$SRC/art/thumbs/$app.ximg" "$DATA/uda:/apps/$app.ximg"; fi
    if [ ! -e "$DATA/uda:/apps/$app.elf32" ]; then ln -s "$DEST/$app" "$DATA/uda:/apps/$app.elf32"; fi
done
echo "Updated"
