#!/bin/sh
# HostMC's supporter relay: makes this machine the relay (see mcrelay.py). Run it as root on the
# relay's VPS (Debian or Ubuntu), from this folder:
#
#   sudo sh setup.sh
#
# It says what it will do and asks first. Run again, it keeps the host key, the secret and the
# ports and brings mcrelay.py, the settings and the services up to date; tunnels that are up stay up.
#   MCRELAY_PLAYERS_PORT  the port players connect to (default 25565, or what it was). When another
#                         Minecraft server has 25565 on this machine, use another, like 25566:
#                         friends then type <name>.<domain>:25566
#   MCRELAY_SSH_PORT      the port for HostMC's tunnels (default 2222; 22 stays your own SSH)
#   MCRELAY_DOMAIN        the names' domain, for the steps it prints at the end (default mc.owencosta.diy)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
ssh_port=${MCRELAY_SSH_PORT:-2222}
domain=${MCRELAY_DOMAIN:-mc.owencosta.diy}
players_port=${MCRELAY_PLAYERS_PORT:-}
if [ -z "$players_port" ] && [ -f /etc/mcrelay/router.env ]; then
    players_port=$(sed -n 's/^MCRELAY_LISTEN=.*:\([0-9]*\)$/\1/p' /etc/mcrelay/router.env | head -n 1)
fi
players_port=${players_port:-25565}

if [ "$(id -u)" != 0 ]; then echo "Run it as root: sudo sh setup.sh"; exit 1; fi
if [ ! -f "$here/mcrelay.py" ]; then echo "mcrelay.py has to be next to setup.sh"; exit 1; fi
for port in "$ssh_port" "$players_port"; do
    case "$port" in ''|*[!0-9]*) echo "Ports are numbers, not $port"; exit 1;; esac
    if [ "$port" -lt 1 ] || [ "$port" -gt 65535 ] || [ "$port" = 22 ]; then echo "Pick another port than $port"; exit 1; fi
done
if [ "$ssh_port" = "$players_port" ]; then echo "The tunnels and the players need a port each"; exit 1; fi

taken() {                        # taken PORT OURS: something listens on PORT that isn't OURS (a command line part)
    for pid in $(ss -Hltnp "sport = :$1" 2>/dev/null | grep -o 'pid=[0-9]*' | cut -d= -f2 | sort -u); do
        tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -q -- "$2" || return 0
    done
    return 1
}
if taken "$players_port" "/opt/mcrelay/mcrelay.py router"; then
    echo "Another program already takes players on port $players_port here (another Minecraft server?)."
    echo "Give the relay another port, like this: MCRELAY_PLAYERS_PORT=25566 sh setup.sh  (nothing was changed)"
    exit 1
fi
if taken "$ssh_port" "/etc/mcrelay/sshd_config"; then
    echo "Another program already uses port $ssh_port here. Pick another one for the tunnels:"
    echo "MCRELAY_SSH_PORT=2223 sh setup.sh  (nothing was changed)"
    exit 1
fi

cloud=""
if [ -f /etc/mcrelay/mcrelay.env ]; then
    cloud=$(sed -n 's/^MCRELAY_CLOUD=//p' /etc/mcrelay/mcrelay.env | head -n 1)
fi

cat <<EOF
This makes this machine HostMC's supporter relay. It will:
  - install python3 and openssh-server, if they're missing (apt)
  - add an account, hostmc, for HostMC's tunnels: no password and no shell
  - run a second sshd on port $ssh_port, for those tunnels only (your own SSH stays as it is)
  - run mcrelay: players on port $players_port, and the names from XenithOS Cloud every 30 seconds
  - open ports $players_port and $ssh_port in ufw, if ufw is on
EOF
printf "Go ahead? [y/N] "
read -r answer
case "$answer" in y|Y|yes|Yes) ;; *) echo "Nothing was changed."; exit 0;; esac

if [ -z "$cloud" ]; then
    printf "XenithOS Cloud's address, as consoles use it (like http://203.0.113.5:8795): "
    read -r cloud
fi
case "$cloud" in http://?*|https://?*) ;; *) echo "That has to start with http:// (nothing was changed)"; exit 1;; esac

need=""
command -v python3 >/dev/null 2>&1 || need="$need python3"
[ -x /usr/sbin/sshd ] || need="$need openssh-server"
if [ -n "$need" ]; then
    apt-get update
    # shellcheck disable=SC2086 # one word a package
    apt-get install -y $need
fi

if ! id hostmc >/dev/null 2>&1; then
    useradd --system --home-dir /var/lib/mcrelay --no-create-home --shell /usr/sbin/nologin hostmc
fi
usermod -p '*' hostmc          # no password at all: sshd without PAM refuses keys for a locked one
install -d -m 755 /etc/mcrelay /opt/mcrelay
install -d -o hostmc -g hostmc -m 755 /var/lib/mcrelay
install -m 644 "$here/mcrelay.py" /opt/mcrelay/mcrelay.py
if [ ! -f /etc/mcrelay/ssh_host_ed25519_key ]; then
    ssh-keygen -q -t ed25519 -N "" -C mcrelay -f /etc/mcrelay/ssh_host_ed25519_key
fi
if [ ! -s /etc/mcrelay/secret ]; then
    ( umask 077; python3 -c 'import secrets; print(secrets.token_hex(32))' > /etc/mcrelay/secret )
fi
chown root:hostmc /etc/mcrelay/secret
chmod 640 /etc/mcrelay/secret
printf 'MCRELAY_CLOUD=%s\n' "$cloud" > /etc/mcrelay/mcrelay.env
printf 'MCRELAY_LISTEN=0.0.0.0:%s\n' "$players_port" > /etc/mcrelay/router.env
python3 -I /opt/mcrelay/mcrelay.py sshd-config "$ssh_port" hostmc /var/lib/mcrelay/authorized_keys \
    /etc/mcrelay/ssh_host_ed25519_key /run/mcrelay-sshd.pid > /etc/mcrelay/sshd_config
if [ ! -f /var/lib/mcrelay/authorized_keys ]; then
    install -o hostmc -g hostmc -m 600 /dev/null /var/lib/mcrelay/authorized_keys
fi
install -d -m 755 /run/sshd
/usr/sbin/sshd -t -f /etc/mcrelay/sshd_config

for unit in mcrelay-sshd mcrelay-sync mcrelay-router; do
    install -m 644 "$here/$unit.service" /etc/systemd/system/
done
systemctl daemon-reload
for unit in mcrelay-sshd mcrelay-sync mcrelay-router; do
    systemctl enable "$unit" >/dev/null 2>&1
    systemctl reset-failed "$unit" >/dev/null 2>&1 || true
    systemctl restart "$unit"
done
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q "Status: active"; then
    ufw allow "$players_port/tcp" >/dev/null
    ufw allow "$ssh_port/tcp" >/dev/null
fi
if [ "$players_port" = 25565 ]; then at=""; else at=":$players_port"; fi

cat <<EOF

Done: the relay is running, for players on port $players_port. It has no names until XenithOS Cloud
gives it some. Friends will type <name>.$domain$at.

1. Give XenithOS Cloud these settings where it keeps its others (/etc/xenith-cloud.env for the
   systemd service; a Docker setup's env file or compose file), then start it again:

     XENITH_RELAY_DOMAIN=$domain
     XENITH_RELAY_SSH=tunnel.$domain:$ssh_port
     XENITH_RELAY_PLAYERS_PORT=$players_port
     XENITH_RELAY_SECRET=<this machine's secret: sudo cat /etc/mcrelay/secret>
     XENITH_OCACCOUNT_URL=<where OCAccounts answers, from its owner>
     XENITH_OCACCOUNT_KEY=<the key OCAccounts gives XenithOS Cloud>
     XENITH_RELAY_PATREON=<your Patreon page>

   The secret works like a password: copy it straight across, and keep it out of chats and the
   repository.

2. DNS, as DNS only (Cloudflare's proxy can't carry Minecraft or SSH):

     *.$domain   A   this machine's address

   It covers tunnel.$domain too, which HostMC's tunnels use.

3. Let ports $players_port and $ssh_port in through your VPS provider's firewall too, if it has one.

4. A minute after step 1, journalctl -u mcrelay-sync -n 5 should say "0 names on, 0 held".
   Your own SSH on port 22 is as it was.
EOF
