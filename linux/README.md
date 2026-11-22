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

### Bluetooth controllers

**Settings > Bluetooth controllers** pairs one with the controller alone, no keyboard needed: put it in
pairing mode, and choose it when it shows up. XenithOS pairs it, trusts it (so from then on it connects by
itself whenever it's turned on) and connects it. A paired one can be connected, disconnected or forgotten
there too, and Y lists every device nearby, for a controller that doesn't say what it is.

It drives BlueZ's `bluetoothctl` (`linux/bluetooth_linux.c`) in a terminal of its own, with an agent that
needs no PIN or confirming, and switches Bluetooth on (and out of rfkill's block) when it's off. The
installer adds BlueZ, starts its service and puts your user in the `bluetooth` group; a machine installed
before this needs the install command once more, which the screen says. No Bluetooth at all? A USB
Bluetooth adapter works.

## Where things are

| Path | What |
|---|---|
| `/usr/lib/xenithos/` | XenithOS and its apps |
| `~/.local/share/xenithos/uda:/` | your settings, profile, game data and saves (same layout as the Xbox's USB stick) |
| `~/.local/share/xenithos/uda:/xenith/crash/` | crash files waiting to be sent, one a program (see Crash reports) |
| `~/.config/xenithos/` | the mode file and logs |
| `~/.local/share/minenest/` | MineNest's Minecraft servers, their worlds, Java and backups |
| `/usr/lib/xenithos/bin/minenest` | MineNest's own program (also `minenest` in a terminal) |

XenithOS's own apps (VOLT, DOOM, Tube, Photos, Music, and MineNest on PCs) come with it on PCs and Pis. The Store has three sections here
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

## MineNest: Minecraft servers

MineNest (in Apps) runs Minecraft servers on the PC. It used to be HostMC; it's its own project now,
[MineNest](https://github.com/OwenCosta/minenest), for any Windows or Linux PC, and on XenithOS the
app is its screen, in XenithOS's look, for a controller or the mouse.

- MineNest's own program runs the servers: `/usr/lib/xenithos/bin/minenest`. It's the release that
  `linux/minenest.sha256` names, kept only if it matches the SHA-256 written there (the release's own
  SHA256SUMS isn't trusted), downloaded by `linux/get-minenest.sh` as the XenithOS user (install.sh
  runs it with sudo -u; nothing downloaded runs as root). That file comes inside XenithOS's source,
  whose SHA-256 is in XenithOS Cloud's signed Store list, so a new MineNest reaches PCs only through a
  XenithOS commit that changes the version and both hashes. The app starts the program the first time
  it's needed, and talks to it on the loopback with the key in its folder. Servers keep running after
  the app closes. `minenest help` in a terminal does everything the app does.
- Paper, Purpur, Fabric, NeoForge, Forge or Minecraft's own, any version and loader; mods, plugins
  and modpacks from Modrinth; backups; players; settings; playit.gg, a VPS of your own or the
  supporter relay; Bedrock players through Geyser. MineNest's README says how each works.
- Before a server's first start the app shows Minecraft's EULA (https://aka.ms/MinecraftEULA) and
  starts it only once you've agreed.
- The XenithOS profile you're signed in with is handed to MineNest, for the supporter relay. On a
  PC without XenithOS, MineNest signs in with your OCAccount instead.
- A server HostMC made (`~/.local/share/xenithos/hostmc/server`) is offered for bringing in on the
  Server page: MineNest copies it, world and all, and leaves HostMC's folder as it was.
- HostMC's achievements carry on in MineNest, under the same names.
- New MineNest versions come with XenithOS's updates. get-minenest.sh leaves `bin/minenest.managed`
  beside the program, so the app has no Update button, and MineNest (from the version after 0.2.0)
  leaves its own Update button and `minenest update` alone. MineNest 0.2.0's own window still offers
  Update: it replaces only the program this account owns, until XenithOS's next update puts the pinned
  one back.
- On XenithOS Desktop, MineNest in the Apps menu opens MineNest's own window.
- `linux/test-minenest.sh` tries the app in a virtual display with MineNest's program
  (`MINENEST_BIN`) and a stand-in server (`sim/minenest/java`): starting, a player joining, leaving
  the app with the server running, finding it again, stopping it, and the desktop window.

## Updates

**XenithOS update** in the Store downloads the new version's source from XenithOS Cloud (it's in the
Store as `xenithos-pc`, kind `pc`, with its SHA-256 checked like everything else), unpacks it in
`~/.cache/xenithos-update`, and runs the `linux/update.sh` that comes with it. That builds XenithOS and
the apps and puts them in place of the old ones in `/usr/lib/xenithos`, which `install.sh` makes yours so
no sudo is needed, then copies the apps' names and thumbnails into the data folder. XenithOS then restarts
into the new program. If the build fails, nothing is replaced, and the build's output is in
`update.log` in `uda:/xenith`. Machines installed before 0.5.3 have a root-owned `/usr/lib/xenithos`:
run the install command once more and the Store takes over after that.

## Crash reports

XenithOS and every app catch the signals a crash ends in (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL;
`crash_linux.c`, set up in `plat_init`) and leave a crash file in `uda:/xenith/crash/<program>.txt`: the
version, the signal and the address it was about, how far the program's start got, the program's build id
and where it was loaded, the stack, and for a glibc abort its message (`*** buffer overflow detected ***` is
how HostMC's crash on Ubuntu looked). The handler only makes async-signal-safe calls; it runs `addr2line` in a
child process, so the file names functions and source lines (the programs are built with `-g` and aren't
stripped). Then the program ends just as it would have. Next time XenithOS starts, or as soon as it's back
from the app, it sends the report to XenithOS Cloud, or asks first: Settings > Send crash reports, "Ask
first" until it's set to "Always". Paths in the home folder are cut down to file names before anything goes.

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

`linux/test-crash.sh` crashes VOLT and XenithOS on purpose (`XENITHOS_TEST_CRASH=segv`, `abort`, `fortify`,
`stack`, `fpe`, `ill` or `bus`, or `volt:segv` for only that program, at frame `XENITHOS_TEST_CRASH_FRAME`)
in a virtual display against a local XenithOS Cloud: a report sent by itself, one sent once asked, and one
the answer was no to. Each that arrives must say where it stopped and have nothing from the home folder in it.

## Apps inside XenithOS

Apps are their own programs, but XenithOS starts each one with `XENITHOS_GUEST_FD` pointing at shared
memory: the app draws its frames and queues its sound there, and XenithOS shows them in its own window,
plays the sound and passes the controller and keyboard on. The screen and the sound device never change
hands, so switching doesn't go black. Hold Guide for two seconds, or press Ctrl+Alt+Backspace, to close an
app that has stopped responding. An app started on its own (outside XenithOS) opens its own window as before.
