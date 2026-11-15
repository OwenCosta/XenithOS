#!/bin/bash
# Sets up, or updates, what Tube needs on a XenithOS Cloud server. Run it as root:
#   ffmpeg, Deno, the yt-dlp standalone build, and bgutil's PO token provider (a yt-dlp
#   plugin plus its token server, xenith-pot.service, which Deno runs).
# Run it again later to move to the newest releases: YouTube changes often and yt-dlp keeps
# up (on the server: xenith tube-update). Every download is checked against the checksum
# its project publishes. To pin versions:
#   DENO_VERSION=v2.9.8 YTDLP_VERSION=2026.10.26 POT_VERSION=2.0.2 ./setup-tube.sh
set -euo pipefail

O=${XENITH_HOME:-/opt/xenith-cloud}
ENV_FILE=${XENITH_ENV_FILE:-/etc/xenith-cloud.env}
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "Run this as root."; exit 1; }
[ -f "$HERE/xenith-pot.service" ] || { echo "xenith-pot.service should be next to this script."; exit 1; }
case "$(uname -m)" in
    x86_64)  DENO_ZIP="deno-x86_64-unknown-linux-gnu.zip";  YTDLP_ZIP="yt-dlp_linux.zip" ;;
    aarch64) DENO_ZIP="deno-aarch64-unknown-linux-gnu.zip"; YTDLP_ZIP="yt-dlp_linux_aarch64.zip" ;;
    *) echo "Tube's helpers have no build for $(uname -m)."; exit 1 ;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"

latest() {      # owner/repo -> its newest release tag
    curl -fsS -m 20 "https://api.github.com/repos/$1/releases/latest" |
        python3 -c 'import json, sys; print(json.load(sys.stdin)["tag_name"])'
}
recorded() {    # owner/repo tag file -> the sha256 GitHub recorded when the file was uploaded
    curl -fsS -m 20 "https://api.github.com/repos/$1/releases/tags/$2" | python3 -c '
import json, sys
for a in json.load(sys.stdin)["assets"]:
    if a["name"] == sys.argv[1]:
        print((a.get("digest") or "").replace("sha256:", ""))' "$3"
}
fetch() { curl -fsSL --retry 2 -m 600 -o "$2" "$1"; }
matches() {     # expected-sha256 file
    [ -n "$1" ] && [ "$1" = "$(sha256sum "$2" | cut -d' ' -f1)" ] || { echo "Checksum mismatch: $2"; exit 1; }
}
setting() {     # KEY VALUE: set one line of the env file and keep the rest (it holds secrets)
    ( umask 077
      { grep -v "^$1=" "$ENV_FILE" 2>/dev/null || true; echo "$1=$2"; } > "$ENV_FILE.new"
      cat "$ENV_FILE.new" > "$ENV_FILE"
      rm -f "$ENV_FILE.new" )
}
switch() {      # name folder: point $O/name at a folder in $O/versions (a real folder there is moved aside)
    if [ -e "$O/$1" ] && [ ! -L "$O/$1" ]; then rm -rf "$O/$1.replaced"; mv "$O/$1" "$O/$1.replaced"; fi
    ln -sfn "versions/$2" "$O/$1"
}
prune() {       # prefix keep keep: remove other versions of one thing
    for d in "$O/versions/$1"*; do
        [ -d "$d" ] || continue
        case "$(basename "$d")" in "$2"|"$3") ;; *) rm -rf "$d" ;; esac
    done
}

missing=""
for p in ffmpeg unzip git curl python3; do command -v $p >/dev/null || missing="$missing $p"; done
if [ -n "$missing" ]; then
    apt-get update -q
    apt-get install -y -q $missing
fi

DENO_VERSION=${DENO_VERSION:-$(latest denoland/deno)}
YTDLP_VERSION=${YTDLP_VERSION:-$(latest yt-dlp/yt-dlp)}
POT_VERSION=${POT_VERSION:-$(latest Brainicism/bgutil-ytdlp-pot-provider)}
echo "Deno $DENO_VERSION, yt-dlp $YTDLP_VERSION, PO token provider $POT_VERSION"
mkdir -p "$O/bin" "$O/versions" "$O/yt-dlp-plugins"

# Deno: yt-dlp runs it for YouTube's JavaScript challenges, and it runs the token server
if [ "$("$O/bin/deno" --version 2>/dev/null | head -1 | cut -d' ' -f2)" != "${DENO_VERSION#v}" ]; then
    fetch "https://github.com/denoland/deno/releases/download/$DENO_VERSION/$DENO_ZIP" "$DENO_ZIP"
    fetch "https://github.com/denoland/deno/releases/download/$DENO_VERSION/$DENO_ZIP.sha256sum" deno.sum
    matches "$(grep -oE '[0-9a-fA-F]{64}' deno.sum | head -1 | tr A-F a-f)" "$DENO_ZIP"
    unzip -q -o "$DENO_ZIP" -d deno
    install -m 755 deno/deno "$O/bin/deno.new"
    mv "$O/bin/deno.new" "$O/bin/deno"
fi

# yt-dlp, the standalone build: it brings its own Python and the challenge solver scripts
yt="yt-dlp-$YTDLP_VERSION"
if [ ! -f "$O/versions/$yt/.complete" ]; then
    fetch "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_VERSION/$YTDLP_ZIP" "$YTDLP_ZIP"
    fetch "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_VERSION/SHA2-256SUMS" sums
    matches "$(awk -v f="$YTDLP_ZIP" '$2 == f || $2 == "*" f {print $1}' sums)" "$YTDLP_ZIP"
    rm -rf "$O/versions/$yt"
    mkdir -p "$O/versions/$yt"
    unzip -q "$YTDLP_ZIP" -d "$O/versions/$yt"
    touch "$O/versions/$yt/.complete"
fi
ytdlp_exe=$(cd "$O/versions/$yt" && find . -maxdepth 1 -type f -name 'yt-dlp*' -perm -u+x | head -1 | cut -c3-)
[ -n "$ytdlp_exe" ] && "$O/versions/$yt/$ytdlp_exe" --version >/dev/null

# The PO token provider: the yt-dlp plugin and its token server come from the same release
pz=bgutil-ytdlp-pot-provider.zip
fetch "https://github.com/Brainicism/bgutil-ytdlp-pot-provider/releases/download/$POT_VERSION/$pz" "$pz"
matches "$(recorded Brainicism/bgutil-ytdlp-pot-provider "$POT_VERSION" "$pz")" "$pz"
pot="pot-provider-$POT_VERSION"
if [ ! -f "$O/versions/$pot/.complete" ]; then
    rm -rf "$O/versions/$pot"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$POT_VERSION" \
        https://github.com/Brainicism/bgutil-ytdlp-pot-provider.git "$O/versions/$pot"
    ( cd "$O/versions/$pot/server" &&
      DENO_DIR="$O/versions/$pot/deno-dir" DENO_NO_UPDATE_CHECK=1 \
          "$O/bin/deno" install --allow-scripts=npm:canvas --frozen ) > deno-install.log 2>&1 ||
        { tail -20 deno-install.log; rm -rf "$O/versions/$pot"; exit 1; }
    touch "$O/versions/$pot/.complete"
fi

# Switch over
old_yt=$(basename "$(readlink "$O/yt-dlp" 2>/dev/null || echo none)")
old_pot=$(basename "$(readlink "$O/pot-provider" 2>/dev/null || echo none)")
switch yt-dlp "$yt"
switch pot-provider "$pot"
install -m 644 "$pz" "$O/yt-dlp-plugins/$pz"
install -m 644 "$HERE/xenith-pot.service" /etc/systemd/system/xenith-pot.service
systemctl daemon-reload
systemctl enable -q xenith-pot
systemctl restart xenith-pot

before=$(sha256sum "$ENV_FILE" 2>/dev/null || true)
setting XENITH_YTDLP "$O/yt-dlp/$ytdlp_exe"
setting XENITH_DENO "$O/bin/deno"
setting XENITH_YTDLP_PLUGINS "$O/yt-dlp-plugins"
if [ "$before" != "$(sha256sum "$ENV_FILE")" ] && systemctl is-active -q xenith-cloud; then
    systemctl restart xenith-cloud
    echo "XenithOS Cloud restarted with the new settings"
fi

for _ in $(seq 1 60); do curl -fsS -m 2 http://127.0.0.1:4416/ping >/dev/null 2>&1 && break; sleep 1; done
answer=$(curl -fsS -m 3 http://127.0.0.1:4416/ping 2>/dev/null || true)
case "$answer" in
    *"\"version\":\"$POT_VERSION\""*) echo "Token server $POT_VERSION is answering" ;;
    "") echo "The token server isn't answering yet: journalctl -u xenith-pot"; exit 1 ;;
    *) echo "The token server answered with another version: $answer"; exit 1 ;;
esac
echo "$("$O/bin/deno" --version | head -1); yt-dlp $("$O/yt-dlp/$ytdlp_exe" --version)"
prune yt-dlp- "$yt" "$old_yt"
prune pot-provider- "$pot" "$old_pot"
