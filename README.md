# XENITH

**A custom operating shell for the Xbox 360**, running bare metal on the real hardware through
[LibXenon](https://github.com/Free60Project/libxenon). It boots from XeLL instead of the Xbox dashboard,
and it has its own interface, its own apps and its own look.

> **Status: early (v0.2).** It builds cleanly. First boot on real hardware (a 360 S, Trinity board) is in progress.

## What's in it

**XENITH shell** (`source/`)
- **Boot splash**: the XENITH logo fades in, an accent bar sweeps across, and the controller rumbles
- **Apps**: finds `.elf` / `.elf32` programs in the root or an `apps/` folder on USB, and launches them
- **System**: live CPU / GPU / memory / board temperatures, video output type, resolution
- **Controller**: every button lights up, both sticks and triggers are drawn live, and the triggers drive the rumble motors
- **Restart / Power off**

**Apps** (`apps/`)
- **VOLT**: steer a streak of light, collect sparks to grow, and it speeds up every 5 sparks. **Back** returns to XENITH.

Controls: stick or D-pad to move, **A** select, **B** back. **Guide** on the home screen exits to XeLL.

## Running it on a console

You need a console that can reach **XeLL**. There are two ways:

| Route | Soldering | Notes |
|---|---|---|
| **Software exploit**: [ABadAvatar](https://github.com/shutterbug2000/ABadAvatar) + [FreeMyXe](https://github.com/FreeMyXe/FreeMyXe), then FreeMyXe's *launch XeLL* option | None | Needs dashboard 17559, offline, and the hard drive attached (for avatar data). Not persistent: rerun it after every power off. |
| **RGH 3.0** | 2 wires | Boots XeLL every time |

USB stick layout (FAT32):

```
xenon.elf            <- XENITH (XeLL loads this automatically)
apps/volt.elf32      <- shows up in XENITH's Apps menu
```

`./build.sh` produces exactly this layout in `usb/`.

> ⚠️ FreeMyXe's own warning: never flash a modified NAND or run anything that writes to the NAND while using
> it, or you will brick the console. XENITH never writes to the NAND.

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
source/main.c        XENITH shell: splash, menus, screens
shared/gfx.c|h       drawing layer: rectangles, 5x7 pixel font, cache flush
shared/theme.h       the shared palette, so every app looks like part of the system
apps/volt/           VOLT game
build.sh             builds everything and stages usb/
```

## Writing an app

Copy `apps/volt`, rename the folder, then edit `source/`. Include `gfx.h` and `theme.h` to match the system look,
and call `elf_runFromDisk(XENITH_SHELL_PATH)` to return to XENITH. `build.sh` picks up every folder in `apps/`.

## Roadmap

- Boot sound through `xenon_sound`
- Settings saved to USB (accent color, boot animation on/off)
- App manifests with real names and icons
- Read the Xbox hard drive (XTAF) for apps, not just USB
- Network screen (LibXenon ships lwIP) to push new builds over LAN

## License

XENITH is licensed under the **GNU GPL v3.0**, see [LICENSE](LICENSE).

- LibXenon is BSD licensed.
- The Makefiles are adapted from devkitxenon's example Makefiles, which are GPL (derived from devkitPro).

Not affiliated with or endorsed by Microsoft. Xbox and Xbox 360 are trademarks of Microsoft.
