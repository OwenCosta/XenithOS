# XENITH

XENITH is a homebrew shell for the Xbox 360. It boots from XeLL in place of the Microsoft dashboard and runs
its own menu and apps, written in C on [LibXenon](https://github.com/Free60Project/libxenon). There's a small
store that downloads apps over Ethernet, a Python server that backs it, and a DOOM port.

I'm building it on a 360 S (Trinity board). Status as of v0.3.1: everything compiles and the server is
running, but none of it has been run on the console yet. The first boot is in progress.

## What's in it

Shell (`source/`):
- Boot splash with a short controller rumble
- Apps: launches `.elf` / `.elf32` files from the root or an `apps/` folder on USB
- Store: installs apps and game data from the server. Each download's SHA-256 is checked before it's
  written, and the store can also update XENITH itself.
- Cloud: server load, memory, disk, uptime and ping, a message banner, an inbox, and the VOLT leaderboard
- System: CPU / GPU / memory / board temperatures, video output, resolution
- Controller: shows every button, stick and trigger, and the triggers run the rumble motors

Apps (`apps/`):
- VOLT: a light-trail game. Grab sparks, speed up every 5. Press X after a crash to upload your score.
- DOOM: [doomgeneric](https://github.com/ozkl/doomgeneric) with controller support, scaled to 4:3.
  Get Freedoom from the store, or copy your own `doom1.wad` / `doom2.wad` into a `doom/` folder on USB.

| DOOM control | |
|---|---|
| Left stick | move / strafe |
| Right stick, D-pad | turn |
| RT | fire |
| LT or L3 | run |
| A | use / menu select |
| B | menu back |
| X | automap |
| Y | "yes" in prompts |
| LB / RB | previous / next weapon |
| Start | menu |
| Hold Back | quit to XENITH |

## Running it on a console

You need a console that can reach **XeLL**. There are two ways:

| Route | Soldering | Notes |
|---|---|---|
| **Software exploit**: [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) + [FreeMyXe](https://github.com/FreeMyXe/FreeMyXe), then FreeMyXe's *launch XeLL* option | None | Needs dashboard 17559, offline, and the hard drive attached (for avatar data). Not persistent: rerun it after every power off. |
| **RGH 3.0** | 2 wires | Boots XeLL every time |

USB stick layout (FAT32), as `./build.sh` stages it in `usb/`:

```
xenon.elf               XENITH (XeLL loads this automatically)
apps/volt.elf32         apps show up under APPS
apps/doom.elf32
xenith/config.txt       settings (below)
data/                   game data installed by the Store
doom/                   DOOM config and saves
```

Networking only works over the Ethernet port, because LibXenon has no Wi-Fi driver. If you use the
software exploit, plug the cable in after XENITH has booted, since the exploit has to run offline.

**Warning from FreeMyXe:** never flash a modified NAND or run anything that writes to the NAND while using
it, or you will brick the console. XENITH only writes to the USB stick.

### Settings: `xenith/config.txt`

```
server=203.0.113.10:8795    # your XENITH Cloud server, IP:port (the console has no DNS)
name=PLAYER                 # leaderboard name: letters and digits, up to 12
key=...                     # console key: matches XENITH_CONSOLE_KEY so only your Xbox reads its inbox
```

`build.sh` copies `config.local.txt` (git-ignored, so your server address stays private) to the USB layout,
or `config.example.txt` if you don't have one.

## XENITH Cloud (the server)

`server/xenith_cloud.py` is a single Python 3 file with no dependencies. It serves the Store, the inbox,
leaderboards and live server stats over plain HTTP (the console has no TLS).

It also serves the project website (`server/web/index.html`) at `http://<server>:8795/`. The page has a
playable copy of the XENITH menu drawn in the console's font, plus the leaderboard, store list and server stats.

```bash
python3 server/xenith_cloud.py --selftest      # 37 checks, no network needed
```

Install on a Linux server (the unit runs as a throwaway user with a read-only system):

```bash
mkdir -p /opt/xenith-cloud/store /opt/xenith-cloud/web
cp server/xenith_cloud.py /opt/xenith-cloud/
cp server/web/index.html /opt/xenith-cloud/web/
cp server/xenith-cloud.service /etc/systemd/system/
install -m 0755 server/xenith /usr/local/bin/xenith
umask 077
echo "XENITH_ADMIN_KEY=$(openssl rand -hex 32)" > /etc/xenith-cloud.env
echo "XENITH_CONSOLE_KEY=$(openssl rand -hex 24)" >> /etc/xenith-cloud.env   # also goes in config.txt as key=
# put your .elf32 / .wad files and a store.json (see server/store.example.json) in /opt/xenith-cloud/store
systemctl daemon-reload && systemctl enable --now xenith-cloud
ufw allow 8795/tcp
```

Then, from the server:

```bash
xenith send "Dinner is ready"      # pops up in the Xbox's CLOUD inbox
xenith motd "Game night at 9"      # banner across the top of the CLOUD screen
xenith status
```

Security notes: downloads are checked against the catalog's SHA-256, which catches corruption. Because
the console can't do TLS, someone who controls your network path could still swap both. Store items can only be written to `apps/`, `data/`, or XENITH itself (after a confirmation), never to a path the server picks. Admin routes need the key, and
leaderboard posts are validated and rate limited.

## Building

XENITH builds with the LibXenon toolchain on Linux (WSL works fine).

```bash
# one time: build the toolchain into ~/xenon
git clone https://github.com/Free60Project/libxenon
cd libxenon/toolchain
PREFIX=$HOME/xenon ./build-xenon-toolchain toolchain
PREFIX=$HOME/xenon ./build-xenon-toolchain libxenon
PREFIX=$HOME/xenon ./build-xenon-toolchain filesystems

# every time
cd xenith-360
./build.sh
```

## Layout

```
source/main.c            shell: splash, home, apps, system, controller
source/screens_cloud.c   STORE and CLOUD screens
source/ui.c|h            shared shell UI: input, chrome, rows, dialogs, progress
shared/gfx.c|h           drawing straight into the tiled Xenos framebuffer, 5x7 font, scaler
shared/net.c|h           HTTP/1.0 client on lwIP's raw API (DHCP, no DNS)
shared/sha256.c|h        SHA-256 for verifying downloads
shared/config.c|h        xenith/config.txt
shared/theme.h           the shared palette, so every app looks like part of the system
apps/volt/               VOLT
apps/doom/               DOOM: doomgeneric_xenith.c + vendored doomgeneric/
server/                  XENITH Cloud server, systemd unit, admin wrapper
build.sh                 builds everything and stages usb/
```

## Writing an app

Copy `apps/volt`, rename the folder, then edit `source/`. Include `gfx.h` and `theme.h` to match the system look,
`net.h` + `config.h` to talk to XENITH Cloud, and call `elf_runFromDisk(XENITH_SHELL_PATH)` to return to XENITH.
`build.sh` picks up every folder in `apps/`. To publish an app, add it to your server's `store.json`.

## Roadmap

- Sound (XENITH UI sounds, DOOM sound effects through `xenon_sound`)
- Bigger lwIP TCP window for faster Store downloads
- Read the Xbox hard drive (XTAF) for apps and data, not just USB
- Push new builds straight from the PC to the console over LAN
- App icons in the Store

## License

XENITH is licensed under the **GNU GPL v3.0**, see [LICENSE](LICENSE).

- LibXenon is BSD licensed.
- The Makefiles are adapted from devkitxenon's example Makefiles, which are GPL (derived from devkitPro).
- `apps/doom/doomgeneric/` is [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL v2 or later, see its
  `LICENSE`; upstream commit in `UPSTREAM_COMMIT`). XENITH's change: `m_config.c` stores config and saves in `uda:/doom/`.
- Freedoom (served by the Store, not included here) is BSD licensed, see [freedoom.github.io](https://freedoom.github.io).

Not affiliated with or endorsed by Microsoft or id Software. Xbox and Xbox 360 are trademarks of Microsoft.
DOOM is a trademark of id Software.
