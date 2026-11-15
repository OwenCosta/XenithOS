# XenithOS

XenithOS is a homebrew shell for the Xbox 360. It boots from XeLL in place of the Microsoft dashboard and runs
its own home screen, profiles with avatars, a store that installs apps over Ethernet, and a DOOM port. It's
written in C on [libxenon](https://github.com/Free60Project/libxenon).

I'm building it on a 360 S (Trinity board). Version 0.3.1 has booted on the real console. 0.4.0, the redesign
described below, has so far only been run in the simulator.

## What's in it

**Shell** (`source/`)
- Start-up screen with the XenithOS chime, which is synthesized in code like every other sound
- Home: your profile and avatar at the top, five tiles (Apps, Store, Cloud, Profile, Settings), and a row of
  installed apps with thumbnails
- Apps: a thumbnail grid of everything on the USB stick
- Store: installs apps, game data and system updates from your server. Each download's SHA-256 is checked
  before anything is written.
- Cloud: live server stats, an inbox you can message from anywhere, and the VOLT leaderboard with avatars
- Profile: create a profile with a gamertag and passcode, build an avatar from nine parts, sign in on any console
- Settings: volume, interface sounds, start-up chime, network, system info, controller test, about

**Apps** (`apps/`)
- VOLT: a light-trail game. Scores go to your profile on the leaderboard.
- DOOM: [doomgeneric](https://github.com/ozkl/doomgeneric) with controller support. Get Freedoom from the Store,
  or copy your own `doom1.wad` / `doom2.wad` into a `doom/` folder on USB.

| DOOM control | |
|---|---|
| Left stick | move / strafe |
| Right stick, D-pad | turn |
| RT / LT | fire / run |
| A / B | use / menu back |
| X / Y | automap / "yes" in prompts |
| LB / RB | previous / next weapon |
| Start | menu |
| Hold Back | quit to XenithOS |

## Running it on a console

You need a console that can reach **XeLL**:

| Route | Soldering | Notes |
|---|---|---|
| Software exploit: [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) + [FreeMyXe](https://github.com/FreeMyXe/FreeMyXe), then "Launch XeLL instead" | None | Dashboard 17559, offline, hard drive attached (avatar data). Rerun after every power off. |
| RGH 3.0 | 2 wires | Boots XeLL every time |

USB stick layout (FAT32), as `./build.sh` stages it in `usb/`:

```
xenon.elf                 XenithOS (XeLL loads this)
apps/volt.elf32           apps, each with an optional .ximg thumbnail and .txt name
xenith/config.txt         server address and console key
xenith/settings.txt       written by XenithOS
xenith/profile.txt        written by XenithOS (keep it private: it signs you in)
data/                     game data from the Store
doom/                     DOOM config and saves
```

Networking only works over the Ethernet port, because libxenon has no Wi-Fi driver. With the software
exploit, plug the cable in after XenithOS has booted, since the exploit has to run offline.

**Warning from FreeMyXe:** never flash a modified NAND or run anything that writes to the NAND while using
it, or you will brick the console. XenithOS only writes to the USB stick.

### `xenith/config.txt`

```
server=203.0.113.10:8795    # your XenithOS Cloud server, IP:port (the console has no DNS)
name=PLAYER                 # leaderboard name for guests: letters and digits, up to 12
key=...                     # console key: matches XENITH_CONSOLE_KEY so only your Xbox reads its inbox
```

`build.sh` copies `config.local.txt` (git-ignored, so your server address stays private) to the USB layout.

## XenithOS Cloud (the server)

`server/xenith_cloud.py` is one Python 3 file with no dependencies. It serves the Store, profiles, the inbox,
leaderboards, live stats and the website (`server/web/`) over plain HTTP, because the console has no TLS.

```bash
python3 server/xenith_cloud.py --selftest      # 58 checks, no network needed
```

Profiles: passcodes are stored as salted PBKDF2 hashes and sign-in tokens as SHA-256 hashes, so the state
file never holds either. Five wrong passcodes lock a profile for 15 minutes, and sign-ups and sign-ins are
rate limited per address. Because the console speaks plain HTTP, don't reuse a passcode from anywhere else.

Install on a Linux server (the unit runs as a throwaway user on a read-only system):

```bash
mkdir -p /opt/xenith-cloud/store /opt/xenith-cloud/web
cp server/xenith_cloud.py /opt/xenith-cloud/
cp -r server/web/. /opt/xenith-cloud/web/
cp server/xenith-cloud.service /etc/systemd/system/
install -m 0755 server/xenith /usr/local/bin/xenith
umask 077
echo "XENITH_ADMIN_KEY=$(openssl rand -hex 32)" > /etc/xenith-cloud.env
echo "XENITH_CONSOLE_KEY=$(openssl rand -hex 24)" >> /etc/xenith-cloud.env   # also goes in config.txt as key=
# store files: the .elf32/.wad files, art/thumbs/*.png and *.ximg, and a store.json (see server/store.example.json)
systemctl daemon-reload && systemctl enable --now xenith-cloud
ufw allow 8795/tcp
```

From the server: `xenith send "Dinner is ready"` (inbox), `xenith motd "Game night at 9"` (banner), `xenith status`.

## Building

XenithOS builds with the libxenon toolchain on Linux (WSL works fine).

```bash
# one time: the toolchain in ~/xenon
git clone https://github.com/Free60Project/libxenon
cd libxenon/toolchain
PREFIX=$HOME/xenon ./build-xenon-toolchain toolchain
PREFIX=$HOME/xenon ./build-xenon-toolchain libxenon
PREFIX=$HOME/xenon ./build-xenon-toolchain filesystems
PREFIX=$HOME/xenon ./build-xenon-toolchain zlib

# every time
cd XenithOS
./build.sh
```

Fonts and thumbnails are generated and committed. To rebuild them: `python3 tools/mkfont.py` and
`python3 tools/mkart.py` (needs Pillow and the Inter font: `apt install python3-pil fonts-inter`).

## The simulator

`sim/` builds the same shell code for Linux, with AddressSanitizer on. It runs on virtual time with scripted
controller input, saves screenshots as PNG, and records everything the mixer plays to `audio.wav`. The website's
screenshots come from it. See [sim/README.md](sim/README.md).

```bash
sim/run.sh sim/scripts/tour.txt tour          # offline screens -> sim/out/tour/
```

## Layout

```
source/                  the shell: main.c (boot, home), screen_*.c, keyboard.c, avatar_editor.c, apps.c, cloud_api.c
shared/platform.h        everything XenithOS needs from the machine
shared/platform_xenon.c  ...on a real Xbox 360 (sim/platform_sim.c is the PC version)
shared/gfx.c             back buffer, anti-aliased shapes, text, images, fast copy to the tiled framebuffer
shared/audio.c           synthesized sounds and the mixer
shared/ui.c              input, frame pacing, headers, button hints, loading screens, toasts, dialogs
shared/avatar.c          the avatar renderer (server/web/static/avatar.js is the website copy)
shared/icons.c           icons drawn from shapes
shared/net_*.c           HTTP client: lwIP on the console, sockets in the simulator
shared/font_data.c       Inter, rendered by tools/mkfont.py
apps/volt, apps/doom     the apps
server/                  XenithOS Cloud, its systemd unit, the admin helper, the website
tools/                   font and thumbnail generators
```

## Writing an app

Copy `apps/volt`, rename the folder, and edit `source/`. Use `platform.h`, `gfx.h`, `ui.h` and `theme.h` so it
looks and sounds like the rest of the system, and call `plat_launch(XENITH_SHELL_PATH)` to go back. `build.sh`
picks up every folder in `apps/`; add `app.txt` (name, version) and `art/thumbs/<name>.ximg` for its tile.

## License

XenithOS is licensed under the **GNU GPL v3.0**, see [LICENSE](LICENSE).

- libxenon is BSD licensed.
- The Makefiles are adapted from devkitxenon's example Makefiles, which are GPL (derived from devkitPro).
- `shared/font_data.c` is rendered from the [Inter](https://rsms.me/inter/) typeface by Rasmus Andersson,
  under the SIL Open Font License 1.1 ([third_party/inter/OFL.txt](third_party/inter/OFL.txt)).
- `apps/doom/doomgeneric/` is [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL v2 or later, see its
  `LICENSE`; upstream commit in `UPSTREAM_COMMIT`). Changed: `m_config.c` stores config and saves in `uda:/doom/`.
- Freedoom (served by the Store, not included here) is BSD licensed, see [freedoom.github.io](https://freedoom.github.io).

Not affiliated with or endorsed by Microsoft or id Software. Xbox and Xbox 360 are trademarks of Microsoft.
DOOM is a trademark of id Software.
