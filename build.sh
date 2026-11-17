#!/bin/bash
# Build XENITH and every app in apps/, then stage them in usb/ exactly as they go on the stick:
#   usb/xenon.elf          <- XENITH (XeLL boots this)
#   usb/apps/<name>.elf32  <- apps XENITH lists under APPS
# Run inside WSL:  ./build.sh
set -e
cd "$(dirname "$0")"

export DEVKITXENON="${DEVKITXENON:-$HOME/xenon}"
export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin"

echo "== XENITH"
make --no-print-directory

rm -rf usb
mkdir -p usb/apps
cp xenith.elf32 usb/xenon.elf

# Private settings (server IP) come from config.local.txt, which git ignores.
mkdir -p usb/xenith
if [ -f config.local.txt ]; then
    cp config.local.txt usb/xenith/config.txt
else
    cp config.example.txt usb/xenith/config.txt
    echo "note: no config.local.txt, staged the example config (no server set)"
fi

for app in apps/*/; do
    name=$(basename "$app")
    # Apps without a Makefile are for PCs only (MineNest runs Minecraft servers): linux/Makefile builds them.
    [ -f "$app/Makefile" ] || continue
    echo "== $name"
    make --no-print-directory -C "$app"
    cp "$app/$name.elf32" "usb/apps/$name.elf32"
    [ -f "$app/app.txt" ] && cp "$app/app.txt" "usb/apps/$name.txt"
    [ -f "art/thumbs/$name.ximg" ] && cp "art/thumbs/$name.ximg" "usb/apps/$name.ximg"
done

echo
echo "Staged for the USB stick:"
find usb -type f -printf "  %p  (%s bytes)\n"
