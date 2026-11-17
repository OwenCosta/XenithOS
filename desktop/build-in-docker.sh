#!/bin/bash
# Builds the XenithOS Desktop ISO inside Docker, on any machine that has it: Windows (Git Bash or
# WSL), macOS or Linux. It runs desktop/build.sh in a Debian 13 container (debian:trixie), which
# has the live-build that makes the ISO. The ISO itself is Ubuntu 24.04.
#
#   bash desktop/build-in-docker.sh [--ram] [build.sh options]
#
# The ISO, its SHA-256 and the build log land in desktop/out/.
#   --ram   build in memory instead of on Docker's disk. Needs about 10 GB free for Docker, but
#           Docker's disk doesn't grow by the 8 GB or so a build uses (on Windows and macOS that
#           space isn't given back by itself).
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
OUT="$HERE/out"
IMAGE=debian:trixie
RAM=0
ARGS=()
for a in "$@"; do
    if [ "$a" = --ram ]; then RAM=1; else ARGS+=("$a"); fi
done
# The build lists XenithOS Console's source with git. In a git worktree, .git is a pointer to the
# main checkout's folder, which the container doesn't have: the build would stop a minute in.
if [ -f "$REPO/.git" ]; then
    echo "This checkout is a git worktree, and the build can't read it from inside Docker." >&2
    echo "Build from a clone instead: git clone \"$REPO\" another-folder, then run this there." >&2
    exit 1
fi
mkdir -p "$OUT"
command -v docker >/dev/null || { echo "Docker isn't installed (or isn't running)" >&2; exit 1; }

# Git Bash on Windows: hand Docker Windows paths, and keep Git Bash from rewriting the
# container's own paths (/src, /out) into Windows ones.
REPO_HOST=$REPO
OUT_HOST=$OUT
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        export MSYS_NO_PATHCONV=1
        REPO_HOST=$(cygpath -w "$REPO")
        OUT_HOST=$(cygpath -w "$OUT")
        ;;
esac

# Inside the container: the tools build.sh needs, then build.sh itself. The work folder is in
# the container (or in memory with --ram), so nothing is written into the checkout but out/.
# shellcheck disable=SC2016
SCRIPT='
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends live-build debootstrap ubuntu-keyring git python3 python3-pil \
    librsvg2-bin fonts-inter grub-common xorriso squashfs-tools mtools dosfstools cpio \
    ca-certificates > /dev/null
mkdir -p /work
if [ "$RAM" = 1 ]; then mount -t tmpfs -o size=14g tmpfs /work; fi
exec bash /src/desktop/build.sh --work /work --out /out "$@"
'
docker run --rm --privileged \
    -e RAM="$RAM" \
    -v "$REPO_HOST:/src:ro" \
    -v "$OUT_HOST:/out" \
    "$IMAGE" bash -c "$SCRIPT" build-in-docker ${ARGS[@]+"${ARGS[@]}"}
