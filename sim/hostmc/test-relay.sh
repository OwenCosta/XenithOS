#!/bin/bash
# HostMC's supporter relay, end to end, with nothing on the internet. Running here: XenithOS
# Cloud (server/xenith_cloud.py) with the relay set up, a stand-in for OCAccounts
# (sim/hostmc/ocaccounts.py), the relay's own software (server/mcrelay/mcrelay.py: the router on
# port 25565 and the sync), the relay's sshd for tunnels with the settings mcrelay.py writes (run
# as you on a spare port, never installed), and HostMC in the simulator with the stand-in
# Minecraft server. HostMC runs in a user namespace with a hosts file of its own, where
# *.mc.test leads here, so its check from outside asks by the name as a friend's game would
# (HostMC itself doesn't know it's in one; nothing in it is there for the test).
#
#   sim/hostmc/test-relay.sh        after make -C sim xhostmc; needs openssh-server, python3 and unshare
#
# It checks: a profile that isn't a supporter's gets the steps and no name; a supporter's gets the
# name it types; the relay gets HostMC's key, good for that supporter's own port and nothing else
# (no other port, no shell, no way to anything on the relay); the tunnel comes up with the
# relay's host key pinned; players reach the server by its name, and another supporter's name
# reaches theirs; a name nobody has gets the relay's own answer; a membership that ends stops the
# relay passing players (its port goes to no one else meanwhile) and coming back starts it again;
# players' addresses: Minecraft's own server keeps seeing 127.0.0.1, and with Paper the relay sends
# each player's own in a PROXY header while a doorway on the server's port does the same for
# players who come straight to it (the stand-in server acts as Paper does with proxy-protocol on);
# a new HostMC picks the tunnel up and closes it, and the doorway, with the server. Players
# "elsewhere" connect from other loopback addresses (127.0.0.7, 127.0.0.9), so the address the
# server sees tells them apart. Screenshots end up in sim/out/hostmc-relay.
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/hostmc-relay-test
W="$T/data"                      # XenithOS's data folder on the supporter's PC, with hostmc/ in it
W2="$T/data2"                    # another PC's, signed in to a profile that isn't a supporter's
C="$T/cloud"                     # XenithOS Cloud's folders
R="$T/relay"                     # the relay's (routes, authorized_keys, secret)
S="$T/sshd"                      # the relay's sshd for tunnels
CLOUD=28795 OC=28796 SSHPORT=22122 MCPORT=25577 MC2PORT=25578
PLAYERS=25566                    # the relay's port for players: not Minecraft's own, as on a VPS that has a server
ME=$(id -un)
SSHD=$(command -v sshd || echo /usr/sbin/sshd)
failed=0
pids=""
check() { local what=$1; shift; if "$@"; then echo "  ok    $what"; else echo "  FAIL  $what"; failed=1; fi; }

[ -x "$SSHD" ] || { echo "needs sshd (openssh-server): it's run as you, nothing is set up system-wide"; exit 2; }
unshare -rm true 2>/dev/null && command -v nsenter > /dev/null ||
    { echo "needs user namespaces (unshare -rm, nsenter), for HostMC's own hosts file"; exit 2; }
for port in $PLAYERS $CLOUD $OC $SSHPORT $MCPORT $MC2PORT; do       # (connections just closed don't count)
    python3 -I -c "import socket as s, sys; k = s.socket(); k.setsockopt(s.SOL_SOCKET, s.SO_REUSEADDR, 1)
k.bind(('0.0.0.0', int(sys.argv[1])))" "$port" 2>/dev/null || { echo "port $port is in use here; the test needs it"; exit 2; }
done
make -s -C "$repo/sim" xhostmc || exit 2

rm -rf "$T"
mkdir -p "$W/uda:/xenith" "$W/uda:/apps" "$W/hostmc" "$W2/uda:/xenith" "$W2/uda:/apps" "$W2/hostmc" \
         "$C/store" "$C/web" "$R" "$S" "$T/shots" "$T/mc2"
printf '127.0.0.1 localhost\n127.0.0.1 alpha.mc.test beta.mc.test nobody.mc.test\n' > "$T/hosts"
SECRET=$(python3 -I -c 'import secrets; print(secrets.token_hex(32))')
printf '%s\n' "$SECRET" > "$R/secret"
chmod 600 "$R/secret"

members() {                      # $1: whether Sup1 counts as a supporter (true or false)
    printf '{"sup1": {"linked": true, "patreon": true, "supporter": %s, "tier": "Supporter"},\n' "$1" > "$T/members.new"
    printf ' "sup2": {"linked": true, "patreon": true, "supporter": true, "tier": "Big supporter"}}\n' >> "$T/members.new"
    mv "$T/members.new" "$T/members.json"
}
# A form post to XenithOS Cloud: post PATH key=value...; admin PATH [BODY] with the admin key.
post() {
    python3 -I - "$CLOUD" "$@" <<'EOF'
import sys, urllib.error, urllib.parse, urllib.request
port, path, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
body = urllib.parse.urlencode([tuple(p.split("=", 1)) for p in pairs]).encode()
try:
    print(urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", body, timeout=15).read().decode(), end="")
except urllib.error.HTTPError as e:
    print(e.read().decode(), end="")
    sys.exit(1)
EOF
}
admin() {
    python3 -I - "$CLOUD" "$@" <<'EOF'
import sys, urllib.request
port, path = sys.argv[1], sys.argv[2]
data = sys.argv[3].encode() if len(sys.argv) > 3 else None
req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data, headers={"X-Xenith-Key": "k3y"})
print(urllib.request.urlopen(req, timeout=15).read().decode(), end="")
EOF
}
play() { python3 -I "$repo/sim/hostmc/ping.py" 127.0.0.1 "$PLAYERS" "$1.mc.test" 2>/dev/null; }   # a friend's game, by name
says() { play "$1" | grep -q "$2"; }                   # what the relay or the server answers for a name
# A friend's game from elsewhere ($2), by name through the relay, or ($1 "") at the server's own port.
play_from() {
    if [ -n "$1" ]; then python3 -I "$repo/sim/hostmc/ping.py" 127.0.0.1 "$PLAYERS" "$1.mc.test" "$2" 2>/dev/null
    else python3 -I "$repo/sim/hostmc/ping.py" 127.0.0.1 "$MCPORT" "" "$2" 2>/dev/null; fi
}
seen() { tail -n 1 "$W/hostmc/server/connections.log" 2>/dev/null | grep -qx "$1"; }   # who the server last saw
run() {                          # $1: the sim script, $2: a name for its screenshots and log, $3: the data folder, $4: typed
    mkdir -p "$T/shots/$2"
    ( cd "$3" && if [ -n "$4" ]; then export XSIM_TYPE="$4"; fi &&
      XSIM_KIND=pc XSIM_THREAD=1 XSIM_SCRIPT="$repo/sim/scripts/$1" XSIM_OUT="$T/shots/$2" \
      HOSTMC_JAVA="$repo/sim/hostmc/java" ASAN_OPTIONS=detect_leaks=0 \
      nsenter -t "$NS" -U -m --preserve-credentials --wd="$3" "$repo/sim/xhostmc" 2> "$T/sim-$2.log" )
    echo "  ($1: the simulator finished with $?)"
    if grep -qE "runtime error|AddressSanitizer" "$T/sim-$2.log"; then echo "  FAIL  $1: the sanitizers found something"; failed=1; fi
}
pid_in() { grep '^pid=' "$1" 2>/dev/null | cut -d= -f2; }
ours() { [ -n "$1" ] && [ "$(readlink "/proc/$1/cwd" 2>/dev/null)" = "$(realpath "$2")" ]; }
keeper() { pid_in "$W/hostmc/relay/tunnel.pid"; }
keeper_alive() { ours "$(keeper)" "$W/hostmc/relay"; }
server_alive() { ours "$(pid_in "$W/hostmc/server/hostmc.pid")" "$W/hostmc/server"; }
wait_for() {                     # $1: seconds, then a command that has to come true by then
    local n=$1; shift
    for _ in $(seq 1 "$((n * 5))"); do "$@" && return 0; sleep 0.2; done
    return 1
}
started() { pids="$pids $1"; }
as_hostmc() {                    # ssh to the relay with HostMC's key, as anyone holding it could
    timeout 15 ssh -F /dev/null -i "$W/hostmc/relay/key" -o IdentitiesOnly=yes -o IdentityAgent=none -o BatchMode=yes \
        -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$W/hostmc/relay/known_hosts" -o ConnectTimeout=5 \
        -o ExitOnForwardFailure=yes -p "$SSHPORT" -l "$ME" "$@" 127.0.0.1 2> "$T/as_hostmc.log"
}

echo "== XenithOS Cloud, OCAccounts and the relay, here"
# HostMC's namespace, with its own hosts file. Every run joins this one, so a HostMC sees the
# server and tunnel an earlier one left running, as it would on a PC.
unshare -rm sh -c 'mount --bind "$0" /etc/hosts && exec sleep 100000' "$T/hosts" &
NS=$!
started $NS
members true
python3 -I "$repo/sim/hostmc/ocaccounts.py" "$T/members.json" "$OC" partn3r 2> "$T/ocaccounts.log" &
started $!
( cd "$C" && exec env XENITH_PORT=$CLOUD XENITH_STATE_DIR="$C/state" XENITH_STORE_DIR="$C/store" XENITH_WEB_DIR="$C/web" \
    XENITH_DOWNLOAD_DIR="$C" XENITH_ADMIN_KEY=k3y XENITH_RELAY_DOMAIN=mc.test XENITH_RELAY_SSH=127.0.0.1:$SSHPORT \
    XENITH_RELAY_SECRET="$SECRET" XENITH_RELAY_USER="$ME" XENITH_RELAY_PORTS=30000-30009 XENITH_RELAY_PLAYERS_PORT=$PLAYERS \
    XENITH_OCACCOUNT_URL=http://127.0.0.1:$OC XENITH_OCACCOUNT_KEY=partn3r \
    XENITH_RELAY_PATREON=https://www.patreon.com/example python3 -I "$repo/server/xenith_cloud.py" ) > "$T/cloud.log" 2>&1 &
started $!
ssh-keygen -q -t ed25519 -N "" -C mcrelay -f "$S/hostkey"
# The relay's own settings, except StrictModes: this test's folders are under /tmp, which it refuses.
python3 -I "$repo/server/mcrelay/mcrelay.py" sshd-config "$SSHPORT" "$ME" "$R/authorized_keys" "$S/hostkey" "$S/sshd.pid" |
    sed 's/^StrictModes yes$/StrictModes no/' > "$S/sshd_config"
echo "ListenAddress 127.0.0.1" >> "$S/sshd_config"
: > "$R/authorized_keys"
chmod 600 "$R/authorized_keys"
"$SSHD" -D -e -f "$S/sshd_config" 2>> "$S/sshd.log" &
started $!
MCRELAY_LISTEN=0.0.0.0:$PLAYERS MCRELAY_DIR="$R" python3 -I "$repo/server/mcrelay/mcrelay.py" router > "$T/router.log" 2>&1 &
started $!
MCRELAY_CLOUD=http://127.0.0.1:$CLOUD MCRELAY_DIR="$R" MCRELAY_SECRET_FILE="$R/secret" MCRELAY_HOSTKEY="$S/hostkey.pub" \
    MCRELAY_EVERY=2 python3 -I "$repo/server/mcrelay/mcrelay.py" sync > "$T/sync.log" 2>&1 &
started $!
check "the relay hears from XenithOS Cloud (signed both ways)" wait_for 20 grep -q "0 names on, 0 held" "$T/sync.log"
TOK1=$(post /v1/accounts/create name=Sup1 pin=1234 avatar=1202001000 | sed -n 's/^token=//p')
TOK2=$(post /v1/accounts/create name=Sup2 pin=1234 avatar=1202001000 | sed -n 's/^token=//p')
TOKN=$(post /v1/accounts/create name=Nolink pin=1234 avatar=1202001000 | sed -n 's/^token=//p')
relay_open() { [ -n "$TOK1" ] && [ -n "$TOK2" ] && [ -n "$TOKN" ] && post /v1/relay/status "token=$TOK1" | grep -qx "relay=on"; }
check "XenithOS Cloud has the relay open, with the relay's host key" relay_open
for d in "$W" "$W2"; do
    cp -r "$repo/sim/hostmc/data/." "$d/hostmc/"
    echo "stand-in" > "$d/hostmc/server/server.jar"
    printf 'server=127.0.0.1:%d\nname=SIM\n' "$CLOUD" > "$d/uda:/xenith/config.txt"
done
printf 'server-port=%d\nmotd=HostMC through the relay\nwhite-list=false\n' "$MCPORT" > "$W/hostmc/server/server.properties"
printf 'name=Sup1\ntoken=%s\navatar=1202001000\njoined=2026-11-16\n' "$TOK1" > "$W/uda:/xenith/profile.txt"
printf 'name=Nolink\ntoken=%s\navatar=1202001000\njoined=2026-11-16\n' "$TOKN" > "$W2/uda:/xenith/profile.txt"

echo "== a profile that isn't a supporter's"
run hostmc_relay_steps.txt steps "$W2" ""
no_name_off() { ! grep -q '^name=' "$W2/hostmc/relay/relay.txt" && ! grep -qx 'relay=1' "$W2/hostmc/hostmc.txt" 2>/dev/null; }
check "it was told what's missing (no OCAccount has it linked)" grep -qx "step=link" "$W2/hostmc/relay/relay.txt"
check "no name, and the relay stays off" no_name_off

echo "== the supporter: a name, the server, the tunnel to the relay"
# A friend's game outside, by the name, every second until it reaches the server.
( for _ in $(seq 1 120); do play alpha > "$T/ping-alpha.json" && grep -q "through the relay" "$T/ping-alpha.json" && break
      rm -f "$T/ping-alpha.json"; sleep 1; done ) &
PINGER=$!
run hostmc_relay.txt first "$W" "alpha|"
wait "$PINGER" 2>/dev/null
named() { grep -qx 'name=alpha' "$W/hostmc/relay/relay.txt" && grep -qx 'state=on' "$W/hostmc/relay/relay.txt" &&
          grep -qx 'port=30000' "$W/hostmc/relay/relay.txt" && grep -qx "address=alpha.mc.test:$PLAYERS" "$W/hostmc/relay/relay.txt"; }
private() { [ "$(stat -c %a "$W/hostmc/relay")" = 700 ] && [ "$(stat -c %a "$W/hostmc/relay/key")" = 600 ]; }
key_line() { grep -qxF "restrict,port-forwarding,permitlisten=\"127.0.0.1:30000\" $(cut -d' ' -f1,2 "$W/hostmc/relay/key.pub") alpha" "$R/authorized_keys"; }
pinned() { grep -qxF "[127.0.0.1]:$SSHPORT $(cut -d' ' -f1,2 "$S/hostkey.pub")" "$W/hostmc/relay/known_hosts"; }
checked_ok() { head -1 "$W/hostmc/relay/check.txt" 2>/dev/null | grep -qx ok; }
both_running() { server_alive && keeper_alive; }
check "XenithOS Cloud gave it the name typed, alpha.mc.test:$PLAYERS, with port 30000 on the relay" named
check "HostMC's key is its own and private (folder 700, key 600)" private
check "the relay has the key, for port 30000 on its loopback only" key_line
check "the relay's host key is pinned, as XenithOS Cloud gave it" pinned
check "the tunnel came up" grep -q "Authenticated to" "$W/hostmc/relay/tunnel.log"
check "a friend's game reached the server by its name, alpha.mc.test" grep -q "HostMC through the relay" "$T/ping-alpha.json"
check "HostMC's own check from outside, by the name, said so" checked_ok
check "a name nobody has gets the relay's own answer" says nobody "no server called nobody"
check "the server and the tunnel kept running after HostMC quit" both_running
vanilla_local() { play_from alpha 127.0.0.7 > /dev/null && seen "status 127.0.0.1"; }
no_proxy() { grep -qx "proxy=off" "$W/hostmc/relay/relay.txt" && grep -qx "proxy_able=1" "$W/hostmc/relay/relay.txt" &&
             ! grep -q "^proxy|" "$R/routes"; }
check "the relay told XenithOS Cloud it can send players' addresses, and this server (Minecraft's own) doesn't want them" no_proxy
check "so through the relay a player from elsewhere shows up as 127.0.0.1" vanilla_local

echo "== what HostMC's key can't do on the relay"
other_port() { ! as_hostmc -N -R "127.0.0.1:30001:127.0.0.1:$MCPORT" && grep -q "forwarding failed" "$T/as_hostmc.log"; }
command_run() { ! as_hostmc true; }
check "open another supporter's port (30001)" other_port
check "run a command or a shell" command_run
as_hostmc -N -L "127.0.0.1:28899:127.0.0.1:$CLOUD" &
LPID=$!
sleep 2
through_it() { ! python3 -I -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:28899/v1/ping', timeout=5).read()" 2>/dev/null; }
check "reach anything on the relay (XenithOS Cloud, here)" through_it
kill "$LPID" 2>/dev/null
wait "$LPID" 2>/dev/null

echo "== a second supporter, with a tunnel of their own to another server"
ssh-keygen -q -t ed25519 -N "" -C sup2 -f "$T/sup2key"
( cd "$T/mc2" && printf 'server-port=%d\nmotd=The second server\n' "$MC2PORT" > server.properties && echo "eula=true" > eula.txt &&
  HOSTMC_FAKE_PLAYERS="" exec python3 -I "$repo/sim/hostmc/java" ) < /dev/null > "$T/mc2.log" 2>&1 &
started $!
post /v1/relay/claim "token=$TOK2" name=beta "key=$(cat "$T/sup2key.pub")" > "$T/claim-beta.txt"
check "the second supporter got beta, with a port of its own" grep -qx "port=30001" "$T/claim-beta.txt"
check "the relay has their key, for their port" wait_for 10 grep -q "permitlisten=\"127.0.0.1:30001\" .* beta$" "$R/authorized_keys"
ssh -F /dev/null -i "$T/sup2key" -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o ExitOnForwardFailure=yes -p "$SSHPORT" -l "$ME" -N -R "127.0.0.1:30001:127.0.0.1:$MC2PORT" 127.0.0.1 2> "$T/sup2-ssh.log" &
SUP2=$!
check "beta.mc.test reaches the second server" wait_for 15 says beta "The second server"
check "and alpha.mc.test still reaches the first" says alpha "through the relay"

echo "== the first supporter's membership ends, then comes back"
members false
admin /v1/relay/check alpha > "$T/check-lapsed.txt"
key_gone() { ! grep -q " alpha$" "$R/authorized_keys"; }
port_kept() { admin /v1/relay/list | grep -q "^alpha|sup1|lapsed|30000|.*|up$"; }
check "XenithOS Cloud lapses the name" grep -qx "state=lapsed" "$T/check-lapsed.txt"
check "the relay stops passing players to it" wait_for 10 says alpha "alpha isn't available"
check "and drops its key" key_gone
check "its port, with the tunnel still on it, is kept from anyone else" port_kept
check "beta is as it was" says beta "The second server"
members true
admin /v1/relay/check alpha > "$T/check-back.txt"
check "back: the name is on again" grep -qx "state=on" "$T/check-back.txt"
check "and players reach the server again" wait_for 10 says alpha "through the relay"
kill "$SUP2" 2>/dev/null
wait "$SUP2" 2>/dev/null

echo "== Paper: players' real addresses through the relay, and the doorway for everyone else"
# The server becomes Paper (what HostMC's Version page would have downloaded), while HostMC is
# closed; HostMC restarts it.
sed -i 's/^type=.*/type=paper/' "$W/hostmc/hostmc.txt" "$W/hostmc/server/installed.txt"
rm -f "$W/hostmc/relay/check.txt"                # what the check from outside finds this time
run hostmc_relay_paper.txt paper "$W" ""
door_pid() { pid_in "$W/hostmc/door/door.pid"; }
door_alive() { ours "$(door_pid)" "$W/hostmc/door"; }
paper_port() { sed -n 's/^paper=//p' "$W/hostmc/door/door.txt" 2>/dev/null; }
asked_on() { grep -qx "proxy=on" "$W/hostmc/relay/relay.txt"; }
paper_set() { grep -qx "  proxy-protocol: true" "$W/hostmc/server/config/paper-global.yml" &&
              [ -n "$(paper_port)" ] && grep -qx "server-port=$(paper_port)" "$W/hostmc/server/server.properties" &&
              grep -qx "server-ip=127.0.0.1" "$W/hostmc/server/server.properties" && grep -qx "port=$MCPORT" "$W/hostmc/door/door.txt"; }
relay_real() { play_from alpha 127.0.0.7 | grep -q "through the relay" && seen "status 127.0.0.7"; }
door_real() { play_from "" 127.0.0.9 | grep -q "through the relay" && seen "status 127.0.0.9"; }
no_header() { python3 -I "$repo/sim/hostmc/ping.py" 127.0.0.1 "$(paper_port)" "" 127.0.0.5 > /dev/null 2>&1; seen "no PROXY header from 127.0.0.5"; }
check "HostMC asked XenithOS Cloud for players' addresses, and it said yes" asked_on
check "the relay lists the server for them" wait_for 10 grep -qx "proxy|alpha" "$R/routes"
check "Paper's proxy-protocol is on, behind the doorway, on a port of its own on the loopback" paper_set
check "the doorway has the server's port ($MCPORT)" door_alive
check "a friend from elsewhere reaches the server through the relay with their own address (127.0.0.7)" wait_for 15 relay_real
check "a friend at the server's own port comes through the doorway with theirs (127.0.0.9)" door_real
check "Paper itself turns away a connection without the header, as the real one does" no_header
check "HostMC's own check from outside, by the name, passed" checked_ok
check "the server, the tunnel and the doorway kept running after HostMC quit" eval "both_running && door_alive"

echo "== a new HostMC picks the tunnel up, changes the name, gives it up, and stops the server"
keeper_pid=$(keeper)
doorway_pid=$(door_pid)
# A friend's game trying the new name, until it reaches the server.
( for _ in $(seq 1 90); do says gamma "through the relay" && touch "$T/gamma-reached" && break; sleep 1; done ) &
PINGER=$!
run hostmc_relay_back.txt back "$W" "gamma|"
wait "$PINGER" 2>/dev/null
both_held() { admin /v1/relay/list > "$T/list-end.txt" && grep -q "^gamma|sup1|left|30000|" "$T/list-end.txt" &&
              grep -q "^alpha|sup1|left|30000|" "$T/list-end.txt"; }
relay_off() { grep -qx "relay=0" "$W/hostmc/hostmc.txt" && ! grep -q "^name=" "$W/hostmc/relay/relay.txt" &&
              grep -qx "held=alpha,gamma" "$W/hostmc/relay/relay.txt"; }
server_gone() { ! server_alive; }
tunnel_gone() { ! ours "$keeper_pid" "$W/hostmc/relay" && [ ! -e "$W/hostmc/relay/tunnel.pid" ]; }
check "the new name, gamma, reached the server through the same tunnel" test -e "$T/gamma-reached"
check "both names are given up, and kept for Sup1 for a week" both_held
check "HostMC turned the relay off and says which names are kept" relay_off
check "the tunnel closed" tunnel_gone
check "the server stopped" server_gone
door_gone() { ! ours "$doorway_pid" "$W/hostmc/door" && [ ! -e "$W/hostmc/door/door.pid" ]; }
check "the doorway closed with it" door_gone
check "a friend's game now hears the name isn't available" wait_for 10 says gamma "gamma isn't available"

# Everything this test started (its own children), and anything HostMC left running (a run that
# went wrong), by checked PID.
for p in $pids; do kill "$p" 2>/dev/null; done
for p in "$(keeper)" "$(pid_in "$W/hostmc/server/hostmc.pid")" "$(pid_in "$W/hostmc/door/door.pid")"; do
    if ours "$p" "$W/hostmc/relay" || ours "$p" "$W/hostmc/server" || ours "$p" "$W/hostmc/door"; then
        echo "  (stopping $p, left running)"; kill "$p"; failed=1
    fi
done
wait 2>/dev/null
mkdir -p "$repo/sim/out/hostmc-relay"
for d in "$T"/shots/*/; do cp "$d"*.png "$repo/sim/out/hostmc-relay/" 2>/dev/null; done
echo "screenshots in sim/out/hostmc-relay"
[ "$failed" = 0 ] && rm -rf "$T"
exit $failed
