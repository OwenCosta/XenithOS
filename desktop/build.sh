#!/bin/bash
# Builds the XenithOS Desktop ISO: an Ubuntu 24.04 system that starts in XenithOS Desktop (a
# desktop in XenithOS's own look), has XenithOS Console one click away, and installs itself on a PC.
#
#   sudo bash desktop/build.sh [options]        on Debian 13 or newer, with live-build
#   bash desktop/build-in-docker.sh [options]   anywhere with Docker (Windows, macOS, Linux)
#
# The tool that makes the ISO is Debian's live-build (Ubuntu's own is a much older one), pointed at
# Ubuntu's packages. So the machine or container that builds is Debian; what it builds is Ubuntu.
#
# Options
#   --out DIR           where the ISO goes (default: desktop/out)
#   --work DIR          where it's built (default: /var/tmp); a build needs about 16 GB there
#   --server HOST:PORT  the XenithOS Cloud address XenithOS Console starts with (default: the
#                       "server=" line in config.local.txt, which is never committed)
#   --channel URL       where installed PCs look for updates (default: desktop/channel.txt)
#   --snapshot STAMP    Ubuntu's packages as they were at STAMP on snapshot.ubuntu.com (for
#                       example 20261115T000000Z), so the same commit and stamp give the same ISO
#   --keep              keep the build folder afterwards (to look inside, or to build again faster)
#
# What goes in the ISO is all in this folder:
#   VERSION            XenithOS Desktop's version
#   packages/          the Ubuntu packages: system.list and live.list for the ISO, and
#                      desktop.list, apps.list and security.list, which XenithOS Desktop's own
#                      packages depend on (so updates can add to them)
#   make-debs.sh       builds XenithOS Desktop's own packages from rootfs/, archive/ and the art;
#                      the ISO installs them, and updates are made of them
#   hooks/             run inside the new system as it's built (XenithOS Console is compiled there)
#   live/              files only the ISO has: the installer and the live system's set-up
#   boot/              the ISO's boot menu
#   art/make-art.py    draws the icons, wallpapers, boot screens and window borders around the logo
# XenithOS Console comes from this repository (linux/, shared/, source/, apps/), built the way
# linux/install.sh builds it.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
OUT="$HERE/out"
WORK=/var/tmp
SNAPSHOT=""
KEEP=0
DEB_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="${2:?}"; shift ;;
        --work) WORK="${2:?}"; shift ;;
        --server|--channel) DEB_ARGS+=("$1" "${2:?}"); shift ;;
        --snapshot) SNAPSHOT="${2:?}"; shift ;;
        --keep) KEEP=1 ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | grep '^#' | cut -c3-; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

say() { printf '\n== %s\n' "$*"; }
die() { printf 'XenithOS Desktop build: %s\n' "$*" >&2; exit 1; }
g() { git -c safe.directory='*' -C "$REPO" "$@"; }

[ "$(id -u)" = 0 ] || die "run it as root (sudo), or with build-in-docker.sh"
NEEDS="live-build debootstrap ubuntu-keyring git python3 python3-pil librsvg2-bin fonts-inter grub-common dpkg-dev"
for tool in lb debootstrap git python3 rsvg-convert grub-mkfont dpkg-deb; do
    command -v "$tool" >/dev/null || die "$tool is missing. Install: apt install $NEEDS"
done
KEYRING=/usr/share/keyrings/ubuntu-archive-keyring.gpg       # how Ubuntu's packages are checked
[ -f "$KEYRING" ] || die "Ubuntu's archive key is missing. Install: apt install $NEEDS"
[ -f /usr/share/debootstrap/scripts/noble ] || die "this debootstrap doesn't know Ubuntu 24.04 (noble). It needs a newer one."
python3 -c 'import PIL' 2>/dev/null || die "Pillow is missing. Install: apt install $NEEDS"
[ -f /usr/share/fonts/opentype/inter/Inter-Regular.otf ] || die "the Inter font is missing. Install: apt install $NEEDS"
case "$SNAPSHOT" in ''|[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]T[0-9][0-9][0-9][0-9][0-9][0-9]Z) ;;
    *) die "--snapshot looks like 20261115T000000Z" ;;
esac

VERSION=$(tr -d ' \r\n' < "$HERE/VERSION")
CONSOLE=$(sed -n 's/.*XENITH_VERSION *"\([0-9.]*\)".*/\1/p' "$REPO/shared/theme.h")
[ -n "$VERSION" ] && [ -n "$CONSOLE" ] || die "couldn't read the versions (desktop/VERSION, shared/theme.h)"
COMMIT=$(g rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
CHANGES=""
if [ -n "$(g status --porcelain 2>/dev/null)" ]; then CHANGES=" (with changes not committed yet)"; fi
# Every date inside the ISO is the commit's, so building the same commit again gives the same files.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(g log -1 --format=%ct 2>/dev/null || date +%s)}
export SOURCE_DATE_EPOCH
NAME="xenithos-desktop-$VERSION-amd64"

mkdir -p "$OUT" "$WORK"
OUT=$(cd "$OUT" && pwd)
BUILD=$(mktemp -d "$WORK/xenithos-desktop.XXXXXX")

# Unmounts whatever a stopped build left mounted inside the build folder, deepest first, so
# deleting the folder can never reach the real /proc, /sys or /dev.
unmount_build() {
    awk -v b="$BUILD/" 'index($2, b) == 1 { print $2 }' /proc/mounts | sort -r | while read -r m; do
        umount -l "$m" 2>/dev/null || true
    done
}
finish() {
    unmount_build
    if [ "$KEEP" = 1 ]; then echo "The build folder is still there: $BUILD"; else rm -rf --one-file-system "$BUILD"; fi
}
trap finish EXIT

say "Setting up XenithOS Desktop $VERSION with XenithOS Console $CONSOLE ($COMMIT$CHANGES) in $BUILD"
cd "$BUILD"
MIRROR=http://archive.ubuntu.com/ubuntu/
SECURITY=http://security.ubuntu.com/ubuntu/
AREAS="main restricted universe multiverse"
APT_OPTIONS="--yes -o Acquire::Retries=5"
if [ -n "$SNAPSHOT" ]; then
    MIRROR=https://snapshot.ubuntu.com/ubuntu/$SNAPSHOT/
    SECURITY=$MIRROR
    APT_OPTIONS="$APT_OPTIONS -o Acquire::Check-Valid-Until=false"
fi
# Kernel options for the live system. username/hostname/locales are live-config's;
# nox11autologin keeps live-config from starting a plain desktop on its own, so the first
# console's automatic login reaches XenithOS Desktop's session instead; noprompt skips "remove
# the disc" at the end.
APPEND="boot=live components quiet splash loglevel=3 vt.global_cursor_default=0 noprompt nox11autologin username=xenith"
APPEND="$APPEND user-fullname=XenithOS hostname=xenithos locales=en_US.UTF-8 keyboard-layouts=us timezone=Etc/UTC"

# live-build's "debian" mode, given Ubuntu's suite, mirrors, areas and key. The kernel isn't
# live-build's to pick here (--linux-packages none): packages/system.list names Ubuntu 24.04's
# newest (linux-image-generic-hwe-24.04), and "generic" is how live-build finds it in /boot.
lb config \
    --mode debian \
    --distribution noble \
    --architectures amd64 \
    --linux-packages none \
    --linux-flavours generic \
    --archive-areas "$AREAS" \
    --mirror-bootstrap "$MIRROR" \
    --mirror-chroot "$MIRROR" \
    --mirror-chroot-security "$SECURITY" \
    --mirror-binary http://archive.ubuntu.com/ubuntu/ \
    --mirror-binary-security http://security.ubuntu.com/ubuntu/ \
    --keyring-packages ubuntu-keyring \
    --debootstrap-options "--keyring=$KEYRING --include=ca-certificates" \
    --apt-options "$APT_OPTIONS" \
    --apt-recommends false \
    --apt-indices false \
    --apt-source-archives false \
    --security true \
    --updates true \
    --backports false \
    --firmware-chroot false \
    --firmware-binary false \
    --initsystem systemd \
    --bootloaders "grub-pc grub-efi" \
    --uefi-secure-boot enable \
    --binary-image iso-hybrid \
    --debian-installer none \
    --memtest none \
    --win32-loader false \
    --zsync false \
    --checksums none \
    --chroot-squashfs-compression-type xz \
    --image-name xenithos-desktop \
    --iso-application "XenithOS Desktop" \
    --iso-publisher "XenithOS" \
    --iso-preparer "XenithOS desktop/build.sh" \
    --iso-volume "XenithOS Desktop $VERSION" \
    --bootappend-live "$APPEND" \
    --bootappend-live-failsafe none \
    > "$BUILD/config.log"

say "Making XenithOS Desktop's packages"
mkdir -p config/packages.chroot
bash "$HERE/make-debs.sh" "$BUILD/debs" ${DEB_ARGS[@]+"${DEB_ARGS[@]}"}
cp "$BUILD"/debs/*.deb config/packages.chroot/
mkdir -p "$OUT/debs"
cp "$BUILD"/debs/*.deb "$BUILD"/debs/*.update.tar "$OUT/debs/"

say "Putting the ISO's files in place"
INC=config/includes.chroot_after_packages
mkdir -p "$INC" config/bootloaders/grub-pc config/package-lists config/hooks/normal config/archives config/preseed
# The ISO's own lists, and XenithOS Desktop's packages, which bring the desktop, apps and tools.
cat "$HERE/packages/system.list" "$HERE/packages/live.list" > config/package-lists/xenithos.list.chroot
printf 'xenithos-archive\nxenithos-desktop\nxenithos-desktop-apps\nxenithos-security-tools\n' >> config/package-lists/xenithos.list.chroot
# Brave's and Mullvad's package sources, for the build itself: they have to be known before the
# xenithos-archive package (which carries them for the installed system) can be installed along
# with Brave and Mullvad VPN. live-build takes these out of the finished system again.
# They name their keys exactly as xenithos-archive's own entries do. Ubuntu's apt refuses to
# read its sources at all when two entries for one source say different things about its key,
# and once the package is in, there are two. The key files themselves are put in place right
# after the base system is laid down (see "Building" below).
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/xenithos-brave.gpg] https://brave-browser-apt-release.s3.brave.com/ stable main" > config/archives/build-brave.list.chroot
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/xenithos-mullvad.asc] https://repository.mullvad.net/deb/stable stable main" > config/archives/build-mullvad.list.chroot
# Questions two of the tools would ask while installing, answered (as bundle-install.sh does).
cat > config/preseed/xenithos.cfg.chroot <<'EOF'
wireshark-common wireshark-common/install-setuid boolean true
macchanger macchanger/automatically_run boolean false
EOF
for hook in "$HERE"/hooks/*.hook.chroot; do
    install -m 755 "$hook" config/hooks/normal/
done
# Copied without the source's permissions or owner (a checkout on Windows, seen from Docker,
# has every file executable by everyone): directories 755, files 644, scripts 755.
copy_tree() {
    mkdir -p "$2"
    cp -r --no-preserve=mode,ownership "$1/." "$2/"
}
fix_modes() {
    find "$1" -type d -exec chmod 755 {} +
    find "$1" -type f -exec chmod 644 {} +
    find "$1" -type f -print0 | while IFS= read -r -d '' f; do
        if [ "$(head -c 2 "$f")" = '#!' ]; then chmod 755 "$f"; fi
    done
}
copy_tree "$HERE/live" "$INC"
copy_tree "$HERE/boot" config/bootloaders/grub-pc

say "Drawing the boot menu"
python3 "$HERE/art/make-art.py" "$BUILD/art" "$REPO/tools"
copy_tree "$BUILD/art/live" "$INC"
copy_tree "$BUILD/art/boot" config/bootloaders/grub-pc
# The boot menu's font: Inter, in GRUB's own format.
for size in 16 20 24; do
    for weight in Regular Bold; do
        lower=$(echo "$weight" | tr A-Z a-z)
        grub-mkfont -s "$size" -o "config/bootloaders/grub-pc/inter-$lower-$size.pf2" \
            "/usr/share/fonts/opentype/inter/Inter-$weight.otf" 2>/dev/null
    done
done

say "Adding XenithOS Console's source"
# The files this checkout has in linux/, shared/, source/ and apps/ (committed or not, but
# never build output, which .gitignore leaves out). hooks/0100-xenithos builds them.
SRC="$INC/usr/src/xenithos"
mkdir -p "$SRC"
g ls-files -z --cached --others --exclude-standard -- linux shared source apps art/thumbs |
    while IFS= read -r -d '' f; do
        if [ -f "$REPO/$f" ]; then install -D -m 644 "$REPO/$f" "$SRC/$f"; fi
    done
[ -f "$SRC/linux/Makefile" ] || die "couldn't copy XenithOS Console's source"
fix_modes "$INC"
fix_modes config/bootloaders/grub-pc

# In the ISO's own files (outside the system image): its name, and what a USB stick made by
# copying the ISO's files onto a FAT32 partition (desktop/write-usb.ps1) needs to start on BIOS
# PCs: GRUB's boot sector and a core image that finds /boot/grub on the stick's first partition.
cat > config/hooks/normal/9000-xenithos.hook.binary <<EOF
#!/bin/sh
set -e
echo "XenithOS Desktop $VERSION (XenithOS Console $CONSOLE, $COMMIT)" > .disk/info
# The kernel and its start-up file, next to the system image. live-build copies them itself when
# it picks the kernel; here packages/system.list does (--linux-packages none), so this does it.
cp -a ../chroot/boot/vmlinuz-* ../chroot/boot/initrd.img-* live/
chmod 644 live/vmlinuz-* live/initrd.img-*
ls live/vmlinuz-*-generic live/initrd.img-*-generic
chroot ../chroot grub-mkimage -O i386-pc -o /tmp/xenithos-usb-core.img -p '(,msdos1)/boot/grub' biosdisk part_msdos fat
mv ../chroot/tmp/xenithos-usb-core.img boot/grub/i386-pc/xenithos-usb-core.img
cp ../chroot/usr/lib/grub/i386-pc/boot.img boot/grub/i386-pc/xenithos-usb-boot.img
# Every file's SHA-256: what the boot menu's "Check the USB stick for errors" and write-usb.ps1
# go by. Made here and not by live-build (--checksums none): its list includes the boot code for
# CDs, which is changed after the list is made, so the check would always find one bad file.
find . -type f ! -path ./boot/grub/grub_eltorito ! -path ./sha256sum.txt -print0 | sort -z | xargs -0 sha256sum > ../sha256sum.txt.new
mv ../sha256sum.txt.new sha256sum.txt
EOF
chmod 755 config/hooks/normal/9000-xenithos.hook.binary

say "Building (15 to 40 minutes; the log is $OUT/$NAME.log)"
# "lb build" is these stages one after the other. They're run one by one here so that Brave's
# and Mullvad's keys can be put in after the first: the base system has to exist, and every
# stage after it reads the package sources that name them. The keys go into the new system and
# into live-build's saved copy of the bare base system (cache/bootstrap), because the later
# stages do part of their work inside a fresh copy of that.
put_keys() {
    install -D -m 644 "$HERE/archive/usr/share/keyrings/xenithos-brave.gpg" "$1/usr/share/keyrings/xenithos-brave.gpg" &&
    install -D -m 644 "$HERE/archive/usr/share/keyrings/xenithos-mullvad.asc" "$1/usr/share/keyrings/xenithos-mullvad.asc"
}
stages() {
    lb bootstrap &&
    put_keys chroot &&
    { [ ! -d cache/bootstrap ] || put_keys cache/bootstrap; } &&
    lb chroot &&
    lb installer &&
    lb binary
}
if ! stages > "$OUT/$NAME.log" 2>&1; then
    tail -n 40 "$OUT/$NAME.log" >&2
    die "the build failed; the whole log is $OUT/$NAME.log"
fi
ISO=$(ls "$BUILD"/*.iso 2>/dev/null | head -n 1)
[ -n "$ISO" ] || die "the build finished without an ISO; see $OUT/$NAME.log"

mv -f "$ISO" "$OUT/$NAME.iso"
if ls "$BUILD"/*.packages >/dev/null 2>&1; then cp "$(ls "$BUILD"/*.packages | head -n 1)" "$OUT/$NAME.packages"; fi
(cd "$OUT" && sha256sum "$NAME.iso" > "$NAME.iso.sha256")
SIZE=$(stat -c %s "$OUT/$NAME.iso")
say "Done: $OUT/$NAME.iso ($((SIZE / 1048576)) MB)"
cat "$OUT/$NAME.iso.sha256"
