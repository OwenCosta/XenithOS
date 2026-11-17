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
  The stand-in answers server list pings on its port (on `server-ip`, or 127.0.0.1), and `sim/hostmc/ping.py`
  sends one. With Paper's `proxies.proxy-protocol` on in `config/paper-global.yml` it wants a PROXY header
  first, as Paper does, and notes who each connection was from in `connections.log`.
- `sim/hostmc/test-bedrock.sh` runs HostMC's Bedrock players (Geyser and Floodgate) through the
  `hostmc_bedrock*.txt` scripts against a stand-in for GeyserMC's download service (`sim/hostmc/geysermc.py`,
  through `HOSTMC_GEYSER_API`), with the stand-in server acting as Paper (it loads the two plugins and takes
  Floodgate's `fwhitelist`), and checks the downloads, their checksums, a refused update and turning it off;
  screenshots go to `sim/out/hostmc-bedrock`.
- `XSIM_DESKTOP=1` runs the program as a desktop app, as XenithOS Desktop's Apps menu opens it
  (`plat_desktop()`): the script drives a mouse and the keyboard's own keys too (below), and
  `XSIM_CLIPBOARD` is what's been copied when it starts (what the program copies is said on stderr, as
  `sim: copied: ...`). Use `XSIM_W=1280 XSIM_H=720`, the picture's real size, so clicks land where they
  would on a PC. `sim/scripts/hostmc_desktop.txt` is HostMC's desktop version: run it like `hostmc.txt`,
  with `XSIM_DESKTOP=1 XSIM_W=1280 XSIM_H=720 XSIM_CLIPBOARD=vps.example.com` added.
- `sim/hostmc/test-vps.sh` runs HostMC's own-VPS route through the `hostmc_vps*.txt` scripts, with a private
  sshd (run as you, on spare ports) standing in for the VPS, and checks each step; screenshots go to
  `sim/out/hostmc-vps`.
- `sim/hostmc/test-relay.sh` runs HostMC's supporter relay through the `hostmc_relay*.txt` scripts against a
  local XenithOS Cloud, a stand-in for OCAccounts (`sim/hostmc/ocaccounts.py`) and the relay's own software
  (`server/mcrelay`, with its sshd run as you on a spare port), and checks each step; screenshots go to
  `sim/out/hostmc-relay`. HostMC runs in a user namespace with a hosts file of its own (`unshare`,
  `nsenter`), so its check asks by name. `ping.py` takes a third argument, the name a player typed, and a
  fourth, the address to connect from (another loopback address stands for a player elsewhere, so the
  server can tell players' addresses apart: `hostmc_relay_paper.txt` is that part).

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

With `XSIM_DESKTOP=1`, for desktop apps (each takes a frame, then one with nothing pressed):

| Command | |
|---|---|
| `mouse X Y` | the pointer moves to X, Y in the picture |
| `click X Y`, `rclick X Y` | it moves there and the left or right button is clicked |
| `wheel N` | the wheel turns N clicks (positive is away from you, up the page) |
| `key NAME`, `key ctrl+NAME` | a key of the keyboard's own (`plat_key_event`): a letter or digit, `tab`, `enter`, `esc`, `backspace`, `delete`, `home`, `end`, `pageup`, `pagedown`, the arrows, `f1` to `f12` |
| `type TEXT`, `type "TEXT"` | typed into the text field that's open (`\|` is Enter); quotes keep spaces at the ends |

`key` is only the keyboard's own key: what a key also does as the controller (Enter is A, Esc is B, the
arrows are the D-pad) is a `press`.
