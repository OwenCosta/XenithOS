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
