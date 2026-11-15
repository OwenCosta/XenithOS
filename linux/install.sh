#!/bin/bash
# XenithOS installer for PCs and Raspberry Pis.
#
# Turns a Debian-based system (Debian 12 or newer, Ubuntu 22.04 or newer, Raspberry Pi OS) into
# a XenithOS machine: it starts straight into XenithOS (console mode), with the Linux desktop
# one option away in the power menu (desktop mode).
#
#   curl -fsSL http://<XenithOS Cloud address>/install.sh | sudo bash
#   sudo bash linux/install.sh                from a copy of the source
#
# Options
#   --no-desktop        console mode only: skips the desktop (saves about 1 GB)
#   --user NAME         the account XenithOS runs as (default: whoever ran sudo)
#   --server HOST:PORT  the XenithOS Cloud address
#   --uninstall         back to a normal login (your XenithOS data is kept)
set -euo pipefail

SERVER="@XENITHOS_SERVER@"      # XenithOS Cloud fills these in when it hands out this script
SOURCE_URL="@XENITHOS_SOURCE@"
GITHUB_URL="https://github.com/OwenCosta/XenithOS/archive/refs/heads/main.tar.gz"
DESKTOP=1
USER_NAME="${SUDO_USER:-}"
UNINSTALL=0

case "$SERVER" in @*) SERVER="" ;; esac
case "$SOURCE_URL" in @*) SOURCE_URL="" ;; esac

while [ $# -gt 0 ]; do
    case "$1" in
        --no-desktop) DESKTOP=0 ;;
        --user) USER_NAME="${2:-}"; shift ;;
        --server) SERVER="${2:-}"; shift ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) grep '^#' "$0" | sed -n '2,16p' | cut -c3-; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

say() { printf '\n== %s\n' "$*"; }
die() { printf 'XenithOS install: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run it with sudo"
[ -n "$USER_NAME" ] && [ "$USER_NAME" != root ] || die "add --user NAME (the account XenithOS should run as, not root)"
id "$USER_NAME" >/dev/null 2>&1 || die "there's no user called $USER_NAME"
command -v apt-get >/dev/null || die "this needs a Debian-based system (Debian, Ubuntu or Raspberry Pi OS)"

HOME_DIR=$(getent passwd "$USER_NAME" | cut -d: -f6)
DATA="$HOME_DIR/.local/share/xenithos"
PROFILE="$HOME_DIR/.profile"
MARK_BEGIN="# >>> XenithOS console session >>>"
MARK_END="# <<< XenithOS console session <<<"
GETTY_DIR=/etc/systemd/system/getty@tty1.service.d
STATE=/var/lib/xenithos

if [ "$UNINSTALL" = 1 ]; then
    say "Removing XenithOS"
    rm -f "$GETTY_DIR/xenithos-autologin.conf"
    rmdir "$GETTY_DIR" 2>/dev/null || true
    [ -f "$PROFILE" ] && sed -i "/$MARK_BEGIN/,/$MARK_END/d" "$PROFILE"
    rm -rf /usr/lib/xenithos /usr/share/xenithos
    rm -f /usr/bin/xenithos-session /usr/bin/xenithos-console
    rm -f /usr/share/applications/xenithos-console.desktop /usr/share/applications/xenithos.desktop
    if [ -f "$STATE/previous-target" ]; then systemctl set-default "$(cat "$STATE/previous-target")" >/dev/null; fi
    rm -rf "$STATE"
    systemctl daemon-reload
    echo "Done. Your XenithOS data is still in $DATA. Restart for a normal login."
    exit 0
fi

say "Installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
PKGS="build-essential libsdl2-dev zlib1g-dev curl ca-certificates tar"
[ "$DESKTOP" = 1 ] && PKGS="$PKGS xserver-xorg xinit xfce4 xfce4-terminal dbus-x11"
# shellcheck disable=SC2086
apt-get install -y $PKGS
# DOOM's free game data (Freedoom: Phase 1 and 2, in /usr/share/games/doom). Not every
# system has the package; DOOM can get it from the Store instead.
apt-get install -y freedoom || say "No freedoom package here; DOOM can get Freedoom from the Store"
# Apps from Flathub and Steam (the Store's Apps and Steam sections): Flatpak; cage, a small
# compositor that shows one app full screen in console mode; Xwayland for apps that still
# need X11; a D-Bus for the user's apps; and a sound server the apps and XenithOS can share,
# if the system has none.
APP_PKGS="flatpak cage xwayland dbus-user-session"
if ! dpkg -s pipewire-pulse >/dev/null 2>&1 && ! dpkg -s pulseaudio >/dev/null 2>&1; then
    APP_PKGS="$APP_PKGS pipewire pipewire-pulse wireplumber"
fi
# shellcheck disable=SC2086
apt-get install -y $APP_PKGS || say "Couldn't install all of: $APP_PKGS. XenithOS works without them, but apps from Flathub won't"

say "Getting the XenithOS source"
HERE=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
    HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
fi
if [ -n "$HERE" ] && [ -f "$HERE/Makefile" ] && [ -d "$HERE/../source" ]; then
    SRC=$(cd "$HERE/.." && pwd)
else
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT
    for url in $SOURCE_URL $GITHUB_URL; do
        if curl -fsSL "$url" -o "$WORK/src.tar.gz"; then break; fi
    done
    [ -s "$WORK/src.tar.gz" ] || die "couldn't download the source"
    mkdir -p "$WORK/src"
    tar -xzf "$WORK/src.tar.gz" -C "$WORK/src" --strip-components=1
    SRC="$WORK/src"
fi

say "Building (a minute or two on a Raspberry Pi)"
make -C "$SRC/linux" -j"$(nproc)"

say "Installing"
install -d /usr/lib/xenithos /usr/share/xenithos/apps "$STATE"
install -m 755 "$SRC/linux/xenithos" "$SRC/linux/volt" "$SRC/linux/doom" "$SRC/linux/tube" "$SRC/linux/photos" /usr/lib/xenithos/
# The programs belong to the XenithOS user, so the Store's "XenithOS update" can build a new
# version and put it in place without sudo (linux/update.sh). They only ever run as that user.
chown -R "$USER_NAME": /usr/lib/xenithos
install -m 755 "$SRC/linux/xenithos-session" "$SRC/linux/xenithos-console" /usr/bin/
install -m 644 "$SRC/linux/xenithos-console.desktop" "$SRC/linux/xenithos.desktop" /usr/share/applications/
for app in volt doom tube photos; do
    if [ -f "$SRC/art/thumbs/$app.ximg" ]; then install -m 644 "$SRC/art/thumbs/$app.ximg" /usr/share/xenithos/apps/; fi
    if [ -f "$SRC/apps/$app/app.txt" ]; then install -m 644 "$SRC/apps/$app/app.txt" "/usr/share/xenithos/apps/$app.txt"; fi
done
if [ -n "$SERVER" ]; then
    printf 'server=%s\nname=%s\n' "$SERVER" "$(hostname)" > /usr/share/xenithos/config.txt
fi

say "Setting up $USER_NAME"
sudo -u "$USER_NAME" mkdir -p "$DATA/uda:/apps" "$DATA/uda:/xenith" "$HOME_DIR/.config/xenithos"
for app in volt doom tube photos; do
    sudo -u "$USER_NAME" ln -sfn "/usr/lib/xenithos/$app" "$DATA/uda:/apps/$app.elf32"
    for ext in ximg txt; do
        if [ -f "/usr/share/xenithos/apps/$app.$ext" ]; then
            sudo -u "$USER_NAME" cp "/usr/share/xenithos/apps/$app.$ext" "$DATA/uda:/apps/$app.$ext"
        fi
    done
done
if [ -f /usr/share/xenithos/config.txt ] && [ ! -f "$DATA/uda:/xenith/config.txt" ]; then
    sudo -u "$USER_NAME" cp /usr/share/xenithos/config.txt "$DATA/uda:/xenith/config.txt"
fi
for g in video render input audio; do
    if getent group "$g" >/dev/null; then usermod -aG "$g" "$USER_NAME"; fi
done
# Flathub for the user's apps (the Store also adds it before the first install).
if command -v flatpak >/dev/null; then
    sudo -u "$USER_NAME" -H flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo \
        || say "Couldn't reach Flathub just now; the Store adds it when it installs the first app"
fi

say "Starting in console mode"
mkdir -p "$GETTY_DIR"
cat > "$GETTY_DIR/xenithos-autologin.conf" <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $USER_NAME --noclear %I \$TERM
EOF
touch "$PROFILE"
chown "$USER_NAME": "$PROFILE"
sed -i "/$MARK_BEGIN/,/$MARK_END/d" "$PROFILE"
cat >> "$PROFILE" <<EOF
$MARK_BEGIN
if [ -z "\$DISPLAY" ] && [ -z "\$WAYLAND_DISPLAY" ] && [ "\$(tty)" = /dev/tty1 ]; then
    exec /usr/bin/xenithos-session
fi
$MARK_END
EOF
# Machines that start on a graphical login screen would cover XenithOS with it.
[ -f "$STATE/previous-target" ] || systemctl get-default > "$STATE/previous-target"
systemctl set-default multi-user.target >/dev/null
systemctl daemon-reload

echo
echo "XenithOS is installed. Restart and it starts by itself:"
echo "    sudo reboot"
if [ "$DESKTOP" = 1 ]; then
    echo "Desktop mode is in XenithOS's power menu. Return to XenithOS from the desktop's menu."
fi
echo "To undo it later, run this installer again with --uninstall."
