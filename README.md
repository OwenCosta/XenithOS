# XENITH

**A custom operating shell for the Xbox 360**, running bare metal on the real hardware through
[LibXenon](https://github.com/Free60Project/libxenon). It boots from XeLL instead of the Xbox dashboard,
and comes with its own interface, its own apps, **its own App Store**, **a cloud server**, and **DOOM**.

> **Status: early (v0.3.1).** Everything builds cleanly, and the cloud server is tested end to end over the internet.
> First boot on real hardware (a 360 S, Trinity board) is in progress.

## What's in it

**XENITH shell** (`source/`)
- **Boot splash**: the XENITH logo fades in, an accent bar sweeps across, and the controller rumbles
- **Apps**: launches `.elf` / `.elf32` programs from the root or an `apps/` folder on USB
- **Store**: browse and install apps and game data from your XENITH Cloud server. Every download is
  checked against its SHA-256 before anything is written. It can even update XENITH itself.
- **Cloud**: a live view of your server: load, memory, disk, uptime, ping, a message-of-the-day banner,
  an **inbox** you can message from anywhere, and the **VOLT leaderboard**
- **System**: live CPU / GPU / memory / board temperatures, video output type, resolution
- **Controller**: every button, both sticks and both triggers drawn live, and the triggers drive the rumble motors

**Apps** (`apps/`)
- **VOLT**: steer a streak of light, collect sparks, speed up every 5 sparks. After a crash, **X** uploads
  your score to the Cloud leaderboard.
- **DOOM**: [doomgeneric](https://github.com/ozkl/doomgeneric) ported to XENITH, stretched to 4:3 with full
  controller support. Install **Freedoom** from the Store, or copy your own `doom1.wad` / `doom2.wad` to a `doom/` folder on USB.

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

**Networking uses the built-in Ethernet port** (LibXenon has no Wi-Fi driver). If you use the software
exploit, plug the cable in only after XENITH has booted, because the exploit must run offline.

> ⚠️ FreeMyXe's own warning: never flash a modified NAND or run anything that writes to the NAND while using
> it, or you will brick the console. XENITH never writes to the NAND.

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

It also serves the **XENITH website** (`server/web/index.html`) at `http://<server>:8795/`: live server
stats, the VOLT leaderboard and the Store catalog, drawn with the console's own pixel font.

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
