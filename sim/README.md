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
`XSIM_BIN` (`xsim` or `xvolt`).

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

## All the scripts at once

`sim/test-scripts.sh` runs every script with the switches its header gives it and `XSIM_TILED=1`, as CI does,
and fails on an AddressSanitizer or UBSan report, a tiled framebuffer check that isn't 0 wrong pixels, or a run
that doesn't finish. `sim/test-scripts.sh boot tour` runs only those.

- It needs no Xbox build: the USB stick is a stand-in for `usb/` (XenithOS and the apps as empty files, with
  their `app.txt` files and thumbnails). `XSIM_USB=usb` uses the real one; `sim/run.sh` takes `XSIM_USB` too.
- Scripts that go online get XenithOS Clouds of their own on spare ports: one with a stand-in Store (the items
  in `server/store.example.json`, at this version of XenithOS so no update is waiting, with stand-in files)
  and the Flathub and Steam lists in `sim/store/`, and one with only those lists, for `store_pc.txt` and
  `store_steam.txt`. It also checks what those scripts leave on the server: the profile `online.txt` makes,
  the report `feedback.txt` sends, and the home-network route `store_route.txt` takes.
- `doom.txt` needs a WAD (`XSIM_WAD=path/to/freedoom1.wad`), and Tube's script asks YouTube only where the
  machine has yt-dlp and ffmpeg (without them it shows Tube turned off). Scripts whose header names one of
  the `sim/hostmc/test-*.sh` tests are left to that test. A new script runs with no switches until it's
  given some in `sim/test-scripts.sh`.
- Screenshots and each run's log go to `sim/out/test-<name>/`. `XSIM_LIMIT` is how long a run may take
  (900 s).
