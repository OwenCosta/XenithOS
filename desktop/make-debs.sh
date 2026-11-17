#!/bin/bash
# Builds XenithOS Desktop's own packages (.deb) and the update bundle made of them. The ISO
# installs these same packages, so a PC installed from the ISO and one brought up to date by an
# update end up alike.
#
#   bash desktop/make-debs.sh OUT_DIR [--server HOST:PORT] [--channel URL]... [--version X.Y.Z]
#
#   xenithos-archive       where Brave's and Mullvad's packages come from (apt sources and keys)
#   xenithos-desktop         the desktop: its session, look, Settings and updater (rootfs/ and the art)
#   xenithos-desktop-apps    the apps it comes with (packages/apps.list)
#   xenithos-security-tools  the security tools it comes with (packages/security.list)
#   xenithos-desktop-VERSION.update.tar   the four of them with install.sh: what the updater downloads
#
# --server  the XenithOS Cloud address XenithOS Console starts with (default: "server=" in the
#           repository's config.local.txt, which is never committed)
# --channel where installed PCs look for updates (default: the addresses in desktop/channel.txt;
#           give it more than once for a spare)
# --version a version other than the one in desktop/VERSION (to try an update out on a test PC)
# Needs dpkg-deb, python3-pil, librsvg2-bin and fonts-inter (build-in-docker.sh installs them).
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
OUT=${1:?usage: make-debs.sh OUT_DIR [--server HOST:PORT] [--channel URL]}
shift
SERVER="" CHANNELS=() VERSION=""
while [ $# -gt 0 ]; do
    case "$1" in
        --server) SERVER="${2:-}"; shift ;;
        --version) VERSION="${2:?--version needs a version}"; shift ;;
        --channel) CHANNELS+=("${2:?--channel needs an address}"); shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done
[ -n "$VERSION" ] || VERSION=$(tr -d ' \r\n' < "$HERE/VERSION")
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]] || { echo "The version (desktop/VERSION) should look like 1.2.3" >&2; exit 1; }
if [ -z "$SERVER" ] && [ -f "$REPO/config.local.txt" ]; then
    SERVER=$(sed -n 's/^server=//p' "$REPO/config.local.txt" | tr -d '\r' | head -n 1)
fi
if [ "${#CHANNELS[@]}" = 0 ]; then
    mapfile -t CHANNELS < <(grep -v '^[[:space:]]*#' "$HERE/channel.txt" | tr -d ' \r' | grep . || true)
fi
[ "${#CHANNELS[@]}" -gt 0 ] || { echo "desktop/channel.txt names no update address" >&2; exit 1; }
for c in "${CHANNELS[@]}"; do
    case "$c" in http://*|https://*) ;; *) echo "An update address should start with https:// ($c)" >&2; exit 1 ;; esac
done
[ -f "$HERE/keys/xenithos-desktop.gpg" ] || { echo "desktop/keys/xenithos-desktop.gpg is missing (desktop/release.sh --new-key makes it)" >&2; exit 1; }
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -c safe.directory='*' -C "$REPO" log -1 --format=%ct 2>/dev/null || date +%s)}

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# A list file's packages as a Depends line.
depends() { grep -hv '^[[:space:]]*#' "$@" | awk 'NF { print $1 }' | sort -u | paste -sd, | sed 's/,/, /g'; }
# Copied without the source's permissions (a checkout on Windows, seen from Docker, has every
# file executable by everyone): directories 755, files 644, scripts 755.
copy_tree() { mkdir -p "$2"; cp -r --no-preserve=mode,ownership "$1/." "$2/"; }
fix_modes() {
    find "$1" -type d -exec chmod 755 {} +
    find "$1" -type f -exec chmod 644 {} +
    find "$1" -type f -print0 | while IFS= read -r -d '' f; do
        if [ "$(head -c 2 "$f")" = '#!' ]; then chmod 755 "$f"; fi
    done
}
control() {    # package, architecture, depends, description, long description
    mkdir -p "$WORK/$1/DEBIAN"
    {
        echo "Package: $1"
        echo "Version: $VERSION"
        echo "Architecture: $2"
        echo "Maintainer: XenithOS <xenithos@owencosta.diy>"
        echo "Section: misc"
        echo "Priority: optional"
        echo "Homepage: https://owencosta.diy/xenithos/"
        [ -n "$3" ] && echo "Depends: $3"
        echo "Description: $4"
        echo " $5"
    } > "$WORK/$1/DEBIAN/control"
    for script in postinst postrm preinst prerm triggers; do
        if [ -f "$HERE/debian/$1.$script" ]; then
            install -m "$([ "$script" = triggers ] && echo 644 || echo 755)" "$HERE/debian/$1.$script" "$WORK/$1/DEBIAN/$script"
        fi
    done
}
build() {
    find "$WORK/$1" -newermt "@$SOURCE_DATE_EPOCH" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    dpkg-deb --root-owner-group -Zxz --build "$WORK/$1" "$OUT/$1_${VERSION}_$2.deb" >/dev/null
    echo "  $1_${VERSION}_$2.deb"
}

echo "== XenithOS Desktop $VERSION packages"

# ---- xenithos-archive
copy_tree "$HERE/archive" "$WORK/xenithos-archive"
fix_modes "$WORK/xenithos-archive"
control xenithos-archive all "ca-certificates" "Package sources for XenithOS Desktop's apps" \
    "Where Brave and Mullvad VPN come from: their makers' own apt sources and keys."
build xenithos-archive all

# ---- xenithos-desktop
D="$WORK/xenithos-desktop"
copy_tree "$HERE/rootfs" "$D"
python3 "$HERE/art/make-art.py" "$WORK/art" "$REPO/tools" >/dev/null
copy_tree "$WORK/art/rootfs" "$D"
install -D -m 644 "$HERE/keys/xenithos-desktop.gpg" "$D/usr/share/keyrings/xenithos-desktop.gpg"
sed -i "s|@VERSION@|$VERSION|g" "$D/usr/share/xenithos-desktop/os-release"
sed -i '/@XENITHOS_CHANNEL@/d' "$D/etc/xenithos-desktop/update.conf"
printf 'channel=%s\n' "${CHANNELS[@]}" >> "$D/etc/xenithos-desktop/update.conf"
mkdir -p "$D/usr/share/xenithos"
if [ -n "$SERVER" ]; then
    printf 'server=%s\n' "$SERVER" > "$D/usr/share/xenithos/config.txt"
else
    echo "  (no XenithOS Cloud address: XenithOS Console will say it isn't set up. Pass --server.)"
fi
cat > "$D/usr/share/xenithos-desktop/build.txt" <<EOF
XenithOS Desktop $VERSION
built $(date -u -d "@$SOURCE_DATE_EPOCH" '+%Y-%m-%d %H:%M UTC')
EOF
# The other highlight colours (Settings > Look): a theme each, sharing XenithOS's gtk-main.css.
python3 - "$D" "$HERE/rootfs/usr/lib/xenithos-desktop/xui.py" <<'EOF'
import os, re, sys
root, xui = sys.argv[1], open(sys.argv[2], encoding="utf-8").read()
block = xui[xui.index("ACCENTS = ["):xui.index("]", xui.index("ACCENTS = ["))]
for name, colour in re.findall(r'\("(\w+)", "(#[0-9a-fA-F]{6})"\)', block):
    if name == "Red":
        continue
    r, g, b = (int(colour[i:i + 2], 16) for i in (1, 3, 5))
    on = "#07080a" if 0.299 * r + 0.587 * g + 0.114 * b > 150 else "#ffffff"
    d = os.path.join(root, "usr/share/themes", f"XenithOS-{name}")
    os.makedirs(os.path.join(d, "gtk-3.0"), exist_ok=True)
    with open(os.path.join(d, "gtk-3.0", "accent.css"), "w") as f:
        f.write(f"@define-color xenith_accent {colour};\n@define-color xenith_on_accent {on};\n")
    with open(os.path.join(d, "gtk-3.0", "gtk.css"), "w") as f:
        f.write(f"/* XenithOS's theme with {name.lower()} as the highlight colour (Settings > Look). */\n"
                '@import url("accent.css");\n@import url("../../XenithOS/gtk-3.0/gtk-main.css");\n')
    with open(os.path.join(d, "index.theme"), "w") as f:
        f.write(f"[Desktop Entry]\nType=X-GNOME-Metatheme\nName=XenithOS-{name}\nComment=XenithOS's look with {name.lower()} highlights\n"
                f"Encoding=UTF-8\n\n[X-GNOME-Metatheme]\nGtkTheme=XenithOS-{name}\nMetacityTheme=XenithOS\nIconTheme=XenithOS\n")
EOF
fix_modes "$D"
chmod 440 "$D/etc/sudoers.d/xenithos-desktop"
chmod 600 "$D/etc/netplan/01-network-manager-all.yaml"       # netplan wants its files private
printf 'interest-noawait /usr/share/applications\ninterest-noawait /usr/lib/x86_64-linux-gnu/libgtk-3.so.0\n' > "$WORK/triggers"
control xenithos-desktop all "$(depends "$HERE/packages/desktop.list"), xenithos-archive (>= $VERSION)" \
    "XenithOS Desktop: the desktop, its look, Settings and updates" \
    "The session that starts the desktop and switches to XenithOS Console, XenithOS's look for XFCE, Settings, and the updater."
install -m 644 "$WORK/triggers" "$D/DEBIAN/triggers"
build xenithos-desktop all

# ---- the apps and the security tools: packages that only bring others
control xenithos-desktop-apps all "$(depends "$HERE/packages/apps.list"), xenithos-desktop (>= $VERSION)" \
    "The apps XenithOS Desktop comes with" "Brave, Mullvad VPN and the everyday apps. Removing this package leaves the apps installed."
build xenithos-desktop-apps all
# (with the list itself: the Apps menu's Security Tools group is made from it)
install -D -m 644 "$HERE/packages/security.list" "$WORK/xenithos-security-tools/usr/share/xenithos-desktop/security-tools.list"
control xenithos-security-tools all "$(depends "$HERE/packages/security.list"), xenithos-archive (>= $VERSION)" \
    "The security tools XenithOS Desktop comes with" "Network, web, password, Wi-Fi, forensics and privacy tools from Ubuntu's archive. Removing this package leaves them installed."
build xenithos-security-tools all

# ---- the update bundle
B="$WORK/bundle"
mkdir -p "$B"
cp "$OUT"/xenithos-archive_"${VERSION}"_all.deb "$OUT"/xenithos-desktop_"${VERSION}"_all.deb \
   "$OUT"/xenithos-desktop-apps_"${VERSION}"_all.deb "$OUT"/xenithos-security-tools_"${VERSION}"_all.deb "$B/"
install -m 755 "$HERE/bundle-install.sh" "$B/install.sh"
echo "$VERSION" > "$B/VERSION"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    -cf "$OUT/xenithos-desktop-$VERSION.update.tar" -C "$B" .
echo "  xenithos-desktop-$VERSION.update.tar"
