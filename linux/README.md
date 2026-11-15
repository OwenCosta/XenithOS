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

What it does: installs the packages it needs (SDL2, a compiler, and XFCE for desktop mode), builds XenithOS and
its apps into `/usr/lib/xenithos`, logs your user in by itself on the first console, and starts
`xenithos-session` from `~/.profile`. Machines that started on a graphical login screen are set to start in
text mode instead, which `--uninstall` undoes.

## Console mode and desktop mode

- Console mode is XenithOS full screen, drawn straight to the display with no desktop underneath.
- Desktop mode: XenithOS power menu, **Switch to desktop mode**.
- Back to XenithOS: **Return to XenithOS** in the desktop's application menu, or log out.

The mode is kept in `~/.config/xenithos/mode`. If XenithOS can't start (no usable graphics driver, say), the
session falls back to desktop mode, or a plain shell when there's no desktop. What it printed is in
`~/.config/xenithos/xenithos.log`. In desktop mode you can also run XenithOS in a window from the menu.

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
| F1 | Start |
| Page Up / Page Down, Ctrl | LT / RT |
| W A S D | left stick (in games) |

In the on-screen keyboard you can type on the real keyboard: letters type, Backspace deletes, Enter finishes.

## Where things are

| Path | What |
|---|---|
| `/usr/lib/xenithos/` | XenithOS and its apps |
| `~/.local/share/xenithos/uda:/` | your settings, profile, game data and saves (same layout as the Xbox's USB stick) |
| `~/.config/xenithos/` | the mode file and logs |

The Store only offers game data here: apps come with XenithOS on PCs and Pis.

## Building by hand

```bash
sudo apt install build-essential libsdl2-dev zlib1g-dev
make -C linux
linux/xenithos             # in a window, when run from a desktop
```

For testing without a screen: `SDL_VIDEODRIVER=offscreen XENITHOS_SHOT=home.png XENITHOS_SHOT_FRAME=200
linux/xenithos` saves frame 200 and quits. `XENITHOS_DATA` changes the data folder.

`linux/test-in-window.sh volt` (or `doom`) runs the whole thing in a virtual display (needs `xvfb xdotool
x11-apps imagemagick`): it boots XenithOS, starts the app, leaves it, and moves around the home screen,
taking screenshots through each switch and recording the sound to a file. It prints how bright every
screenshot is (0 would be a black screen) and how loud each quarter second was, so a black gap or missing
sound after coming back shows up straight away.

## Apps inside XenithOS

Apps are their own programs, but XenithOS starts each one with `XENITHOS_GUEST_FD` pointing at shared
memory: the app draws its frames and queues its sound there, and XenithOS shows them in its own window,
plays the sound and passes the controller and keyboard on. The screen and the sound device never change
hands, so switching doesn't go black. Hold Guide for two seconds, or press Ctrl+Alt+Backspace, to close an
app that has stopped responding. An app started on its own (outside XenithOS) opens its own window as before.
