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

for app in apps/*/; do
    name=$(basename "$app")
    echo "== $name"
    make --no-print-directory -C "$app"
    cp "$app/$name.elf32" "usb/apps/$name.elf32"
done

echo
echo "Staged for the USB stick:"
find usb -type f -printf "  %p  (%s bytes)\n"
