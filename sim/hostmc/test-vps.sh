#!/bin/bash
# HostMC's own-VPS route, end to end, with nothing on the internet: a private sshd (run as you,
# on spare ports, with its own keys and settings, never installed as a service) stands in for the
# VPS, and the simulator's stand-in server answers pings. This machine's own address plays the
# VPS's public address, so a ping to it goes the way a friend's game would.
#
#   sim/hostmc/test-vps.sh            after make -C sim xhostmc; needs openssh-server and python3
#
# It checks: the key HostMC makes (and that it's private), the login test, the tunnel coming up
# while the server runs, a player's ping reaching the server through the VPS's public port, the
# tunnel outliving HostMC and coming back by itself when its ssh is killed, a new HostMC picking
# it up and closing it with the server, and the plain message when the VPS's sshd keeps forwards
# to itself (GatewayPorts left off). Screenshots end up in sim/out/hostmc-vps.
repo="$(cd "$(dirname "$0")/../.." && pwd)"
T=/tmp/hostmc-vps-test
W="$T/data"                      # XenithOS's data folder, with HostMC's hostmc/ in it
V="$T/vps"                       # the stand-in VPS's sshd
SSHPORT=22022 PUBLIC=25578 MCPORT=25577
SSHD=$(command -v sshd || echo /usr/sbin/sshd)
HOST=$(hostname -I 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[0-9]+(\.[0-9]+){3}$/) { print $i; exit }}')
failed=0
check() { if eval "$2"; then echo "  ok    $1"; else echo "  FAIL  $1"; failed=1; fi; }

[ -x "$SSHD" ] || { echo "needs sshd (openssh-server): it's run as you, nothing is set up system-wide"; exit 2; }
[ -n "$HOST" ] || { echo "needs a network address other than 127.0.0.1 to stand in for the VPS's"; exit 2; }
make -s -C "$repo/sim" xhostmc || exit 2

rm -rf "$T"
mkdir -p "$W/uda:/xenith" "$W/uda:/apps" "$W/hostmc/vps" "$V" "$T/shots"
printf 'server=\nname=SIM\n' > "$W/uda:/xenith/config.txt"
cp -r "$repo/sim/hostmc/data/." "$W/hostmc/"
echo "stand-in" > "$W/hostmc/server/server.jar"
printf 'server-port=%d\nmotd=HostMC through a VPS\nwhite-list=false\n' "$MCPORT" > "$W/hostmc/server/server.properties"
# The answers to HostMC's four setup questions, as if typed before: the script takes each one.
printf 'host=%s\nuser=%s\nssh_port=%d\npublic_port=%d\n' "$HOST" "$(id -un)" "$SSHPORT" "$PUBLIC" > "$W/hostmc/vps/vps.txt"
chmod 700 "$W/hostmc/vps"

start_vps() {                    # $1: the GatewayPorts setting
    [ -f "$V/hostkey" ] || ssh-keygen -q -t ed25519 -N "" -f "$V/hostkey"
    touch "$V/authorized_keys"
    cat > "$V/sshd_config" <<EOF
Port $SSHPORT
ListenAddress 0.0.0.0
HostKey $V/hostkey
PidFile $V/sshd.pid
AuthorizedKeysFile $V/authorized_keys
UsePAM no
StrictModes no
PasswordAuthentication no
KbdInteractiveAuthentication no
AllowTcpForwarding yes
GatewayPorts $1
LogLevel VERBOSE
EOF
    "$SSHD" -D -e -f "$V/sshd_config" 2>> "$V/sshd.log" &
    SSHD_PID=$!
    for _ in $(seq 1 50); do                 # until it takes connections
        python3 -I -c "import socket, sys; socket.create_connection(('127.0.0.1', int(sys.argv[1])), 1).close()" "$SSHPORT" 2>/dev/null && break
        sleep 0.2
    done
}
stop_vps() {                     # the sshd started above, by its PID, if it's still that sshd
    if [ -n "$SSHD_PID" ] && tr '\0' ' ' < "/proc/$SSHD_PID/cmdline" 2>/dev/null | grep -q "$V/sshd_config"; then
        kill "$SSHD_PID"; wait "$SSHD_PID" 2>/dev/null
    fi
    SSHD_PID=
}
run() {                          # $1: the sim script, $2: a name for its screenshots and log
    mkdir -p "$T/shots/$2"
    ( cd "$W" && XSIM_KIND=pc XSIM_THREAD=1 XSIM_SCRIPT="$repo/sim/scripts/$1" XSIM_OUT="$T/shots/$2" \
        HOSTMC_JAVA="$repo/sim/hostmc/java" ASAN_OPTIONS=detect_leaks=0 "$repo/sim/xhostmc" 2> "$T/sim-$2.log" )
    echo "  ($1: the simulator finished with $?)"
}
pid_in() { grep '^pid=' "$1" 2>/dev/null | cut -d= -f2; }
ours() { [ -n "$1" ] && [ "$(readlink "/proc/$1/cwd" 2>/dev/null)" = "$(realpath "$2")" ]; }
keeper() { pid_in "$W/hostmc/vps/tunnel.pid"; }
keeper_alive() { ours "$(keeper)" "$W/hostmc/vps"; }
server_alive() { ours "$(pid_in "$W/hostmc/server/hostmc.pid")" "$W/hostmc/server"; }
ssh_child() { k=$(keeper); [ -n "$k" ] && pgrep -P "$k" -x ssh; }
ping_through() { python3 -I "$repo/sim/hostmc/ping.py" "$HOST" "$PUBLIC" 2>/dev/null; }

echo "== first run: setting the VPS up, and the server with the tunnel (VPS at $HOST, SSH port $SSHPORT)"
start_vps clientspecified
# The player adds HostMC's key to the VPS once HostMC shows it.
( for i in $(seq 1 300); do [ -s "$W/hostmc/vps/key.pub" ] && break; sleep 0.2; done
  cat "$W/hostmc/vps/key.pub" >> "$V/authorized_keys" ) &
# A friend's game outside, trying the VPS's public port every second.
( for i in $(seq 1 90); do ping_through > "$T/ping.json" && break; rm -f "$T/ping.json"; sleep 1; done ) &
PINGER=$!
run hostmc_vps.txt first
wait "$PINGER" 2>/dev/null
check "HostMC made its own key" "[ -s '$W/hostmc/vps/key' ] && grep -q '^ssh-ed25519 .* hostmc\$' '$W/hostmc/vps/key.pub'"
check "only you can read the key (folder 700, key 600)" \
      "[ \"\$(stat -c %a '$W/hostmc/vps')\" = 700 ] && [ \"\$(stat -c %a '$W/hostmc/vps/key')\" = 600 ]"
check "the VPS let HostMC in with it" "grep -q 'Authenticated to' '$W/hostmc/vps/tunnel.log'"
check "a player's ping reached the server through the VPS's port $PUBLIC" "grep -q 'HostMC through a VPS' '$T/ping.json'"
check "HostMC's own check from outside said so" "head -1 '$W/hostmc/vps/check.txt' 2>/dev/null | grep -qx ok"
check "the server and the tunnel kept running after HostMC quit" "server_alive && keeper_alive && [ -n \"\$(ssh_child)\" ]"

echo "== the tunnel's ssh dies: it has to come back by itself, with HostMC closed"
child=$(ssh_child)
if [ -n "$child" ] && tr '\0' ' ' < "/proc/$child/cmdline" | grep -q "$W/hostmc/vps/key"; then kill "$child"; fi
sleep 1
check "the ping fails while it's down" "! ping_through > /dev/null"
for i in $(seq 1 30); do sleep 1; ping_through > /dev/null && break; done
check "it came back within $i s, and players get through again" "ping_through > /dev/null && [ -n \"\$(ssh_child)\" ] && [ \"\$(ssh_child)\" != \"$child\" ]"

echo "== second run: a new HostMC picks the tunnel up, and stops the server"
keeper_pid=$(keeper)
run hostmc_vps_back.txt back
check "the server stopped" "! server_alive"
check "the tunnel closed with it" "! ours '$keeper_pid' '$W/hostmc/vps' && [ ! -e '$W/hostmc/vps/tunnel.pid' ]"
check "nothing answers on the VPS's port now" "! ping_through > /dev/null"

echo "== third run: the VPS's sshd keeps forwards to itself (GatewayPorts no)"
stop_vps
start_vps no
run hostmc_vps_nogateway.txt nogateway
check "the tunnel still got in" "grep -q 'Authenticated to' '$W/hostmc/vps/tunnel.log'"
check "HostMC's check from outside says to add GatewayPorts" \
      "head -1 '$W/hostmc/vps/check.txt' | grep -qx failed && grep -q 'GatewayPorts clientspecified' '$W/hostmc/vps/check.txt'"
check "the server and the tunnel stopped" "! server_alive && ! keeper_alive"

stop_vps
# Anything left running (a run that went wrong) is stopped by its checked PID.
for p in "$(keeper)" "$(pid_in "$W/hostmc/server/hostmc.pid")"; do
    if ours "$p" "$W/hostmc/vps" || ours "$p" "$W/hostmc/server"; then echo "  (stopping $p, left running)"; kill "$p"; failed=1; fi
done
mkdir -p "$repo/sim/out/hostmc-vps"
for d in "$T"/shots/*/; do cp "$d"*.png "$repo/sim/out/hostmc-vps/" 2>/dev/null; done
echo "screenshots in sim/out/hostmc-vps"
[ "$failed" = 0 ] && rm -rf "$T"
exit $failed
