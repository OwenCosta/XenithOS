#!/bin/bash
# The Store's signatures and its downloads queue, end to end and offline: XenithOS Cloud
# (server/xenith_cloud.py) on a spare port with a Store and a key made for the test, the simulator
# trusting that key (XSIM_STORE_KEY), and sim/store/between.py between the two wherever a test
# needs someone changing things on the way.
#
#   sim/store/test-store.sh        needs python3; builds sim/xsim first (XSIM_PREBUILT=1: uses the one there)
#
# It checks:
#   - a signed Store opens, on the Xbox and on a PC
#   - refused, and nothing downloaded: a list with a file and its SHA-256 swapped on the way (what
#     an attacker would do), one with its signature taken off, one signed with another key, and one
#     from a XenithOS Cloud without a key
#   - with the bundle blocked, each list and thumbnail on its own is checked: signed ones open, a
#     swap is refused
#   - a build without a key still opens an unsigned Store, and there the swap gets through: the
#     hole signing closes. With the key, the same swap installs nothing.
#   - the downloads queue: one after another, B stops the one downloading and the rest carry on,
#     on the Xbox, and on a PC with apps from Flathub
# Screenshots end up in sim/out/store-signing.
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/store-signing-test
SRV=18795 MID=18796
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }

if [ -z "$XSIM_PREBUILT" ]; then make -s -C "$repo/sim" xsim || exit 2; fi
[ -x "$repo/sim/xsim" ] || { echo "no sim/xsim to run"; exit 2; }

rm -rf "$T"
mkdir -p "$T/store" "$T/state" "$T/web" "$T/downloads" "$T/keys" "$T/shots" "$T/lib"

# The Store: server/store.example.json's items with made-up files of their own (Tube's big enough
# to stop midway), their thumbnails, and three apps from Flathub for PCs.
python3 -I - "$repo" "$T/store" <<'EOF' || exit 2
import json, os, shutil, sys
repo, d = sys.argv[1], sys.argv[2]
sizes = {"volt": 200000, "doom": 300000, "tube": 3000000, "photos": 250000, "freedoom1": 600000,
         "freedoom2": 700000, "xenith": 400000, "xenithos-pc": 500000}
items = json.load(open(os.path.join(repo, "server", "store.example.json")))
flathub = [
    {"id": "minecraft", "name": "Minecraft", "ref": "org.prismlauncher.PrismLauncher", "arches": ["x86_64", "aarch64"],
     "mb": 39, "shared_mb": 1350, "description": "Minecraft: Java Edition, through Prism Launcher."},
    {"id": "kodi", "name": "Kodi", "ref": "tv.kodi.Kodi", "arches": ["x86_64", "aarch64"], "mb": 285,
     "shared_mb": 690, "description": "Your films, shows and music."},
    {"id": "retroarch", "name": "RetroArch", "ref": "org.libretro.RetroArch", "arches": ["x86_64", "aarch64"],
     "mb": 160, "shared_mb": 690, "description": "Games from older consoles."}]
for it in items:
    with open(os.path.join(d, it["file"]), "wb") as f:
        f.write(os.urandom(sizes[it["id"]]))
for iid in [it["id"] for it in items] + [a["id"] for a in flathub]:
    thumb = os.path.join(repo, "art", "thumbs", iid + ".ximg")
    if os.path.exists(thumb):
        shutil.copy(thumb, os.path.join(d, iid + ".ximg"))
json.dump(items, open(os.path.join(d, "store.json"), "w"))
json.dump(flathub, open(os.path.join(d, "flathub.json"), "w"))
EOF

# XenithOS's own apps on a PC, as sim/run.sh makes them for XSIM_KIND=pc.
for app in volt doom tube photos minenest; do
    : > "$T/lib/$app"
    [ -f "$repo/apps/$app/app.txt" ] && cp "$repo/apps/$app/app.txt" "$T/lib/$app.txt"
    [ -f "$repo/art/thumbs/$app.ximg" ] && cp "$repo/art/thumbs/$app.ximg" "$T/lib/$app.ximg"
done

# The test's keys: the one its XenithOS Cloud signs with, and another one.
newkey() { python3 -I "$repo/server/xenith_cloud.py" store-key new "$1" | sed -n 's/^Public half: \([0-9a-f]*\).*/\1/p'; }
KEY=$(newkey "$T/keys/store.key")
OTHER=$(newkey "$T/keys/other.key")
{ [ ${#KEY} = 64 ] && [ ${#OTHER} = 64 ]; } || { echo "couldn't make the test's keys"; exit 2; }

SERVER= MIDDLE=
up() {                           # something answers /v1/ping on port $1
    for _ in $(seq 1 50); do
        python3 -I -c "import urllib.request as u; u.urlopen('http://127.0.0.1:$1/v1/ping', timeout=1)" 2>/dev/null && return 0
        sleep 0.1
    done
    echo "  (nothing answered on port $1)"
    return 1
}
stop_server() { if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null; wait "$SERVER" 2>/dev/null; fi; SERVER=; }
stop_middle() { if [ -n "$MIDDLE" ]; then kill "$MIDDLE" 2>/dev/null; wait "$MIDDLE" 2>/dev/null; fi; MIDDLE=; }
start_server() {                 # $1: the key file, or "" for a XenithOS Cloud without one
    stop_server
    XENITH_PORT=$SRV XENITH_STATE_DIR="$T/state" XENITH_STORE_DIR="$T/store" XENITH_WEB_DIR="$T/web" \
        XENITH_DOWNLOAD_DIR="$T/downloads" XENITH_STORE_KEY="$1" \
        python3 -I "$repo/server/xenith_cloud.py" >> "$T/server.log" 2>&1 &
    SERVER=$!
    up $SRV
}
start_middle() {                 # between.py, with its options
    stop_middle
    python3 -I "$repo/sim/store/between.py" $MID "127.0.0.1:$SRV" "$@" >> "$T/between.log" 2>&1 &
    MIDDLE=$!
    up $MID
}

# run SCRIPT NAME xbox|pc direct|middle [VAR=value for the simulator...]
run() {
    local script=$1 name=$2 kind=$3 port=$SRV env app
    [ "$4" = middle ] && port=$MID
    shift 4
    W="$T/run-$name"
    rm -rf "$W"
    mkdir -p "$W/uda:/xenith" "$W/uda:/apps" "$T/shots/$name"
    printf 'server=127.0.0.1:%s\nname=SIM\n' "$port" > "$W/uda:/xenith/config.txt"
    env=(XSIM_SCRIPT="$repo/sim/scripts/$script" XSIM_OUT="$T/shots/$name" ASAN_OPTIONS=detect_leaks=0)
    if [ "$kind" = pc ]; then
        env+=(XSIM_KIND=pc XSIM_LIB="$T/lib")
        for app in volt tube photos minenest; do       # on the home screen already; DOOM isn't
            cp "$T/lib/$app" "$W/uda:/apps/$app.elf32"
            cp "$T/lib/$app.txt" "$W/uda:/apps/$app.txt" 2>/dev/null
            cp "$T/lib/$app.ximg" "$W/uda:/apps/$app.ximg" 2>/dev/null
        done
    fi
    ( cd "$W" && env "${env[@]}" "$@" "$repo/sim/xsim" > "$T/sim-$name.log" 2>&1 )
    echo "  ($script as $name: the simulator finished with $?)"
}
said() { grep -q -- "$2" "$T/sim-$1.log"; }            # run $1's log has $2
files_asked() { grep -c "GET /v1/store/file/" "$T/server.log"; }

echo "== a signed Store opens"
start_server "$T/keys/store.key"
run store_signed.txt signed xbox direct XSIM_STORE_KEY="$KEY"
check "the Xbox's Store checked out" "said signed 'store: all: signed'"
run store_signed.txt signed-pc pc direct XSIM_STORE_KEY="$KEY"
check "the PC's Store checked out" "said signed-pc 'store: all pc: signed'"

echo "== someone in between: refused, and nothing downloaded"
asked=$(files_asked)
start_middle --swap volt
run store_refused.txt swapped xbox middle XSIM_STORE_KEY="$KEY"
check "VOLT and its SHA-256 swapped on the way: refused" "said swapped 'store: all: signature doesn.t match, refused'"
start_middle --strip
run store_refused.txt stripped pc middle XSIM_STORE_KEY="$KEY"
check "the signature taken off: refused" "said stripped 'store: all pc: not signed, refused'"
stop_middle
run store_refused.txt other-key xbox direct XSIM_STORE_KEY="$OTHER"
check "signed with another key: refused" "said other-key 'store: all: signature doesn.t match, refused'"
start_server ""
run store_refused.txt no-server-key pc direct XSIM_STORE_KEY="$KEY"
check "a XenithOS Cloud without a key: refused" "said no-server-key 'store: all pc: not signed, refused'"
check "none of them downloaded anything" "[ \"\$(files_asked)\" = '$asked' ]"

echo "== the bundle blocked: each list and thumbnail on its own"
start_server "$T/keys/store.key"
start_middle --no-bundle
run store_signed.txt lists pc middle XSIM_STORE_KEY="$KEY"
check "each list checked out" "said lists 'store: store: signed' && said lists 'store: needs: signed' && said lists 'store: flathub: signed' && said lists 'store: steam: signed'"
check "and the thumbnails" "said lists 'store: icon doom.ximg: signed' && said lists 'store: icon kodi.ximg: signed' && ! said lists refused"
start_middle --no-bundle --swap volt
run store_refused.txt lists-swapped xbox middle XSIM_STORE_KEY="$KEY"
check "a swap there is refused too" "said lists-swapped 'store: store: signature doesn.t match, refused'"

echo "== why: a build without a key, and the same swap"
start_middle --swap volt
run store_install_first.txt no-key-swap xbox middle XSIM_STORE_KEY=none
check "without a key, the swapped VOLT is installed: the hole signing closes" \
      "grep -q 'Not what XenithOS Cloud sent' \"$T/run-no-key-swap/uda:/apps/volt.elf32\""
run store_install_first.txt key-swap xbox middle XSIM_STORE_KEY="$KEY"
check "with the key, the same swap installs nothing" "said key-swap refused && [ ! -e \"$T/run-key-swap/uda:/apps/volt.elf32\" ]"
stop_middle
run store_install_first.txt key-direct xbox direct XSIM_STORE_KEY="$KEY"
check "and straight from XenithOS Cloud, VOLT installs as it is there" \
      "cmp -s \"$T/run-key-direct/uda:/apps/volt.elf32\" \"$T/store/volt.elf32\""

echo "== the downloads queue on the Xbox: Tube stopped midway, the rest carry on"
start_middle --slow tube
before=$(files_asked)
run store_queue.txt queue xbox middle XSIM_STORE_KEY="$KEY" XSIM_NET_TIME=1
U="$T/run-queue/uda:"
order=$(grep -o 'GET /v1/store/file/[a-z0-9-]*' "$T/server.log" | tail -n +"$((before + 1))" | sed 's,.*/,,' | paste -sd' ')
check "one after another, in the order picked: $order" "[ '$order' = 'doom freedoom1 freedoom2 tube photos' ]"
check "DOOM and its Freedoom installed, as XenithOS Cloud has them" \
      "cmp -s '$U/apps/doom.elf32' '$T/store/doom.elf32' && cmp -s '$U/data/freedoom1.wad' '$T/store/freedoom1.wad' && cmp -s '$U/data/freedoom2.wad' '$T/store/freedoom2.wad'"
check "Tube stopped midway: not installed, nothing half written" \
      "[ ! -e '$U/apps/tube.elf32' ] && [ ! -e '$U/apps/tube.elf32.part' ] && grep -q 'tube: the console hung up' '$T/between.log'"
check "Photos installed after the stop" "cmp -s '$U/apps/photos.elf32' '$T/store/photos.elf32'"
check "VOLT, not picked, not downloaded" "[ ! -e '$U/apps/volt.elf32' ]"
stop_middle

echo "== the downloads queue on a PC: Kodi stopped while Flatpak works, the rest carry on"
run store_queue_pc.txt queue-pc pc direct XSIM_STORE_KEY="$KEY" XSIM_NET_TIME=1
U="$T/run-queue-pc/uda:"
check "DOOM's Freedoom downloaded, and DOOM put on the home screen" \
      "cmp -s '$U/data/freedoom1.wad' '$T/store/freedoom1.wad' && cmp -s '$U/data/freedoom2.wad' '$T/store/freedoom2.wad' && said queue-pc 'sim: on the home screen: doom'"
check "Kodi stopped: Flatpak started on it, but it isn't on the home screen" \
      "said queue-pc 'sim: flatpak install tv.kodi.Kodi' && [ ! -e '$U/apps/kodi.app' ]"
check "RetroArch installed after it" \
      "said queue-pc 'sim: flatpak install org.libretro.RetroArch' && grep -q 'flatpak=org.libretro.RetroArch' '$U/apps/retroarch.app'"

stop_middle
stop_server
check "no memory or undefined-behaviour errors in any run" "! grep -l -e AddressSanitizer -e 'runtime error' \"$T\"/sim-*.log"
mkdir -p "$repo/sim/out/store-signing"
for d in "$T"/shots/*/; do
    for f in "$d"*.png; do [ -e "$f" ] && cp "$f" "$repo/sim/out/store-signing/$(basename "$d")-$(basename "$f")"; done
done
echo "screenshots in sim/out/store-signing"
[ "$failed" = 0 ] && rm -rf "$T"
exit $failed
