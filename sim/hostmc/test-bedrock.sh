#!/bin/bash
# HostMC's Bedrock players (Geyser and Floodgate, with Paper), with nothing downloaded: a stand-in
# for GeyserMC's download service (sim/hostmc/geysermc.py, HOSTMC_GEYSER_API) and the stand-in
# Minecraft server acting as Paper (sim/hostmc/java loads the two plugins as Paper would and takes
# Floodgate's fwhitelist).
#
#   sim/hostmc/test-bedrock.sh        after make -C sim xhostmc; needs python3
#
# It checks: HostMC asks the service for the newest builds and downloads exactly those, only once
# the user says yes; both land in the server's plugins folder matching the checksums the service
# gives; the server loads them; a Bedrock player goes on the whitelist through Floodgate; an update
# whose download doesn't match its checksum is thrown away and the ones there stay; turning Bedrock
# players off takes both out again and keeps their settings. Screenshots end up in sim/out/hostmc-bedrock.
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/hostmc-bedrock-test
W="$T/data"                      # XenithOS's data folder, with hostmc/ in it
G="$T/geysermc"                  # the stand-in service's folder (builds.json, requests.log)
API=28797 MCPORT=25579
failed=0
check() { local what=$1; shift; if "$@"; then echo "  ok    $what"; else echo "  FAIL  $what"; failed=1; fi; }

for port in $API $MCPORT; do
    python3 -I -c "import socket as s, sys; k = s.socket(); k.setsockopt(s.SOL_SOCKET, s.SO_REUSEADDR, 1)
k.bind(('127.0.0.1', int(sys.argv[1])))" "$port" 2>/dev/null || { echo "port $port is in use here; the test needs it"; exit 2; }
done
make -s -C "$repo/sim" xhostmc || exit 2

rm -rf "$T"
mkdir -p "$W/uda:/xenith" "$W/uda:/apps" "$W/hostmc" "$G" "$T/shots"
cp -r "$repo/sim/hostmc/data/." "$W/hostmc/"
echo "stand-in" > "$W/hostmc/server/server.jar"
sed -i 's/^type=.*/type=paper/' "$W/hostmc/server/installed.txt"      # Paper, as Version would have downloaded it
printf 'type=paper\nversion=latest\nram=1024\n' > "$W/hostmc/hostmc.txt"
printf 'server-port=%d\nmotd=HostMC with Bedrock players\n' "$MCPORT" > "$W/hostmc/server/server.properties"
printf 'server=\nname=SIM\n' > "$W/uda:/xenith/config.txt"
python3 -I "$repo/sim/hostmc/geysermc.py" "$API" "$G" 2> "$T/geysermc.log" &
GM=$!
sleep 0.5

run() {                          # $1: the sim script, $2: a name for its screenshots and log, $3: typed
    mkdir -p "$T/shots/$2"
    ( cd "$W" && if [ -n "$3" ]; then export XSIM_TYPE="$3"; fi &&
      XSIM_KIND=pc XSIM_THREAD=1 XSIM_SCRIPT="$repo/sim/scripts/$1" XSIM_OUT="$T/shots/$2" \
      HOSTMC_JAVA="$repo/sim/hostmc/java" HOSTMC_GEYSER_API="http://127.0.0.1:$API/v2" ASAN_OPTIONS=detect_leaks=0 \
      "$repo/sim/xhostmc" 2> "$T/sim-$2.log" )
    echo "  ($1: the simulator finished with $?)"
    if grep -qE "runtime error|AddressSanitizer" "$T/sim-$2.log"; then echo "  FAIL  $1: the sanitizers found something"; failed=1; fi
}
sha() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
want_sha() {                     # what the service says the jar's checksum is
    python3 -I -c "import json, urllib.request, sys
print(json.load(urllib.request.urlopen(sys.argv[1]))['downloads']['spigot']['sha256'])" \
        "http://127.0.0.1:$API/v2/projects/$1/versions/latest/builds/latest"
}
pid_in() { grep '^pid=' "$1" 2>/dev/null | cut -d= -f2; }
server_alive() { p=$(pid_in "$W/hostmc/server/hostmc.pid"); [ -n "$p" ] && [ "$(readlink "/proc/$p/cwd" 2>/dev/null)" = "$(realpath "$W/hostmc/server")" ]; }

echo "== Bedrock players on, the server with them, a Bedrock player on the whitelist"
run hostmc_bedrock.txt on "BedrockPal|"
asked_first() { grep -qx "/v2/projects/geyser/versions/latest/builds/latest" "$G/requests.log" &&
                grep -qx "/v2/projects/floodgate/versions/latest/builds/latest" "$G/requests.log"; }
exact_builds() { grep -qx "/v2/projects/geyser/versions/2.12.1/builds/1303/downloads/spigot" "$G/requests.log" &&
                 grep -qx "/v2/projects/floodgate/versions/2.2.5/builds/141/downloads/spigot" "$G/requests.log"; }
checked() { [ "$(sha "$W/hostmc/server/plugins/Geyser-Spigot.jar")" = "$(want_sha geyser)" ] &&
            [ "$(sha "$W/hostmc/server/plugins/floodgate-spigot.jar")" = "$(want_sha floodgate)" ]; }
noted() { grep -qx "geyser=2.12.1, build 1303" "$W/hostmc/geyser.txt" && grep -qx "floodgate=2.2.5, build 141" "$W/hostmc/geyser.txt"; }
loaded() { grep -q "\[Geyser-Spigot\] Loading server plugin" "$W/hostmc/server/console.log" &&
           grep -q "\[floodgate\] Loading server plugin" "$W/hostmc/server/console.log"; }
whitelisted() { grep -q '"name": ".BedrockPal"' "$W/hostmc/server/whitelist.json"; }
check "HostMC asked GeyserMC's service which builds are newest" asked_first
check "and downloaded exactly those builds" exact_builds
check "both are in the server's plugins folder, matching the checksums the service gave" checked
check "HostMC notes which builds they are (hostmc/geyser.txt)" noted
check "Bedrock players are on in HostMC's settings" grep -qx "bedrock=1" "$W/hostmc/hostmc.txt"
check "the server loaded Geyser and Floodgate, as Paper does" loaded
check "Geyser made its settings (where HostMC reads the Bedrock port)" test -f "$W/hostmc/server/plugins/Geyser-Spigot/config.yml"
check "a Bedrock player went on the whitelist through Floodgate (.BedrockPal)" whitelisted
check "the server stopped" eval "! server_alive"

echo "== an update that doesn't match its checksum, then Bedrock players off"
before_g=$(sha "$W/hostmc/server/plugins/Geyser-Spigot.jar")
before_f=$(sha "$W/hostmc/server/plugins/floodgate-spigot.jar")
# Geyser's newer build is good, Floodgate's isn't: neither may go in, and nothing may be left over.
printf '{"geyser": ["2.12.2", 1310], "floodgate": ["2.2.6", 145], "bad": "floodgate"}\n' > "$G/builds.json"
run hostmc_bedrock_update.txt update ""
tried() { grep -qx "/v2/projects/geyser/versions/2.12.2/builds/1310/downloads/spigot" "$G/requests.log" &&
          grep -qx "/v2/projects/floodgate/versions/2.2.6/builds/145/downloads/spigot" "$G/requests.log"; }
thrown() { [ ! -e "$W/hostmc/server/plugins/Geyser-Spigot.jar.new" ] && [ ! -e "$W/hostmc/server/plugins/floodgate-spigot.jar.new" ]; }
stayed() { [ "$(sha "$W/hostmc/server/plugins/Geyser-Spigot.jar")" = "$before_g" ] &&
           [ "$(sha "$W/hostmc/server/plugins/floodgate-spigot.jar")" = "$before_f" ] && noted; }
check "the newer builds were asked for" tried
check "Floodgate's didn't match its checksum: both downloads thrown away" thrown
check "and the builds there stayed as they were (Geyser's good one didn't go in alone)" stayed
run hostmc_bedrock_off.txt off ""
off() { [ ! -e "$W/hostmc/server/plugins/Geyser-Spigot.jar" ] && [ ! -e "$W/hostmc/server/plugins/floodgate-spigot.jar" ] &&
        grep -qx "bedrock=0" "$W/hostmc/hostmc.txt"; }
check "turned off: both out of the plugins folder" off
check "their settings kept for next time" test -f "$W/hostmc/server/plugins/Geyser-Spigot/config.yml"

kill "$GM" 2>/dev/null
wait 2>/dev/null
if server_alive; then echo "  (stopping the server left running)"; kill "$(pid_in "$W/hostmc/server/hostmc.pid")"; failed=1; fi
mkdir -p "$repo/sim/out/hostmc-bedrock"
for d in "$T"/shots/*/; do cp "$d"*.png "$repo/sim/out/hostmc-bedrock/" 2>/dev/null; done
echo "screenshots in sim/out/hostmc-bedrock"
[ "$failed" = 0 ] && rm -rf "$T"
exit $failed
