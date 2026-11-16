#!/bin/bash
# Makes a XenithOS Desktop release's update channel: what installed PCs download to update.
# Runs in Docker (like build-in-docker.sh), on Windows (Git Bash), macOS or Linux.
#
#   bash desktop/release.sh [--notes "What's new, in a line"] [--channel URL]... [--server HOST:PORT] [--out DIR]
#                           [--version X.Y.Z]
#   bash desktop/release.sh --new-key        makes XenithOS Desktop's signing key (once, ever)
#
# --channel and --server are make-debs.sh's (the default update addresses are desktop/channel.txt's).
# --version with --out makes a try-out update: a higher version than desktop/VERSION's, in a
# folder of its own, to serve to a test PC before the real release.
#
# In desktop/out/channel/ afterwards, to put where desktop/channel.txt says:
#   manifest.txt, manifest.txt.sig                 the newest version and its bundle (signed)
#   xenithos-desktop-VERSION.update.tar, .sig      the bundle: the packages and their installer (signed)
#   get-update.sh                                  the first update for PCs installed before 1.1
# and in desktop/out/debs/, the packages themselves (the ISO build uses the same ones).
#
# The signing key lives outside the repository, in ~/.xenithos/desktop-signing-key.asc. Keep a
# copy of that file somewhere safe: without it, installed PCs can't be sent updates any more.
# Its public half is desktop/keys/xenithos-desktop.gpg, which goes into every install.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
OUT="$HERE/out"
KEY_DIR="${XENITHOS_KEY_DIR:-$HOME/.xenithos}"
NEW_KEY=0 NOTES="" PASS=()
# --out DIR: where it all goes (default desktop/out)
while [ $# -gt 0 ]; do
    case "$1" in
        --new-key) NEW_KEY=1 ;;
        --notes) NOTES="${2:-}"; shift ;;
        --channel|--server|--version) PASS+=("$1" "${2:-}"); shift ;;
        --out) OUT="${2:?}"; shift ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | grep '^#' | cut -c3-; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done
mkdir -p "$OUT" "$KEY_DIR"
OUT=$(cd "$OUT" && pwd)
command -v docker >/dev/null || { echo "Docker isn't installed (or isn't running)" >&2; exit 1; }

REPO_HOST=$REPO OUT_HOST=$OUT KEY_HOST=$KEY_DIR
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        export MSYS_NO_PATHCONV=1
        REPO_HOST=$(cygpath -w "$REPO"); OUT_HOST=$(cygpath -w "$OUT"); KEY_HOST=$(cygpath -w "$KEY_DIR") ;;
esac

if [ "$NEW_KEY" = 1 ]; then
    [ ! -f "$KEY_DIR/desktop-signing-key.asc" ] || { echo "There's already a key in $KEY_DIR. Installed PCs trust that one; a new one would cut them off." >&2; exit 1; }
    # shellcheck disable=SC2016
    docker run --rm -v "$KEY_HOST:/keys" -v "$OUT_HOST:/out" debian:trixie bash -c '
        set -e
        export DEBIAN_FRONTEND=noninteractive GNUPGHOME=/tmp/gnupg
        apt-get update -qq && apt-get install -y -qq --no-install-recommends gnupg >/dev/null
        mkdir -m 700 "$GNUPGHOME"
        gpg --batch --quiet --passphrase "" --quick-generate-key "XenithOS Desktop updates <xenithos@owencosta.diy>" ed25519 sign never
        gpg --batch --quiet --armor --export-secret-keys > /keys/desktop-signing-key.asc
        chmod 600 /keys/desktop-signing-key.asc
        gpg --batch --quiet --export > /out/xenithos-desktop.gpg
        gpg --list-keys --with-fingerprint | sed -n "2,4p"'
    mkdir -p "$HERE/keys"
    mv "$OUT/xenithos-desktop.gpg" "$HERE/keys/xenithos-desktop.gpg"
    echo "Made the signing key: $KEY_DIR/desktop-signing-key.asc (keep a copy somewhere safe)"
    echo "and its public half: desktop/keys/xenithos-desktop.gpg"
    exit 0
fi

[ -f "$KEY_DIR/desktop-signing-key.asc" ] || { echo "No signing key in $KEY_DIR. Make one with: bash desktop/release.sh --new-key" >&2; exit 1; }
# shellcheck disable=SC2016
SCRIPT='
set -e
export DEBIAN_FRONTEND=noninteractive GNUPGHOME=/tmp/gnupg
apt-get update -qq
apt-get install -y -qq --no-install-recommends git python3 python3-pil librsvg2-bin fonts-inter gnupg gpgv xz-utils > /dev/null
mkdir -m 700 "$GNUPGHOME"
gpg --batch --quiet --import /keys/desktop-signing-key.asc
rm -rf /out/debs /out/channel
mkdir -p /out/debs /out/channel
bash /src/desktop/make-debs.sh /out/debs "$@"
VERSION=$(basename /out/debs/xenithos-desktop-*.update.tar .update.tar)
VERSION=${VERSION#xenithos-desktop-}
CHANNELS=$(dpkg-deb --fsys-tarfile /out/debs/xenithos-desktop_${VERSION}_all.deb | tar -xO ./etc/xenithos-desktop/update.conf | sed -n "s/^channel=//p" | paste -sd" ")
[ -n "$CHANNELS" ] || { echo "The package names no update address" >&2; exit 1; }
cd /out/channel
mv /out/debs/xenithos-desktop-$VERSION.update.tar .
gpg --batch --yes --quiet --detach-sign --output xenithos-desktop-$VERSION.update.tar.sig xenithos-desktop-$VERSION.update.tar
{
    echo "version=$VERSION"
    echo "bundle=xenithos-desktop-$VERSION.update.tar"
    echo "sha256=$(sha256sum xenithos-desktop-$VERSION.update.tar | cut -d" " -f1)"
    echo "size=$(stat -c %s xenithos-desktop-$VERSION.update.tar)"
    echo "notes=$NOTES"
} > manifest.txt
gpg --batch --yes --quiet --detach-sign --output manifest.txt.sig manifest.txt
KEY=$(base64 -w 76 /src/desktop/keys/xenithos-desktop.gpg)
awk -v key="$KEY" -v channels="$CHANNELS" -v first="${CHANNELS%% *}" \
    "{ if (\$0 == \"@KEY@\") print key; else { gsub(/@CHANNELS@/, channels); gsub(/@CHANNEL@/, first); print } }" \
    /src/desktop/get-update.sh.in > get-update.sh
gpgv --keyring /src/desktop/keys/xenithos-desktop.gpg manifest.txt.sig manifest.txt 2>/dev/null
gpgv --keyring /src/desktop/keys/xenithos-desktop.gpg xenithos-desktop-$VERSION.update.tar.sig xenithos-desktop-$VERSION.update.tar 2>/dev/null
echo "== Channel for: $CHANNELS"
ls -la /out/channel
cat manifest.txt
'
docker run --rm -e NOTES="$NOTES" -v "$REPO_HOST:/src:ro" -v "$OUT_HOST:/out" -v "$KEY_HOST:/keys:ro" \
    debian:trixie bash -c "$SCRIPT" release ${PASS[@]+"${PASS[@]}"}
