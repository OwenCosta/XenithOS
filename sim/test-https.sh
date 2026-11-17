#!/bin/bash
# XenithOS Cloud over HTTPS on PCs and Raspberry Pis, end to end with nothing on the internet: a
# local XenithOS Cloud, sim/tls_proxy.py standing in for Cloudflare's edge and tunnel (HTTPS with a
# certificate from a test authority made here, passed on to the server's edge port with
# CF-Connecting-IP), and the simulator as a PC. The proxy names every console 203.0.113.50, so the
# server's log says which way each request came; plain ones come from 127.0.0.1.
#
#   sim/test-https.sh          needs openssl (the command) and python3; nothing is installed
#
# It checks that a PC with an old config.txt (a plain address) moves to HTTPS once XenithOS Cloud
# offers it and it answers there, then syncs its profile (the token) and creates one (the
# passcode) over HTTPS; that its apps (Tube) do too (route.txt); and that the Store's downloads
# arrive whole (8 MB of DOOM game data). A certificate the PC doesn't trust, or one for another
# name, keeps it on plain HTTP, and so does a server that stops offering HTTPS. An Xbox never
# tries it. A config.txt that says https:// is used as it is. At home the home-network route
# still comes first, with HTTPS as the way out when the home address stops answering.
# Screenshots end up in sim/out/https.
repo="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/xenith-https-test
CLOUD=18795 EDGE=18797 TLS=18443
EDGE_IP=203.0.113.50
HTTPS_ADDR="localhost:$TLS"
HOME_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
CLOUD_PID= PROXY_PID= MARK=0
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }

command -v openssl >/dev/null || { echo "needs openssl (the command) for the test certificates"; exit 2; }
make -s -C "$repo/sim" xsim xtube || exit 2

rm -rf "$T"
mkdir -p "$T/store" "$T/web" "$T/dl" "$T/shots"
: > "$T/cloud.log"

# A test certificate authority, and certificates from it for localhost and for another name.
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2 -subj "/CN=XenithOS test authority" \
    -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -keyout "$T/ca.key" -out "$T/ca.pem" 2>/dev/null
for name in localhost wrong.test; do
    openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -subj "/CN=$name" \
        -keyout "$T/$name.key" -out "$T/$name.csr" 2>/dev/null
    printf 'subjectAltName=DNS:%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' "$name" > "$T/$name.ext"
    openssl x509 -req -in "$T/$name.csr" -CA "$T/ca.pem" -CAkey "$T/ca.key" -CAcreateserial -days 2 \
        -extfile "$T/$name.ext" -out "$T/$name.pem" 2>/dev/null
done
[ -s "$T/localhost.pem" ] && [ -s "$T/wrong.test.pem" ] || { echo "couldn't make the test certificates"; exit 2; }

# The Store: DOOM with game data of a few MB (random), for a big download over HTTPS, and DOOM's
# program in a folder standing in for a PC's /usr/lib/xenithos (sim/run.sh does the same).
head -c 3000000 /dev/urandom > "$T/store/freedoom1.wad"
head -c 5000000 /dev/urandom > "$T/store/freedoom2.wad"
head -c 4096 /dev/urandom > "$T/store/doom.elf32"
cat > "$T/store/store.json" <<'EOF'
[{"id": "doom", "kind": "app", "name": "DOOM", "version": "1.7", "file": "doom.elf32", "description": "DOOM.",
  "needs": ["freedoom1", "freedoom2"]},
 {"id": "freedoom1", "kind": "data", "name": "Freedoom: Phase 1", "version": "1", "file": "freedoom1.wad"},
 {"id": "freedoom2", "kind": "data", "name": "Freedoom: Phase 2", "version": "1", "file": "freedoom2.wad"}]
EOF
mkdir -p "$T/lib"
: > "$T/lib/doom"
cp "$repo/apps/doom/app.txt" "$T/lib/doom.txt"
if [ -f "$repo/art/thumbs/doom.ximg" ]; then cp "$repo/art/thumbs/doom.ximg" "$T/lib/doom.ximg"; fi

stop_cloud() {
    if [ -n "$CLOUD_PID" ]; then kill "$CLOUD_PID" 2>/dev/null; wait "$CLOUD_PID" 2>/dev/null; fi
    CLOUD_PID=
}
start_cloud() {                  # $1: XENITH_PUBLIC_HTTPS, $2: XENITH_LAN_ADDR ("" for none)
    stop_cloud
    XENITH_PORT=$CLOUD XENITH_EDGE_PORT=$EDGE XENITH_PUBLIC_HTTPS="$1" XENITH_LAN_ADDR="$2" \
        XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" XENITH_DOWNLOAD_DIR="$T/dl" \
        XENITH_YTDLP=/nonexistent XENITH_FFMPEG=/nonexistent python3 -I -u "$repo/server/xenith_cloud.py" >> "$T/cloud.log" 2>&1 &
    CLOUD_PID=$!
    for _ in $(seq 50); do
        python3 -I -c 'import sys, urllib.request; urllib.request.urlopen(sys.argv[1], timeout=1)' \
            "http://127.0.0.1:$CLOUD/v1/ping" 2>/dev/null && return
        sleep 0.1
    done
    echo "XenithOS Cloud didn't start:"; tail -5 "$T/cloud.log"; exit 2
}
stop_proxy() {
    if [ -n "$PROXY_PID" ]; then kill "$PROXY_PID" 2>/dev/null; wait "$PROXY_PID" 2>/dev/null; fi
    PROXY_PID=
}
start_proxy() {                  # $1: whose certificate (localhost or wrong.test)
    stop_proxy
    python3 -I "$repo/sim/tls_proxy.py" $TLS "$T/$1.pem" "$T/$1.key" $EDGE $EDGE_IP >> "$T/proxy.log" 2>&1 &
    PROXY_PID=$!
    sleep 0.5
}
trap 'stop_proxy; stop_cloud' EXIT

machine() {                      # $1: its name, $2: config.txt's server, $3: 1 to be signed in
    rm -rf "${T:?}/$1"
    mkdir -p "$T/$1/uda:/xenith" "$T/$1/uda:/apps"
    printf 'server=%s\nname=SIM\n' "$2" > "$T/$1/uda:/xenith/config.txt"
    if [ "$3" = 1 ]; then cp "$T/profile.txt" "$T/$1/uda:/xenith/profile.txt"; fi
}
run() {                          # $1: the machine, $2: the script, $3: the program (default xsim),
    local shots="${4:-$1}"       # $4: a name for its screenshots (default the machine's)
    mkdir -p "$T/shots/$shots"
    MARK=$(wc -l < "$T/cloud.log")
    ( cd "$T/$1" && XSIM_SCRIPT="$repo/sim/scripts/$2" XSIM_OUT="$T/shots/$shots" ASAN_OPTIONS=detect_leaks=0 \
        "$repo/sim/${3:-xsim}" >> "$T/sim-$1.log" 2>&1 )
    echo "  ($1: $2 finished with $?)"
}
since() { tail -n +"$((MARK + 1))" "$T/cloud.log"; }
came() { since | grep -qF "$1 \"$2 "; }              # $1: from, $2: "POST /v1/accounts/me"
tunnel_used() { since | grep -q "^$EDGE_IP "; }
route() { cat "$T/$1/uda:/xenith/route.txt" 2>/dev/null; }
no_route() { [ ! -e "$T/$1/uda:/xenith/route.txt" ]; }

start_cloud "$HTTPS_ADDR" ""
start_proxy localhost
token=$(python3 -I -c '
import sys, urllib.request
r = urllib.request.urlopen(sys.argv[1], data=b"name=Tester&pin=4321&avatar=1202001000", timeout=5)
print(dict(line.split("=", 1) for line in r.read().decode().splitlines())["token"])' \
    "http://127.0.0.1:$CLOUD/v1/accounts/create")
[ -n "$token" ] || { echo "couldn't make the test profile"; exit 2; }
printf 'name=Tester\ntoken=%s\navatar=1202001000\njoined=2026-11-16\n' "$token" > "$T/profile.txt"

echo "== a PC with an old config.txt (a plain address), XenithOS Cloud offering https://$HTTPS_ADDR"
machine pc "127.0.0.1:$CLOUD" 1
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run pc https.txt
check "it asked the plain address what else there is" 'came 127.0.0.1 "GET /v1/lan"'
check "the HTTPS address answered as the same server" 'came $EDGE_IP "GET /v1/lan"'
check "its token went over HTTPS (the profile sync)" \
      'came $EDGE_IP "POST /v1/accounts/me" && ! came 127.0.0.1 "POST /v1/accounts/me"'
check "route.txt keeps it for the apps" 'route pc | grep -qx "https=https://$HTTPS_ADDR"'

echo "== Tube on that PC, which takes it from route.txt"
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run pc tube_search.txt xtube pc_tube
check "its search (and token) went over HTTPS" \
      'came $EDGE_IP "POST /v1/tube/search" && ! came 127.0.0.1 "POST /v1/tube/search"'

echo "== the Store on that PC installs DOOM, with 8 MB of game data"
XSIM_KIND=pc XSIM_LIB="$T/lib" SSL_CERT_FILE="$T/ca.pem" run pc store_install.txt xsim pc_store
check "the game data came over HTTPS" \
      'came $EDGE_IP "GET /v1/store/file/freedoom2" && ! came 127.0.0.1 "GET /v1/store/file/freedoom2"'
check "and arrived whole (the Store checks its SHA-256 before saving it)" \
      'cmp -s "$T/store/freedoom1.wad" "$T/pc/uda:/data/freedoom1.wad" && cmp -s "$T/store/freedoom2.wad" "$T/pc/uda:/data/freedoom2.wad"'

echo "== a new profile made on a PC: the passcode"
machine newpc "127.0.0.1:$CLOUD" 0
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run newpc online.txt
check "the profile was made over HTTPS" \
      'came $EDGE_IP "POST /v1/accounts/create" && ! came 127.0.0.1 "POST /v1/accounts/create"'

echo "== a PC that doesn't trust the certificate (no test authority)"
machine untrusted "127.0.0.1:$CLOUD" 1
XSIM_KIND=pc run untrusted https.txt
check "it stayed on plain HTTP" 'came 127.0.0.1 "POST /v1/accounts/me" && ! tunnel_used'
check "it says why" 'grep -q "certificate didn.t check out" "$T/sim-untrusted.log"'
check "nothing to remember" 'no_route untrusted'

echo "== a certificate for another name"
start_proxy wrong.test
machine wrongname "127.0.0.1:$CLOUD" 1
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run wrongname https.txt
check "it stayed on plain HTTP" 'came 127.0.0.1 "POST /v1/accounts/me" && ! tunnel_used'
check "it says why" 'grep -q "hostname mismatch" "$T/sim-wrongname.log"'
check "nothing to remember" 'no_route wrongname'
start_proxy localhost

echo "== an Xbox, with the same server"
machine xbox "127.0.0.1:$CLOUD" 1
SSL_CERT_FILE="$T/ca.pem" run xbox https.txt
check "plain HTTP, and it never tried HTTPS" 'came 127.0.0.1 "POST /v1/accounts/me" && ! tunnel_used'
check "nothing to remember" 'no_route xbox'

echo "== a PC whose config.txt says https:// itself"
machine httpsconfig "https://$HTTPS_ADDR" 1
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run httpsconfig https.txt
check "everything went over HTTPS" 'came $EDGE_IP "POST /v1/accounts/me" && ! since | grep -q "^127.0.0.1 "'
check "nothing else to remember" 'no_route httpsconfig'

echo "== the server stops offering HTTPS (the first PC again)"
start_cloud "" ""
XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run pc https.txt xsim pc_after
check "back on plain HTTP" 'came 127.0.0.1 "POST /v1/accounts/me" && ! came $EDGE_IP "POST /v1/accounts/me"'
check "route.txt is gone" 'no_route pc'

if [ -n "$HOME_IP" ]; then
    echo "== at home: XenithOS Cloud's home address is $HOME_IP:$CLOUD"
    start_cloud "$HTTPS_ADDR" "$HOME_IP:$CLOUD"
    machine home "127.0.0.1:$CLOUD" 1
    XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run home https.txt
    check "the home address comes first" 'came $HOME_IP "POST /v1/accounts/me" && ! came $EDGE_IP "POST /v1/accounts/me"'
    check "route.txt has both" \
          'route home | grep -qx "lan=$HOME_IP:$CLOUD" && route home | grep -qx "https=https://$HTTPS_ADDR"'

    echo "== the home address stops answering while Tube runs"
    sed -i 's/^lan=.*/lan=127.0.0.1:1/' "$T/home/uda:/xenith/route.txt"
    XSIM_KIND=pc SSL_CERT_FILE="$T/ca.pem" run home tube_search.txt xtube home_tube
    check "its search went out over HTTPS" \
          'came $EDGE_IP "POST /v1/tube/search" && ! came 127.0.0.1 "POST /v1/tube/search"'
else
    echo "== at home: skipped (no network address besides 127.0.0.1 to play XenithOS Cloud's home one)"
fi

mkdir -p "$repo/sim/out/https"
for d in "$T"/shots/*/; do
    for f in "$d"*.png; do [ -e "$f" ] && cp "$f" "$repo/sim/out/https/$(basename "$d")_$(basename "$f")"; done
done
echo "screenshots in sim/out/https, logs in $T"
if [ "$failed" = 0 ]; then echo "all passed"; else echo "SOME FAILED"; fi
exit "$failed"
