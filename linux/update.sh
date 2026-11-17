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
make -C "$SRC/linux" -j"$(nproc)" xenithos volt doom tube photos minenest

echo "Installing into $DEST"
for b in xenithos volt doom tube photos minenest; do
    install -m 755 "$SRC/linux/$b" "$DEST/.$b.new"
    mv -f "$DEST/.$b.new" "$DEST/$b"
done

# MineNest's own program, the newest release (it keeps the one here when GitHub can't be reached).
"$SRC/linux/get-minenest.sh" "$DEST" || echo "MineNest wasn't updated"
# HostMC became MineNest: its program goes (its data stays, for MineNest to bring in).
rm -f "$DEST/hostmc" "$DEST/hostmc.txt" "$DEST/hostmc.ximg"

# Each app's name, version and picture next to its program, for the Store to install it with.
for app in volt doom tube photos minenest; do
    if [ -f "$SRC/apps/$app/app.txt" ]; then install -m 644 "$SRC/apps/$app/app.txt" "$DEST/$app.txt"; fi
    if [ -f "$SRC/art/thumbs/$app.ximg" ]; then install -m 644 "$SRC/art/thumbs/$app.ximg" "$DEST/$app.ximg"; fi
done

# The apps on the home screen get their new names, versions and pictures. The rest stay off it:
# they're installed from the Store, and one removed there stays removed.
mkdir -p "$DATA/uda:/apps"
# HostMC on the home screen becomes MineNest there.
if [ -e "$DATA/uda:/apps/hostmc.elf32" ] || [ -L "$DATA/uda:/apps/hostmc.elf32" ]; then
    rm -f "$DATA/uda:/apps/hostmc.elf32" "$DATA/uda:/apps/hostmc.txt" "$DATA/uda:/apps/hostmc.ximg"
    ln -sfn "$DEST/minenest" "$DATA/uda:/apps/minenest.elf32"
fi
for app in volt doom tube photos minenest; do
    [ -e "$DATA/uda:/apps/$app.elf32" ] || [ -L "$DATA/uda:/apps/$app.elf32" ] || continue
    if [ -f "$DEST/$app.txt" ]; then cp "$DEST/$app.txt" "$DATA/uda:/apps/$app.txt"; fi
    if [ -f "$DEST/$app.ximg" ]; then cp "$DEST/$app.ximg" "$DATA/uda:/apps/$app.ximg"; fi
done
echo "Updated"
