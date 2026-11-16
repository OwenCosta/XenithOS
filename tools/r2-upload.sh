#!/bin/bash
# Puts files in XenithOS's Cloudflare R2 bucket, where https://dl.owencosta.diy/<name> serves them.
#
#   bash tools/r2-upload.sh FILE...        in Git Bash, from the repo
#
# It asks for the R2 API token's values (hiding the keys as you paste them), unless R2_ACCOUNT_ID,
# R2_ACCESS_KEY_ID and R2_SECRET_ACCESS_KEY are set. Each file keeps its name in the bucket and
# replaces one there with that name. R2 checks every file against its MD5 as it arrives and
# refuses one that came through damaged. Only curl 7.75 or newer is needed (Git Bash has it).
#
# R2_BUCKET (default xenithos), R2_PUBLIC (default https://dl.owencosta.diy) and R2_ENDPOINT
# (default https://<account>.r2.cloudflarestorage.com) change where things go.
set -euo pipefail

BUCKET="${R2_BUCKET:-xenithos}"
PUBLIC="${R2_PUBLIC:-https://dl.owencosta.diy}"

die() { printf 'r2-upload: %s\n' "$*" >&2; exit 1; }

[ $# -gt 0 ] || die "name the files to upload, for example: bash tools/r2-upload.sh desktop/out/xenithos-desktop-2.0.1-amd64.iso"
for f in "$@"; do
    [ -f "$f" ] || die "there's no file $f"
    [[ "$(basename "$f")" =~ ^[A-Za-z0-9._-]+$ ]] || die "$f: use a file name of letters, digits, dots, dashes and underscores"
done
command -v curl >/dev/null || die "this needs curl"
command -v openssl >/dev/null || die "this needs openssl (Git Bash has it)"

# ask VAR "prompt" hidden: the variable's value if it's set, otherwise what's typed or pasted.
ask() {
    local v="${!1:-}"
    if [ -z "$v" ]; then
        if [ "$3" = 1 ]; then read -r -s -p "$2: " v; echo >&2; else read -r -p "$2: " v; fi
    fi
    v="${v//[[:space:]]/}"
    printf '%s' "$v"
}

ACCOUNT=$(ask R2_ACCOUNT_ID "Account ID (or the whole S3 address R2 showed)" 0)
# The token page shows the S3 address, https://<account>.r2.cloudflarestorage.com: take the ID from it.
ACCOUNT="${ACCOUNT#https://}"
ACCOUNT="${ACCOUNT%%.*}"
[[ "$ACCOUNT" =~ ^[0-9a-fA-F]{32}$ ]] || die "that account ID doesn't look right: it's 32 letters and digits"
KEY_ID=$(ask R2_ACCESS_KEY_ID "Access Key ID (not shown)" 1)
[[ "$KEY_ID" =~ ^[A-Za-z0-9]{16,128}$ ]] || die "that Access Key ID doesn't look right"
SECRET=$(ask R2_SECRET_ACCESS_KEY "Secret Access Key (not shown)" 1)
[[ "$SECRET" =~ ^[A-Za-z0-9/+=]{16,128}$ ]] || die "that Secret Access Key doesn't look right"
ENDPOINT="${R2_ENDPOINT:-https://$ACCOUNT.r2.cloudflarestorage.com}"

# The keys reach curl through its standard input, so they never appear in a command line.
creds() { printf 'user = "%s:%s"\n' "$KEY_ID" "$SECRET"; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for f in "$@"; do
    name=$(basename "$f")
    case "$name" in
        *.iso) type="application/x-iso9660-image" ;;
        *.sha256|*.txt|*.md|*.ps1|*.sh) type="text/plain; charset=utf-8" ;;
        *) type="application/octet-stream" ;;
    esac
    size=$(wc -c < "$f" | tr -d ' ')
    echo "$name: $size bytes. Working out its MD5..."
    md5=$(openssl dgst -md5 -binary "$f" | openssl base64)
    echo "Uploading $name to $BUCKET (a large file takes a while; the bar shows how far it's got)"
    code=$(creds | curl -K - --aws-sigv4 "aws:amz:auto:s3" \
        -H "Content-Type: $type" \
        -H "Content-Disposition: attachment; filename=\"$name\"" \
        -H "Content-MD5: $md5" \
        --progress-bar -T "$f" -o "$TMP/reply" -w '%{http_code}' \
        "$ENDPOINT/$BUCKET/$name") || code="${code:-000}"
    if [ "$code" != 200 ]; then
        echo "R2 said no ($code):" >&2
        sed -e 's/<[^>]*>/ /g' "$TMP/reply" 2>/dev/null | tr -s ' ' | head -c 600 >&2 || true
        echo >&2
        die "$name wasn't uploaded"
    fi
    # Ask R2 what it now holds under that name.
    stored=$(creds | curl -sS -K - --aws-sigv4 "aws:amz:auto:s3" -I "$ENDPOINT/$BUCKET/$name" \
        | tr -d '\r' | sed -n 's/^[Cc]ontent-[Ll]ength: *//p' | tail -1)
    [ "$stored" = "$size" ] || die "$name: R2 holds ${stored:-nothing} bytes, not $size"
    echo "Done: $PUBLIC/$name"
done
