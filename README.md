# XenithOS

XenithOS is a homebrew console system. It runs on an Xbox 360 in place of the Microsoft dashboard, and on PCs
and Raspberry Pis as a console-style system you can switch to a normal Linux desktop. It has its own home
screen, profiles with avatars, a store, YouTube, photos, a DOOM port with sound, and a light-trail game. On PCs
its Store also installs apps from Flathub (Roblox through Sober, Minecraft, Kodi, emulators and more) and Steam
with its games. It's written in C: on the Xbox on [libxenon](https://github.com/Free60Project/libxenon), on PCs
and Pis on SDL2.

XenithOS Cloud is the free online service every copy uses for profiles, friends, cloud saves, achievements,
the Store, news, leaderboards and YouTube. Nothing to set up: the downloads come with its address.

I'm building it on a 360 S (Trinity board). The 0.4 redesign runs on the real console. 0.5.0 (sound fixes, faster
app starts, PC and Pi support, Tube, the new avatar editor) has so far been tested in the simulator and on Linux.

## What's in it

**System** (`source/`)
- Start-up screen with the XenithOS chime, which is synthesized in code like every other sound
- Home: your profile and avatar at the top, seven tiles (Apps, Store, Cloud, Friends, Profile, Settings,
  Feedback), and a row of installed apps with thumbnails
- Apps: a thumbnail grid of everything installed
- Store: apps, game data and system updates from XenithOS Cloud. Its lists come signed by XenithOS Cloud, and
  each download's SHA-256 is checked against them before anything is written. Pick several with X and START
  downloads them one after another. On PCs it has sections: apps from Flathub, Steam and games on Steam, and
  XenithOS.
- Cloud: your friends who are on, XenithOS news, and the VOLT leaderboard with avatars
- Friends: ask someone by their gamertag and they say yes on their own console. Friends see who's on and what
  they're playing (also as a notification when one comes on), and the address of a friend's HostMC server to
  join. "Show friends what I'm playing" in Settings keeps that to yourself.
- Profile: a gamertag and passcode, saved to XenithOS Cloud so it follows you to any machine
- Cloud saves: DOOM's saves and settings, and XenithOS's own (interface sounds, the start-up chime), follow
  your profile between the Xbox and your PCs. Each profile has its own folder on a machine, so people
  sharing a console never mix saves. If a save changed on two machines before they matched up, XenithOS
  asks which one to keep; nothing is replaced without asking. Profile, Cloud saves shows what's kept (up to
  4 MB) and deletes it; Settings turns them off for one machine.
- Achievements: fifteen across VOLT, DOOM, Tube and HostMC, earned on the machine (offline too: they wait on
  the stick until XenithOS is online) and kept on your profile. Profile shows yours, the Cloud screen the ones
  just earned by anyone, and the website everyone's.
- Avatar editor: thirteen parts in three tabs (face, hair, outfit), colour swatches, random looks
- Settings: volume, interface sounds, start-up chime, showing friends what you play, cloud saves on this
  machine, crash reports, theme, accessibility, Bluetooth controllers (PCs and Pis: pair one with the
  controller alone), network, system info (with start-up times and sound stats), controller test, about
- Crash reports: when XenithOS or one of its apps crashes, where it stopped is saved, and when XenithOS is back
  it asks to send a crash report to the people who make it (or sends it by itself, if Settings says to). No
  gamertag goes with one unless you add it, and nothing from your home folder.
- Themes: six colour themes for XenithOS and its apps (XenithOS's red, Ocean, Forest, Ember, Violet, Teal) and
  six wallpapers for the home screen, all flat and dark and drawn in code, picked in Settings > Theme
- Accessibility, for XenithOS and every app: bigger text, high contrast, and the controller's buttons moved to
  other jobs (any button can do another one's, the hints show the button that does each job) or the sticks
  swapped. The D-pad always moves around, so a layout can't lock you out.

**Apps** (`apps/`)
- VOLT: a light-trail game. Scores go to your profile on the leaderboard.
- DOOM: [doomgeneric](https://github.com/ozkl/doomgeneric) with controller support and sound effects. Get
  Freedoom from the Store, or copy your own `doom1.wad` / `doom2.wad` into a `doom/` folder. Signed in, its
  saves and settings are your profile's (`doom/profiles/<gamertag>/`) and follow you as cloud saves.
- Tube: search and watch YouTube. XenithOS Cloud turns the video into small JPEG frames and plain sound that
  even the Xbox can play. Needs a XenithOS profile.
- Photos: your pictures (JPEG, PNG, BMP, GIF) from a `photos` folder on the USB stick, or `~/Pictures` on a
  PC, as a grid of thumbnails. Full screen with zoom, turning and slideshows.
- Music: your own songs (MP3, OGG, FLAC, WAV) from a `music` folder on the USB stick, or `~/Music` on a PC, by
  album (with covers from a cover.jpg or inside the songs), artist or song. Shuffle, repeat, and it keeps
  playing while you look around. See the table below.
- HostMC (PCs and Pis): a Minecraft: Java Edition server for you and your friends, Minecraft's own or Paper.
  Start and stop it, see who's on, type commands, change its settings, back the world up, and with
  [playit.gg](https://playit.gg) turned on, let friends join from anywhere. It keeps running after you leave
  HostMC, so you can play on it. Your friends on XenithOS see its address in Friends while it's reachable.
  See [linux/README.md](linux/README.md#hostmc-a-minecraft-server).

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

| Music control | |
|---|---|
| A | open an album or artist, play a song |
| LB / RB | Albums, Artists, Songs (in Now Playing: previous / next song) |
| X | pause or play, from anywhere (in Now Playing: shuffle) |
| Y | Now Playing (in Now Playing: repeat off, all, one) |
| Left / Right | in Now Playing: back or on 10 seconds |
| B | back |

Music reads each song's tags (ID3, Vorbis comments, WAV's INFO) once and keeps them in
`xenith/music-library.txt`, so it starts quickly next time. Folders can go four deep (`music/Artist/Album/`).

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

The Store on a PC also has apps from Flathub and Steam. They install through XenithOS's own screens, start from
its home screen and open full screen over XenithOS (console mode runs XenithOS on its own X server, which it
manages itself); hold Guide for three seconds to come back. Games installed in Steam show up on the home screen too. Roblox comes through Sober, a
community project that isn't made by Roblox, and only runs on x86-64 PCs; Steam needs a Steam account.

## XenithOS Cloud (the server)

`server/xenith_cloud.py` is one Python 3 file with no dependencies. It serves the Store, profiles, news,
leaderboards, the downloads and the website (`server/web/`) over plain HTTP, because the Xbox has no TLS. Tube
also needs `yt-dlp` and `ffmpeg` on the server; it switches itself off without them.

PCs and Raspberry Pis use HTTPS when XenithOS Cloud has an HTTPS address too (`XENITH_PUBLIC_HTTPS`, such as
a Cloudflare tunnel's hostname whose service is `XENITH_EDGE_PORT`, where each console's address comes from
`CF-Connecting-IP`). It tells them at `/v1/lan`, and they move there once it answers with a certificate that
checks out, at each start; see [linux/README.md](linux/README.md#xenithos-cloud-over-https).

YouTube is hard on server addresses, so Tube asks for a video at most twice. First it tries the quick way, without
JavaScript, which takes a few seconds. If YouTube refuses, it tries the full way: Deno solves YouTube's
JavaScript challenges, and a local token helper (`server/xenith-pot.service`, the
[bgutil PO token provider](https://github.com/Brainicism/bgutil-ytdlp-pot-provider)) gives the mobile web
client a PO token. Starting a video answers at once, and the console waits on the chunk requests. A video's
links are kept for an hour, so skipping ahead doesn't ask YouTube again. `server/setup-tube.sh` installs or
updates all of it (ffmpeg, Deno, the yt-dlp standalone build, the plugin and the token helper), checking each
download against its published checksum.

```bash
python3 server/xenith_cloud.py --selftest      # 238 checks, no network needed
```

Profiles: passcodes are stored as salted PBKDF2 hashes and sign-in tokens as SHA-256 hashes, so the state
file never holds either. Five wrong passcodes lock a profile for 15 minutes, and sign-ups and sign-ins are
rate limited per address (an IPv6 address by its /64). Because the Xbox speaks plain HTTP, don't reuse a
passcode from anywhere else. Tube needs a signed-in profile, plays one video per profile and a few at a time
overall, so the server can't be used as an open proxy.

The Store is signed. XenithOS Cloud signs every Store list, the Store bundle and each thumbnail with its own
ed25519 key, in an `X-Xenith-Sig` header older consoles never read, and XenithOS checks it before it shows the
Store, so nobody between a console and the server can swap a file along with the SHA-256 it's checked against.
The key is a file of its own (`XENITH_STORE_KEY`), made once with `xenith_cloud.py store-key new <file>` and
kept out of every folder the server serves; its public half is built into XenithOS (`source/store_key.h`).
`xenith_cloud.py store-key check <host:port>` checks a running server's signatures against it. A XenithOS
Cloud of your own needs a key of its own and XenithOS built with its public half; a build with no key in
`store_key.h` doesn't check, as older versions didn't. A XenithOS update older than the version running is
never installed, so an old signed list played back can't take a console back to a version without the check.

Crash reports (`POST /v1/crash`) are kept like feedback, with each report in `crashes/<id>.txt` next to
`state.json`, and the first of each kind a day goes to Discord (`crash=<webhook>` in `feedback-webhooks.txt`,
or the bug reports' webhook). Read them with the admin key:
`curl -H "X-Xenith-Key: $XENITH_ADMIN_KEY" http://<server>/v1/crash` lists them, `/v1/crash/<id>` is one. On a
PC a report names the function and source line it stopped at. An Xbox report has addresses: `xenon-addr2line -f
-e xenith.elf <address>` (or the app's `.elf`) with that release's unstripped build turns them into names.

Friends: requests only become friends once the other person says yes, a "no" stops the same person asking
again for a week, and requests are rate limited per profile. Consoles say hello with what's running when
they start something (and every minute or so on the home screen); only friends see it. What someone plays is
kept in memory, never in the state file, and with "Show friends what I'm playing" off it isn't even sent. A
HostMC server's address is shown to friends while HostMC says people on the internet can reach it, and for a
day after.

Cloud saves: each file is kept as it came, in `saves/` in the state folder (in folders named by a random id,
not the gamertag), up to 512 KB a file and 4 MB a profile. Every file has a revision: a machine says which one
its copy was last in step with, and XenithOS Cloud refuses a change based on an older one instead of
replacing what another machine sent, so XenithOS can ask which to keep. Deleting leaves a marker, so the
machines that still have a copy keep it without sending it back.

Achievements: the set is in `ACHIEVEMENTS` (the console's notifications name them from `shared/social.c`, and
the self-test checks the two agree). They're public like the leaderboards: each profile's, with when it was
earned, and the newest fifty anyone earned.

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
# the Store's signing key: put the public half it prints in source/store_key.h, and give the
# service the file with `systemctl edit xenith-cloud`: LoadCredential=store-key:/etc/xenith-store.key
# and Environment=XENITH_STORE_KEY=%d/store-key under [Service]
python3 /opt/xenith-cloud/xenith_cloud.py store-key new /etc/xenith-store.key
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
`python3 tools/mkart.py` (needs Pillow and the Inter font: `apt install python3-pil fonts-inter`). The fonts
come in two sets, the second for Settings > Accessibility > Bigger text.

## The simulator

`sim/` builds the same code for Linux, with AddressSanitizer on. It runs on virtual time with scripted
controller input, saves screenshots as PNG, and records everything the mixer plays to `audio.wav`. It can also
check the Xbox's tiled framebuffer code, run the sound mixer on a real thread, and act like the PC version. See
[sim/README.md](sim/README.md).

```bash
sim/run.sh sim/scripts/tour.txt tour          # offline screens -> sim/out/tour/
```

## Checks

GitHub runs these on every push to a branch and every pull request (`.github/workflows/ci.yml`), and they work
the same on a PC:

```bash
make -C linux                                 # the PC version, as Ubuntu 24.04 and Debian 12 build it
sim/test-scripts.sh                           # every simulator script: ASan, UBSan, the tiled framebuffer
python3 server/xenith_cloud.py --selftest     # and server/xenith-relay.py --selftest
linux/test-hostmc.sh                          # HostMC on a PC, in a virtual display
sim/hostmc/test-vps.sh                        # HostMC's own-VPS route, with a private sshd
./build.sh                                    # the Xbox 360 build
```

Compiler warnings show on the commit or pull request without failing the build. Ubuntu's gcc builds with
`_FORTIFY_SOURCE=3`, so a buffer that's too small for a call like `realpath` stops the program in the HostMC
test as it would on a real PC. The Xbox toolchain is built once from the libxenon commit named in the workflow
and kept in GitHub's cache; a new commit there, or a week without any run, builds it again (about an hour).

## Layout

```
source/                  the system: main.c (boot, home), screen_*.c, avatar_editor.c, apps.c, cloud_api.c,
                         wallpaper.c (the home screen's wallpapers)
shared/theme.c           the colour themes; theme.h has the colours every screen draws with
shared/platform.h        everything XenithOS needs from the machine
shared/platform_xenon.c  ...on a real Xbox 360
linux/                   ...on PCs and Raspberry Pis (SDL2), plus the installer and session scripts
shared/gfx.c, tiled.c    back buffer, anti-aliased shapes, text, cached images, the Xbox's tiled framebuffer
shared/audio.c           synthesized sounds, the mixer (on its own CPU thread) and streamed sound
shared/ui.c, keyboard.c  input, frame pacing, headers, hints, loading screens, toasts, dialogs, keyboard
shared/avatar.c          the avatar renderer (server/web/static/avatar.js is the website copy)
shared/startlog.c        start-up timing for System info
shared/crash.h           crash reports: crash files (linux/crash_linux.c on PCs, crashdump() in
                         platform_xenon.c on the Xbox), sent from source/crash_report.c
shared/net_*.c           HTTP client: lwIP on the Xbox (linux/net_posix.c and sim/net_sim.c elsewhere)
source/store_sig.c       checks the Store's signatures (the key: store_key.h; ed25519: shared/monocypher*.c)
apps/volt, doom, tube,   the apps
  photos, music, hostmc  (hostmc is for PCs only: it has no Xbox Makefile)
linux/apps_linux.c       apps from Flathub (Flatpak) and Steam's library, on PCs
linux/kiosk.c            console mode's window manager: apps' windows over XenithOS on its own X server
shared/picture.c         JPEG, PNG, BMP and GIF through stb_image
shared/dr_mp3.h, dr_flac.h, stb_vorbis.h   Music's MP3, FLAC and Ogg Vorbis decoders
server/                  XenithOS Cloud, its systemd unit, the admin helper, the website
tools/                   font and thumbnail generators
```

## Writing an app

Copy `apps/volt`, rename the folder, and edit `source/`. Use `platform.h`, `gfx.h`, `ui.h` and `theme.h` so it
looks and sounds like the rest of the system, and call `plat_launch(XENITH_SHELL_PATH)` to go back. Draw with
theme.h's colours and lay text out with `gfx_line_h()` and `gfx_str_w()` rather than fixed sizes, so the colour
themes, high contrast and bigger text work in it too; `ui_poll()` hands over the buttons as Accessibility has
moved them. `build.sh`
picks up every folder in `apps/` that has a Makefile; add `app.txt` (name, version) and `art/thumbs/<name>.ximg`
for its tile, a target in `linux/Makefile` for PCs, and its name to the lists in `linux/install.sh` and
`linux/update.sh`. An app for PCs only (like HostMC) leaves the Makefile out.

## License

XenithOS is licensed under the **GNU GPL v3.0**, see [LICENSE](LICENSE).

- libxenon is BSD licensed.
- The Makefiles are adapted from devkitxenon's example Makefiles, which are GPL (derived from devkitPro).
- `shared/font_data.c` is rendered from the [Inter](https://rsms.me/inter/) typeface by Rasmus Andersson,
  under the SIL Open Font License 1.1 ([third_party/inter/OFL.txt](third_party/inter/OFL.txt)).
- `apps/doom/doomgeneric/` is [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL v2 or later, see its
  `LICENSE`; upstream commit in `UPSTREAM_COMMIT`). Changed: `m_config.c` stores config and saves in `uda:/doom/`,
  `i_sound.c` doesn't use SDL_mixer, and `i_video.c` reports reads for the loading screen.
- `shared/stb_image.h` is [stb_image](https://github.com/nothings/stb) 2.32 by Sean Barrett (public domain or
  MIT, see the end of the file).
- Music's decoders, unchanged: `shared/dr_mp3.h` (0.7.5) and `shared/dr_flac.h` (0.13.5) from
  [dr_libs](https://github.com/mackron/dr_libs) by David Reid (public domain or MIT No Attribution; dr_mp3 is
  based on minimp3), and `shared/stb_vorbis.h`, which is `stb_vorbis.c` 1.24 from [stb](https://github.com/nothings/stb)
  by Sean Barrett (public domain or MIT; renamed so the builds don't compile it on its own). All three as of
  November 2026, see the end of each file.
- `shared/monocypher.c`, `monocypher-ed25519.c` and their headers are [Monocypher](https://monocypher.org)
  4.0.2 by Loup Vaillant, unchanged (BSD 2-Clause or CC0 1.0, [third_party/monocypher/LICENCE.md](third_party/monocypher/LICENCE.md)).
- Apps from Flathub and Steam aren't part of XenithOS: Flatpak downloads them from Flathub on your machine,
  under their own licenses. Sober is a VinegarHQ project, not affiliated with Roblox.
- Freedoom (served by the Store, not included here) is BSD licensed, see [freedoom.github.io](https://freedoom.github.io).
- What HostMC downloads isn't part of XenithOS either: the Minecraft server (Mojang's, under Minecraft's EULA,
  which you agree to in HostMC first), Paper, Eclipse Temurin and playit.gg's agent come from their makers to
  your machine, under their own licenses.

Not affiliated with or endorsed by Microsoft, Mojang, Google, YouTube, id Software, Valve, Roblox or playit.gg.
Xbox and Xbox 360 are trademarks of Microsoft, Minecraft of Mojang, YouTube of Google, DOOM of id Software,
Steam of Valve, and Roblox of Roblox Corporation.
