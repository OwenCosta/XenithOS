# XenithOS on PCs and Raspberry Pis

The same XenithOS as on the Xbox 360, built for Linux with SDL2. Installed, the machine starts straight into
XenithOS (**console mode**) and can switch to a normal Linux desktop (**desktop mode**) and back.

## Installing

1. Install a Debian-based system:
   - **Raspberry Pi** (3, 4, 5 or 400): Raspberry Pi OS Lite (64-bit) with Raspberry Pi Imager. Set a user and,
     if you need it, Wi-Fi in the Imager's settings.
   - **PC**: Debian 12 or newer, or Ubuntu 22.04 or newer. A minimal install without a desktop is fine.
2. Run the installer from the XenithOS website, signed in as the user XenithOS should run as:

   ```bash
   curl -fsSL http://<XenithOS Cloud address>/install.sh | sudo bash
   ```

   Or from a copy of this repository: `sudo bash linux/install.sh`.
3. Restart. XenithOS starts by itself.

Options: `--no-desktop` skips desktop mode (saves about 1 GB), `--user NAME` picks the account, `--server
HOST:PORT` sets the XenithOS Cloud address, `--uninstall` puts back a normal login (your XenithOS data stays).

What it does: installs the packages it needs (SDL2, a compiler, XFCE for desktop mode, and Flatpak, cage,
Xwayland and a sound server for apps from Flathub), builds XenithOS and its apps into `/usr/lib/xenithos`,
logs your user in by itself on the first console, and starts `xenithos-session` from `~/.profile`. Machines
that started on a graphical login screen are set to start in text mode instead, which `--uninstall` undoes.

## Console mode and desktop mode

- Console mode is XenithOS full screen with no desktop underneath. Since 0.5.5 XenithOS starts its own X
  server for it (with startx, like desktop mode) and is its only program, so apps from Flathub and Steam can
  open their windows over XenithOS's (see below). On machines without an X server (installed with
  `--no-desktop`) it draws straight to the display, as before.
- Desktop mode: XenithOS power menu, **Switch to desktop mode**.
- Back to XenithOS: **Return to XenithOS** in the desktop's application menu, or log out.

The mode is kept in `~/.config/xenithos/mode`. If XenithOS can't start (no usable graphics driver, say), the
session falls back to desktop mode, or a plain shell when there's no desktop. What it printed is in
`~/.config/xenithos/xenithos.log`, and the X server's own log in `~/.local/share/xorg/`. In desktop mode
you can also run XenithOS in a window from the menu.

If console mode's X server doesn't get as far as XenithOS, the next start draws straight to the display
instead and leaves `~/.config/xenithos/kiosk-failed` (holding the version), so a broken X server can't stop
XenithOS from starting; it tries X again after the next update. Delete that file to try again sooner, or
set `XENITHOS_KIOSK=0` to never use it.

## Controls

Any controller SDL knows works: Xbox, PlayStation, Switch Pro and most others. On a keyboard:

| Key | Does |
|---|---|
| Arrows | move around |
| Enter or Space | A |
| Escape | B |
| X, Y | X, Y |
| Q, E | LB, RB |
| Tab | the power menu (Back) |
| Ctrl+Alt+Backspace | close an app that has the screen (apps from Flathub, Steam) |
| F1 | Start |
| Page Up / Page Down, Ctrl | LT / RT |
| W A S D | left stick (in games) |

In the on-screen keyboard you can type on the real keyboard: letters type, Backspace deletes, Enter finishes.

## Where things are

| Path | What |
|---|---|
| `/usr/lib/xenithos/` | XenithOS and its apps |
| `~/.local/share/xenithos/uda:/` | your settings, profile, game data and saves (same layout as the Xbox's USB stick) |
| `~/.config/xenithos/` | the mode file and logs |
| `~/.local/share/xenithos/hostmc/` | HostMC's Minecraft server, its world, Java and backups |

XenithOS's own apps (VOLT, DOOM, Tube, Photos, and HostMC on PCs) come with it on PCs and Pis. The Store has three sections here
(LB and RB switch between them): **Apps** from Flathub, **Steam**, and **XenithOS** (updates and game data).

## XenithOS Cloud over HTTPS

The Xbox talks to XenithOS Cloud over plain HTTP, since it has no TLS. PCs and Pis use HTTPS when XenithOS Cloud
has an HTTPS address, so passcodes, sign-ins and everything else can't be read on the way.

- Each time XenithOS starts it asks the address in `config.txt` what else it answers at (`/v1/lan`). When that
  names an HTTPS address, and the server answers there as itself with a certificate this machine trusts for
  that name, everything goes there until the next start: profiles, the Store, Tube, scores. Settings > System
  info says "Connected securely".
- The address is kept in `uda:/xenith/route.txt` (`https=`) for the apps, next to the home-network address.
  At home that one still comes first, with HTTPS as the way out if the machine leaves.
- If the HTTPS address doesn't answer, or its certificate doesn't check out, XenithOS stays on plain HTTP for
  that start, and says why in `~/.config/xenithos/xenithos.log`. If the address in `config.txt` doesn't answer
  at all, it keeps the HTTPS address it had.
- `config.txt` can say `server=https://name` itself (with `:port` when it isn't 443).
- It's the system's OpenSSL (libssl, which ca-certificates needs, so every Debian, Ubuntu and Raspberry Pi OS
  has it), opened when it's first needed, and the system's certificates. Building XenithOS doesn't need
  OpenSSL's headers, so updates from the Store build as before.
- `sim/test-https.sh` tries all of it here, with a stand-in for Cloudflare (`sim/tls_proxy.py`).

## Apps from Flathub, and Steam

The Store's Apps section lists apps from [Flathub](https://flathub.org) that XenithOS Cloud picks
(`flathub.json` on the server: Roblox through Sober, Minecraft through Prism Launcher, Kodi, Firefox,
Spotify, Discord, Moonlight, emulators, games and more). Install downloads one with Flatpak, for your
user only (`flatpak install --user`, no sudo), behind XenithOS's own progress screen; B stops it. The
first app also fetches the shared parts apps are built on (Flatpak runtimes, up to a gigabyte or so). An
installed app is `uda:/apps/<id>.app`:

```
name=Kodi
flatpak=tv.kodi.Kodi
args=
```

It's on the home screen and in Apps like the rest. These apps aren't drawn inside XenithOS's window, so
they open their own. In console mode they open over XenithOS on its X server: XenithOS manages their
windows itself (linux/kiosk.c), so an app's window fills the screen, dialogs and windows that can't be
resized (installers, say) are centred, the newest has the keyboard, and XenithOS's "Starting" screen
stays up until the app's window shows. Nothing goes black in between. Without an X server, XenithOS
hands the display over and the app runs under **cage**, a kiosk compositor. In desktop mode XenithOS's
window steps aside. **Hold Guide for three seconds** (or press
Ctrl+Alt+Backspace) to close the app and come back to XenithOS; the app is asked to close first, so it
can save. X on an installed app in the Store updates or removes it (its settings stay in
`~/.var/app`). Apps removed some other way (from the desktop, say) disappear from the home screen too.

**Steam** is in the Steam section (x86-64 PCs only; Steam doesn't run on ARM). It opens in Big Picture,
made for controllers; sign in with your Steam account the first time. The section also lists games on
Steam: getting one opens Steam on it, after the Store says how it works (Steam games install and run
through Steam with your Steam account; free games only need the account, others are bought on Steam).
Every game installed in Steam shows up on XenithOS's home screen with Steam's picture of it
(`uda:/apps/steam-<appid>.app`, kept up to date from Steam's library folders), and starts straight into
the game. When the game ends, XenithOS closes Steam and comes back, as long as Steam says the game has
ended in its `registry.vdf`; otherwise hold Guide.

Roblox runs through **Sober**, a community project that isn't made or supported by Roblox (its makers
say to use it at your own risk), and only on x86-64 PCs. Some apps (Firefox, Discord, Spotify, Prism
Launcher) need a keyboard and mouse.

Your own: any `.app` file with `name=` and `exec=` (a program and its arguments, split at spaces, no
shell) is started the same way.

Machines installed before 0.5.4 need the install command once more for Flatpak; the Store says so if
it's missing. What Flatpak and the apps print goes to `flatpak.log` and `app.log` in `uda:/xenith`. If
Minecraft won't start from Prism Launcher, its own log is in
`~/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances/<name>/.minecraft/logs/latest.log`.

Apps get XenithOS's environment minus its own SDL settings, under both SDL names: where SDL2 is sdl2-compat
on top of SDL3, `SDL_VIDEODRIVER=kmsdrm` is also copied to `SDL_VIDEO_DRIVER`, which sent SDL3 programs
(Sober) to the bare console in 0.5.4 and made them stop with "no video subsystem".

## HostMC: a Minecraft server

HostMC (in Apps) runs a Minecraft: Java Edition server on the PC. Start it, and Minecraft on the same PC
(Prism Launcher) joins at `localhost`, friends at home join at the PC's address on the network, which
HostMC shows, and with playit.gg turned on in its Internet page, friends anywhere join at the address
playit.gg gives it. Its pages: Server (start, stop, where to join, who's on), Console (the server's output,
and commands), Players (operators, kicks, bans, the whitelist), Settings (`server.properties`: message,
game mode, difficulty, max players, whitelist, seed, online mode, port, and the server's memory), Version
(Minecraft or Paper, the latest release or an earlier one), World (backups, putting one back, a new world)
and Internet (playit.gg).

- The server keeps running after you leave HostMC, so you can go and play on it, and HostMC picks it up
  again next time. It's started in a session of its own; `server/hostmc.pid` holds its PID and start time,
  and HostMC checks the process is still that server, running in its folder, before it signals it.
  Commands reach it through `server/console.in`, a named pipe that's its standard input.
- Before the first start HostMC shows Minecraft's EULA (https://aka.ms/MinecraftEULA) and starts the server
  only once you agree. It writes your answer into `eula.txt`.
- The first start downloads the server from Mojang (or Paper from PaperMC) and the Java it needs:
  Minecraft 26.x needs Java 25, 1.20.5 to 1.21.x need 21, and Debian 12 only packages 17. So HostMC
  downloads Eclipse Temurin's runtime from Adoptium (about 60 MB) into its own folder, where nothing else
  uses it. It says what it's about to download first, and checks each download against the checksum its
  source publishes. Downloads go through `curl`, which the installer puts in.
- playit.gg is off unless you turn it on. Then HostMC downloads playit's agent (playitd, about 9 MB) from
  playit's releases on GitHub, checked against the SHA-256 GitHub publishes, and shows a link and a QR code
  to approve it with your playit.gg account. The agent's key stays in `playit/playit.toml`, readable by you
  only, and the agent runs while the server does. HostMC asks playit.gg for a Minecraft tunnel to the
  server, and shows its address.
- Or, in place of playit.gg, a VPS of your own: HostMC asks for its address, the user it logs in as, its SSH
  port and the port for players, then keeps an SSH reverse tunnel to it while the server runs
  (`ssh -N -R 0.0.0.0:<port for players>:127.0.0.1:<server port>`), so friends join at the VPS's address.
  - HostMC logs in with a key of its own (ed25519, in `vps/`, readable by you only), never a password, and
    shows the one line to add to `~/.ssh/authorized_keys` on the VPS, with a QR code of a command that adds
    it. The VPS's host key is trusted the first time and checked after that, in a `known_hosts` of its own.
  - The VPS needs `GatewayPorts clientspecified` in `/etc/ssh/sshd_config` (without it sshd listens on its
    own loopback only) and the port open in its firewall. Each time the tunnel comes up, HostMC pings the
    server through the VPS's public address, the way a friend's game would, and says plainly which of
    those is missing (and keeps what it found in `vps/check.txt`).
  - A small `sh` loop keeps ssh going and starts it again when it drops, waiting a little longer each time
    (up to a minute), whether HostMC is open or not; the next HostMC finds it by `vps/tunnel.pid`. Its
    tries are in `vps/tunnel.log`.
  - TCP only, which Java Edition is. Every player arrives from 127.0.0.1, so bans by address don't work
    for them; bans by name do. It needs `ssh` (openssh-client), which the installer puts in.
  - `sim/hostmc/test-vps.sh` tries all of it with a private sshd standing in for the VPS (run as you, on
    spare ports): the key, the login test, a ping through the VPS, ssh killed and coming back by itself, a
    new HostMC picking the tunnel up, and the message when GatewayPorts is off.
- In `~/.local/share/xenithos/hostmc/`: `server/` (the server, its world, `server.properties` and
  `console.log`), `java/`, `backups/` (the newest 20, as `.tar.gz`), `playit/`, `vps/` and `cache/`
  (version lists). `HOSTMC_JAVA` points it at another `java` (the simulator uses a stand-in).

## Updates

**XenithOS update** in the Store downloads the new version's source from XenithOS Cloud (it's in the
Store as `xenithos-pc`, kind `pc`, with its SHA-256 checked like everything else), unpacks it in
`~/.cache/xenithos-update`, and runs the `linux/update.sh` that comes with it. That builds XenithOS and
the apps and puts them in place of the old ones in `/usr/lib/xenithos`, which `install.sh` makes yours so
no sudo is needed, then copies the apps' names and thumbnails into the data folder. XenithOS then restarts
into the new program. If the build fails, nothing is replaced, and the build's output is in
`update.log` in `uda:/xenith`. Machines installed before 0.5.3 have a root-owned `/usr/lib/xenithos`:
run the install command once more and the Store takes over after that.

## Building by hand

```bash
sudo apt install build-essential libsdl2-dev zlib1g-dev
make -C linux
linux/xenithos             # in a window, when run from a desktop
```

For testing without a screen: `SDL_VIDEODRIVER=offscreen XENITHOS_SHOT=home.png XENITHOS_SHOT_FRAME=200
linux/xenithos` saves frame 200 and quits. `XENITHOS_DATA` changes the data folder.

`linux/test-other-apps.sh` (or `linux/test-other-apps.sh console`) does the same for apps that take the
screen, with stand-ins for Steam, cage and Flatpak: a Steam game on the home screen with its picture,
Steam closed again when the game ends, and a failing app explained.

`linux/test-console-mode.sh` checks console mode's X server and window manager in a 1920x1080 virtual
display with a stand-in startx: xclock must fill the screen, a window that can't be resized must be
centred, a slow app must leave the "Starting" screen moving, each must give the screen back when closed
the way holding Guide does, and a startx that fails must fall back to drawing directly.

`linux/test-in-window.sh volt` (or `doom`) runs the whole thing in a virtual display (needs `xvfb xdotool
x11-apps imagemagick`): it boots XenithOS, starts the app, leaves it, and moves around the home screen,
taking screenshots through each switch and recording the sound to a file. It prints how bright every
screenshot is (0 would be a black screen) and how loud each quarter second was, so a black gap or missing
sound after coming back shows up straight away.

## Apps inside XenithOS

Apps are their own programs, but XenithOS starts each one with `XENITHOS_GUEST_FD` pointing at shared
memory: the app draws its frames and queues its sound there, and XenithOS shows them in its own window,
plays the sound and passes the controller and keyboard on. The screen and the sound device never change
hands, so switching doesn't go black. Hold Guide for two seconds, or press Ctrl+Alt+Backspace, to close an
app that has stopped responding. An app started on its own (outside XenithOS) opens its own window as before.
