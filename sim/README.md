# XenithOS simulator

The simulator compiles the real shell (and VOLT) for Linux with `sim/platform_sim.c` in place of the console.
Time is virtual, so animations are frame-exact and runs finish instantly. It's built with AddressSanitizer and
UBSan, so memory bugs show up here before they reach a console.

```bash
sim/run.sh <script> <name> [server]      # results in sim/out/<name>/
XSIM_BIN=xvolt sim/run.sh sim/scripts/volt.txt volt
```

`run.sh` makes a working folder under `~/.xsim/<name>/` with a directory literally named `uda:` standing in for
the USB stick (copied from `usb/`, so run `build.sh` first). Pass a server like `127.0.0.1:8795` to test the
online screens against a local `server/xenith_cloud.py`.

Environment: `XSIM_KEY` (console key for the inbox), `XSIM_PROFILE` (a profile.txt to boot signed in),
`XSIM_BIN` (`xsim` or `xvolt`), `XSIM_SETTINGS` (settings.txt to start with, its lines separated by spaces:
`XSIM_SETTINGS="theme=ocean wallpaper=peaks"`).

Checks for things that only exist on the console:

- `XSIM_TILED=1` also sends every frame through the console's tiled framebuffer code (`shared/tiled.c`)
  into a fake framebuffer, reads it back at every screenshot and at the end, and prints how many pixels came
  out wrong. It should always say 0.
- `XSIM_THREAD=1` runs the sound mixer on a real second thread, like the console does. The simulator then
  runs in real time, so a script takes as long as it would on the Xbox.

At the end the simulator prints the average and worst time a frame took to draw on your PC, and how many
times the sound ran dry ("dropouts"). Compare frame times before and after a change with the same script.

More switches:

- `XSIM_KIND=pc` acts like the PC and Raspberry Pi version (desktop mode in the power menu, game data only in
  the Store).
- `XSIM_TYPE="text"` types that text the first time the on-screen keyboard opens, as a real keyboard would
  (`|` is Enter).
- `XSIM_BIN=xdoom XSIM_WAD=path/to/freedoom1.wad` runs DOOM (`sim/scripts/doom.txt`), and `XSIM_BIN=xtube`
  runs Tube against a XenithOS Cloud with yt-dlp and ffmpeg (`sim/scripts/tube.txt`, use `XSIM_THREAD=1`).
- `XSIM_BIN=xhostmc XSIM_KIND=pc XSIM_THREAD=1 XSIM_HOSTMC=1` runs HostMC with a stand-in for the Minecraft
  server (`sim/hostmc/java`, which talks like one: starting up, players joining, answers to commands) and
  what it would have downloaded already in `hostmc/` (`sim/hostmc/data`), so nothing is downloaded
  (`sim/scripts/hostmc.txt`). It runs in real time, since the stand-in does. Without `XSIM_HOSTMC`, HostMC
  downloads Java and a real server and runs it. A server a script leaves running is stopped afterwards.
  The stand-in answers server list pings on its port (127.0.0.1 only), and `sim/hostmc/ping.py` sends one.
- `XSIM_BIN=xmusic XSIM_MUSIC=<folder>` runs Music with that folder as the stick's `music` folder
  (`sim/scripts/music.txt`). `sim/music/make-music.sh <folder>` makes a library to try it with, all synthesized
  here (MP3, FLAC, Ogg and WAV at different rates, tags, covers inside the songs and beside them, accented
  names, a compilation, a broken file); it needs ffmpeg with lame and vorbis. `sim/music/test-tones.sh` plays an
  album of four steady tones, one per format, and checks what was heard: each at its pitch, in order, with no
  gap or click between them.
- Bluetooth (`XSIM_KIND=pc`, Settings > Bluetooth controllers) runs the PC's own code
  (`linux/bluetooth_linux.c`) against a stand-in for BlueZ's `bluetoothctl` (`sim/bluetooth/bluetoothctl`),
  never this PC's Bluetooth: an 8BitDo controller already paired but asleep, an Xbox controller, a DualSense
  that turns the first pairing down, a TV and a device with no name. `XSIM_BT=none` has no adapter,
  `XSIM_BT=blocked` has Bluetooth switched off, and `XSIM_BLUETOOTHCTL=/nowhere` has no BlueZ. Use
  `XSIM_THREAD=1`, since the stand-in runs in real time (`sim/scripts/bluetooth.txt`, `bluetooth_off.txt`).
- `sim/hostmc/test-vps.sh` runs HostMC's own-VPS route through the `hostmc_vps*.txt` scripts, with a private
  sshd (run as you, on spare ports) standing in for the VPS, and checks each step; screenshots go to
  `sim/out/hostmc-vps`.

## Scripts

One command per line; each controller poll is one frame.

| Command | |
|---|---|
| `wait N` | N frames with nothing pressed |
| `press BUTTON [N]` | hold a button for N frames (default 2), then release for one |
| `hold BUTTON N` | same as press |
| `stick lx\|ly\|rx\|ry VALUE N` | push a stick for N frames |
| `shot NAME` | save the last frame as NAME.png |
| `quit` | stop and write audio.wav |

Buttons: `up down left right a b x y lb rb lt rt start back guide ls rs`.
