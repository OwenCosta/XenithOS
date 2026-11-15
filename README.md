# XenithOS

XenithOS is a homebrew console system. It runs on an Xbox 360 in place of the Microsoft dashboard, and on PCs
and Raspberry Pis as a console-style system you can switch to a normal Linux desktop. It has its own home
screen, profiles with avatars, a store, YouTube, a DOOM port with sound, and a light-trail game. It's written in
C: on the Xbox on [libxenon](https://github.com/Free60Project/libxenon), on PCs and Pis on SDL2.

XenithOS Cloud is the free online service every copy uses for profiles, the Store, news, leaderboards and
YouTube. Nothing to set up: the downloads come with its address.

I'm building it on a 360 S (Trinity board). The 0.4 redesign runs on the real console. 0.5.0 (sound fixes, faster
app starts, PC and Pi support, Tube, the new avatar editor) has so far been tested in the simulator and on Linux.

## What's in it

**System** (`source/`)
- Start-up screen with the XenithOS chime, which is synthesized in code like every other sound
- Home: your profile and avatar at the top, five tiles (Apps, Store, Cloud, Profile, Settings), and a row of
  installed apps with thumbnails
- Apps: a thumbnail grid of everything installed
- Store: apps, game data and system updates from XenithOS Cloud. Each download's SHA-256 is checked before
  anything is written.
- Cloud: who's playing, XenithOS news, and the VOLT leaderboard with avatars
- Profile: a gamertag and passcode, saved to XenithOS Cloud so it follows you to any machine
- Avatar editor: thirteen parts in three tabs (face, hair, outfit), colour swatches, random looks
- Settings: volume, interface sounds, start-up chime, network, system info (with start-up times and sound
  stats), controller test, about

**Apps** (`apps/`)
- VOLT: a light-trail game. Scores go to your profile on the leaderboard.
- DOOM: [doomgeneric](https://github.com/ozkl/doomgeneric) with controller support and sound effects. Get
  Freedoom from the Store, or copy your own `doom1.wad` / `doom2.wad` into a `doom/` folder.
- Tube: search and watch YouTube. XenithOS Cloud turns the video into small JPEG frames and plain sound that
  even the Xbox can play. Needs a XenithOS profile.

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

## On an Xbox 360

You need a console that can reach **XeLL**:

| Route | Soldering | Notes |
|---|---|---|
| Software exploit: [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) + [FreeMyXe](https://github.com/FreeMyXe/FreeMyXe), then "Launch XeLL instead" | None | Dashboard 17559, offline, hard drive attached (avatar data). Rerun after every power off. |
| RGH 3.0 | 2 wires | Boots XeLL every time |

Download `xenithos-xbox360.zip` from the XenithOS website and copy it to a FAT32 USB stick. It holds:

```
xenon.elf                 XenithOS (XeLL loads this)
apps/volt.elf32           apps, each with a .ximg thumbnail and a .txt name
xenith/config.txt         the XenithOS Cloud address
xenith/settings.txt       written by XenithOS
xenith/profile.txt        written by XenithOS (keep it private: it signs you in)
data/                     game data from the Store
doom/                     DOOM config and saves
```

Networking only works over the Ethernet port, because libxenon has no Wi-Fi driver. With the software
exploit, plug the cable in after XenithOS has booted, since the exploit has to run offline. Once XenithOS is
running, updates come from the Store, so you don't have to run the exploit again just to update.

**Warning from FreeMyXe:** never flash a modified NAND or run anything that writes to the NAND while using
it, or you will brick the console. XenithOS only writes to the USB stick.

## On a PC or Raspberry Pi

Install Debian 12 or newer, Ubuntu 22.04 or newer, or Raspberry Pi OS (Lite is fine), then run the installer
from the XenithOS website:

```bash
curl -fsSL http://<XenithOS Cloud address>/install.sh | sudo bash
```

It builds XenithOS, sets the machine to start straight into it, and installs a desktop for desktop mode. After
a restart you're in **console mode**: XenithOS full screen, with a controller or keyboard. **Desktop mode** (the
XFCE Linux desktop) is in XenithOS's power menu; "Return to XenithOS" in the desktop's menu goes back. Details,
keyboard controls and how to undo it are in [linux/README.md](linux/README.md).

## XenithOS Cloud (the server)

`server/xenith_cloud.py` is one Python 3 file with no dependencies. It serves the Store, profiles, news,
leaderboards, the downloads and the website (`server/web/`) over plain HTTP, because the Xbox has no TLS. Tube
also needs `yt-dlp` and `ffmpeg` on the server; it switches itself off without them.

YouTube is hard on server addresses, so Tube asks for a video at most twice. First it tries the quick way, without
JavaScript, which takes a few seconds. If YouTube refuses, it tries the full way: Deno solves YouTube's
JavaScript challenges, and a local token helper (`server/xenith-pot.service`, the
[bgutil PO token provider](https://github.com/Brainicism/bgutil-ytdlp-pot-provider)) gives the mobile web
client a PO token. Starting a video answers at once, and the console waits on the chunk requests. A video's
links are kept for an hour, so skipping ahead doesn't ask YouTube again. `server/setup-tube.sh` installs or
updates all of it (ffmpeg, Deno, the yt-dlp standalone build, the plugin and the token helper), checking each
download against its published checksum.

```bash
python3 server/xenith_cloud.py --selftest      # 90 checks, no network needed
```

Profiles: passcodes are stored as salted PBKDF2 hashes and sign-in tokens as SHA-256 hashes, so the state
file never holds either. Five wrong passcodes lock a profile for 15 minutes, and sign-ups and sign-ins are
rate limited per address. Because the Xbox speaks plain HTTP, don't reuse a passcode from anywhere else. Tube
needs a signed-in profile, plays one video per profile and a few at a time overall, so the server can't be used
as an open proxy.

Running your own copy (the unit runs as a throwaway user on a read-only system):

```bash
mkdir -p /opt/xenith-cloud/store /opt/xenith-cloud/web /opt/xenith-cloud/downloads
cp server/xenith_cloud.py /opt/xenith-cloud/
cp -r server/web/. /opt/xenith-cloud/web/
cp linux/install.sh /opt/xenith-cloud/downloads/
cp server/xenith-cloud.service /etc/systemd/system/
install -m 0755 server/xenith /usr/local/bin/xenith
umask 077
echo "XENITH_ADMIN_KEY=$(openssl rand -hex 32)" > /etc/xenith-cloud.env
echo "XENITH_PUBLIC_ADDR=<this server's ip>:8795" >> /etc/xenith-cloud.env
# store files: the .elf32/.wad files, art/thumbs/*.png and *.ximg, and a store.json (see server/store.example.json)
systemctl daemon-reload && systemctl enable --now xenith-cloud
ufw allow 8795/tcp
# Tube: ffmpeg, Deno, yt-dlp and the PO token helper (run it again later to update them)
cp server/setup-tube.sh server/xenith-pot.service /opt/xenith-cloud/
bash /opt/xenith-cloud/setup-tube.sh
```

From the server: `xenith send "XenithOS 0.5 is out"` (news), `xenith motd "Welcome"` (banner), `xenith status`,
`xenith tube-update` (newest yt-dlp, Deno and token helper, when YouTube changes something).

## Building

The Xbox version builds with the libxenon toolchain on Linux (WSL works fine).

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
./build.sh                  # stages a USB stick layout in usb/
```

The PC and Pi version: `make -C linux` (needs `libsdl2-dev` and `zlib1g-dev`).

Fonts and thumbnails are generated and committed. To rebuild them: `python3 tools/mkfont.py` and
`python3 tools/mkart.py` (needs Pillow and the Inter font: `apt install python3-pil fonts-inter`).

## The simulator

`sim/` builds the same code for Linux, with AddressSanitizer on. It runs on virtual time with scripted
controller input, saves screenshots as PNG, and records everything the mixer plays to `audio.wav`. It can also
check the Xbox's tiled framebuffer code, run the sound mixer on a real thread, and act like the PC version. See
[sim/README.md](sim/README.md).

```bash
sim/run.sh sim/scripts/tour.txt tour          # offline screens -> sim/out/tour/
```

## Layout

```
source/                  the system: main.c (boot, home), screen_*.c, avatar_editor.c, apps.c, cloud_api.c
shared/platform.h        everything XenithOS needs from the machine
shared/platform_xenon.c  ...on a real Xbox 360
linux/                   ...on PCs and Raspberry Pis (SDL2), plus the installer and session scripts
shared/gfx.c, tiled.c    back buffer, anti-aliased shapes, text, cached images, the Xbox's tiled framebuffer
shared/audio.c           synthesized sounds, the mixer (on its own CPU thread) and streamed sound
shared/ui.c, keyboard.c  input, frame pacing, headers, hints, loading screens, toasts, dialogs, keyboard
shared/avatar.c          the avatar renderer (server/web/static/avatar.js is the website copy)
shared/startlog.c        start-up timing for System info
shared/net_*.c           HTTP client: lwIP on the Xbox (linux/net_posix.c and sim/net_sim.c elsewhere)
apps/volt, doom, tube    the apps
server/                  XenithOS Cloud, its systemd unit, the admin helper, the website
tools/                   font and thumbnail generators
```

## Writing an app

Copy `apps/volt`, rename the folder, and edit `source/`. Use `platform.h`, `gfx.h`, `ui.h` and `theme.h` so it
looks and sounds like the rest of the system, and call `plat_launch(XENITH_SHELL_PATH)` to go back. `build.sh`
picks up every folder in `apps/`; add `app.txt` (name, version) and `art/thumbs/<name>.ximg` for its tile, and
a target in `linux/Makefile` for PCs.

## License

XenithOS is licensed under the **GNU GPL v3.0**, see [LICENSE](LICENSE).

- libxenon is BSD licensed.
- The Makefiles are adapted from devkitxenon's example Makefiles, which are GPL (derived from devkitPro).
- `shared/font_data.c` is rendered from the [Inter](https://rsms.me/inter/) typeface by Rasmus Andersson,
  under the SIL Open Font License 1.1 ([third_party/inter/OFL.txt](third_party/inter/OFL.txt)).
- `apps/doom/doomgeneric/` is [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL v2 or later, see its
  `LICENSE`; upstream commit in `UPSTREAM_COMMIT`). Changed: `m_config.c` stores config and saves in `uda:/doom/`,
  `i_sound.c` doesn't use SDL_mixer, and `i_video.c` reports reads for the loading screen.
- `apps/tube/source/stb_image.h` is [stb_image](https://github.com/nothings/stb) 2.32 by Sean Barrett (public
  domain or MIT, see the end of the file).
- Freedoom (served by the Store, not included here) is BSD licensed, see [freedoom.github.io](https://freedoom.github.io).

Not affiliated with or endorsed by Microsoft, Google, YouTube or id Software. Xbox and Xbox 360 are trademarks
of Microsoft, YouTube of Google, and DOOM of id Software.
