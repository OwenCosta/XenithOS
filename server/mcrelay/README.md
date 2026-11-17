# mcrelay: HostMC's supporter relay

Supporters on Patreon get a name for their Minecraft: Java Edition server, `<name>.mc.owencosta.diy`,
that friends type into Minecraft. This folder is the relay machine's software, for a small VPS of its own.

```
friend's game ──25565──▶ relay: router ──127.0.0.1:<their port>──▶ sshd ══ SSH tunnel ══▶ HostMC ──▶ server
                         (reads the name)                         (HostMC keeps it open while the server runs)

relay: sync ──every 30 s, signed both ways──▶ XenithOS Cloud ──▶ OCAccounts (who's a supporter)
```

- **XenithOS Cloud** (`server/xenith_cloud.py`, the `Relay` class) gives out the names: it asks OCAccounts
  whether a XenithOS profile is a supporter's, keeps the names, each supporter's port on the relay's loopback
  and the key their HostMC logs in with, and holds names for a while when they're given up (a week) or a
  membership ends (30 days).
- **mcrelay.py router** listens on 25565 for everyone. A game names the address it was given in its first
  packet, so the router reads it and passes the connection to that supporter's port. A name that isn't on
  gets a plain answer in the server list and when joining ("offline right now", "isn't available right now",
  "no server called ..."), not a hang. Players' addresses aren't logged or kept; connections are capped per
  address, per name and overall.
- **mcrelay.py sync** asks XenithOS Cloud for the list every 30 seconds and writes the router's routes and
  the tunnel account's `authorized_keys`. Each key may only open its own port, on the loopback:
  `restrict,port-forwarding,permitlisten="127.0.0.1:<port>"`. It also tells XenithOS Cloud the relay's SSH
  host key (which HostMC pins) and which ports have a tunnel on them (so a port with a tunnel left on it
  goes to no one else). Requests and answers are HMAC-SHA256 signed with a secret only the two share, which
  never crosses the wire; an answer has to name the request's time. When XenithOS Cloud doesn't answer, the
  names from before stay.
- **A second sshd**, for the tunnels only (`mcrelay.py sshd-config` writes its settings): its own port, one
  account (`hostmc`, no password, no shell), keys only, remote forwards only (`AllowTcpForwarding remote`,
  `PermitOpen none`, `MaxSessions 0`), forwards always on the loopback (`GatewayPorts no`). The machine's own
  SSH on port 22 stays as it is.

Every player arrives at the supporter's server from 127.0.0.1 (bans by address don't work for them; by name
they do). Passing the real address on (PROXY protocol, for Paper) could come later.

## Setting it up

Everything here is done by hand by the owner; nothing in this repository has the relay's address.

1. **The relay's VPS** (Debian or Ubuntu, any small one): copy this folder over and run
   `sudo sh setup.sh`. It says what it will do and asks first, then asks for XenithOS Cloud's address
   (the one consoles use). At the end it prints the lines for step 2 and the secret. Run it again later to
   update; the host key, the secret and tunnels that are up stay.
2. **XenithOS Cloud**: add the printed lines to `/etc/xenith-cloud.env`, plus where OCAccounts answers
   (`XENITH_OCACCOUNT_URL`, `XENITH_OCACCOUNT_KEY`) and the Patreon page (`XENITH_RELAY_PATREON`), then
   `systemctl restart xenith-cloud`. The relay opens to supporters once its first sync arrives.
   Words no name may contain go in `relay-blocked-words.txt` in its state folder, one a line.
3. **DNS** for owencosta.diy: `*.mc.owencosta.diy` as an A record to the relay's VPS, DNS only (Cloudflare's
   proxy can't carry Minecraft or SSH). It covers `tunnel.mc.owencosta.diy`, which HostMC's tunnels use.
4. **OCAccounts and Patreon**: "Link Patreon" on the account page and the one endpoint XenithOS Cloud asks,
   `GET /api/partner/xenithos/supporter?profile=<gamertag>` with `X-Partner-Key`, answering
   `{"linked": true, "patreon": true, "supporter": true, "tier": "Supporter"}`. The supporter tier and any
   above it count.

Afterwards, on the relay: `journalctl -u mcrelay-sync -n 5` ("N names on, M held"),
`journalctl -u mcrelay-router`, `systemctl status mcrelay-sshd`. On XenithOS Cloud's machine:
`xenith relay list` (name, profile, state, port, until, membership, last checked, whether a tunnel is up),
`xenith relay off <name>` and `xenith relay on <name>`, and `xenith relay check <name>` to ask OCAccounts
about a name's supporter now (otherwise it's asked every 6 hours, and whenever their HostMC asks).

Worth reading before charging, all the owner's call: Patreon's rules for benefits (the relay is best
effort, with no promise that it's always up, which is how HostMC describes it), Mojang's Minecraft Usage
Guidelines, and the VPS provider's terms on traffic.

## Tests

```bash
python3 server/mcrelay/mcrelay.py --selftest     # the router, the sync and the sshd settings
python3 server/xenith_cloud.py --selftest        # XenithOS Cloud, its relay part with a stand-in OCAccounts
sim/hostmc/test-relay.sh                         # all of it with HostMC, in WSL or any Linux
```
