# XenithOS Desktop

XenithOS as a whole operating system for PCs: an ISO that installs itself and puts XenithOS Desktop where
Windows or another OS was. The PC starts in a desktop with XenithOS's own look, and **XenithOS Console** (the
XenithOS you know from the Xbox 360) comes with it as an app. Underneath it's Ubuntu 24.04.

The XenithOS console download (for the Xbox 360) and `linux/install.sh` (XenithOS on top of a Linux you already
have) stay as they are; this is the third way in.

## Using it

1. Download `xenithos-desktop-<version>-amd64.iso` and put it on a USB stick of 4 GB or more:
   - On Windows: `powershell -ExecutionPolicy Bypass -File desktop\write-usb.ps1`. It lists the USB sticks,
     asks which one to erase, and names the stick **XENITHOS** so you can tell it from your others. Then
     it reads every file back from the stick and checks it against the ISO's own list. (If Windows won't
     let it make the stick that way, it writes the ISO byte for byte instead and says so.)
   - Anywhere: [balenaEtcher](https://etcher.balena.io), Rufus (choose "DD Image mode" if it asks), or
     `sudo dd if=xenithos-desktop-*.iso of=/dev/sdX bs=4M status=progress`.
2. Start the PC from the USB stick (its boot menu key is usually F12, F11, F8 or Esc). That menu is the PC's
   own and lists sticks by their maker's name ("PNY USB 2.0 FD"), whatever is on them.
3. The boot menu:
   - **Start XenithOS Desktop** tries it without changing the PC. The desktop has **Install XenithOS
     Desktop** on it.
   - **Install XenithOS Desktop** goes straight to the installer.
   - **More options**: safe graphics (for PCs whose screen stays black), checking the USB stick, and the
     PC's firmware settings.
4. The installer asks for your name, a user name, a password, the computer's name, the keyboard and the
   time zone, then for a drive. **It erases the whole drive it installs on**: it says so, and the "Erase and
   install" button stays off until you tick "I understand, erase this drive". The USB stick it started
   from, read-only drives and drives under 16 GB can't be picked.
5. When it's done, choose Restart and wait: the PC restarts by itself. Take the USB stick out when the
   screen goes dark.

A keyboard is needed to install (for the account).

## The desktop and XenithOS Console

The PC starts in the desktop. XenithOS Console is one click away and takes the whole screen, like a console:

- To XenithOS Console: **Return to XenithOS Console** on the desktop, the XenithOS logo at the left of the
  panel, the top of the Apps menu, or Super+Esc.
- Back to the desktop: XenithOS Console's power menu, **Switch to desktop mode**.
- Settings > Start-up chooses which of the two the PC starts in.

Both run in the same screen session, so switching shows XenithOS's wallpaper in between and never a text
console.

What comes with it:

- **Brave** as the browser, **Mullvad VPN**, Files, Terminal, a text editor, picture, PDF and video
  viewers, an archive tool, a calculator, screenshots, a task manager, Disks, and Bluetooth.
- **Security tools** from Ubuntu's archive, for networks, the web, passwords, Wi-Fi, forensics and privacy.
  `packages/security.list` is the whole list, and it's also how they're sorted in the Apps menu.
- **Settings**, XenithOS Desktop's own: Wi-Fi (join, forget, hidden networks), Sound (volume, output,
  input), Look (highlight colour, wallpaper, panel, window buttons, text size), Start-up, Console apps,
  Security (firewall, a different Wi-Fi address on every network), Updates and About.

### The Apps menu

The Apps menu is sorted XenithOS Desktop's own way: Internet, Media, Office, Games, Tools, Security Tools
and Settings (and Development, Learning and Other when something's in them). An app you add later lands in
the group its maker's categories point to.

**Security Tools** opens into smaller groups: Networks, Websites, Passwords, Wi-Fi, Remote access, People
and organisations, Programs and files, Protection and privacy. A tool with a window is there as itself. A
tool that's used by typing has an entry that opens a terminal on it, saying what it is (its package's own
one-line description), which commands it brings, and leaving you at a prompt. The groups and what's in
them come from `packages/security.list`: move a line, or add a `## Group` header, and the menu follows at
the next update.

### XenithOS Console's apps

Photos and Tube come on XenithOS Console's home screen. **HostMC, DOOM and VOLT don't**: Settings > Console
apps installs them (and removes them again; what you made with them stays). They're on the PC either way,
built with XenithOS Console, so installing one takes a moment and needs no download.
`rootfs/usr/share/xenithos-desktop/console-apps.list` says which apps wait to be installed.

The look is XenithOS's: a near-black wallpaper with the logo, one panel along the bottom, flat dark window
borders with a red close button, GTK's dark theme recoloured, Papirus's flat icons with grey folders, and the
Inter font (with Noto's fonts behind it for the world's other writing, Chinese, Japanese and Korean
included). The system calls itself XenithOS Desktop everywhere a program asks (the boot menu, the start-up
screen, `os-release`), and Ubuntu's own extras that would say otherwise (the Ubuntu Pro client and its
notes in apt) aren't in it.

## Updates

Settings > Updates has one button. It updates XenithOS Desktop itself, XenithOS Console, the apps (Brave,
Mullvad VPN and the rest) and the system underneath, and says when a restart is needed. A notice appears on
the desktop when updates are ready. No terminal, and no writing the USB stick again.

- XenithOS Desktop's own parts are four packages (`xenithos-archive`, `xenithos-desktop`,
  `xenithos-desktop-apps`, `xenithos-security-tools`). An update is a bundle of them, signed with XenithOS
  Desktop's key; the updater checks the signature before it installs anything, and apt does the installing.
- An update that's cut off (the power goes, say) is finished the next time the PC starts.
- Where updates come from is in `channel.txt` (built into every install). The addresses are tried in order.
- Ubuntu's security fixes also install themselves once a day.

Making a release's update:

```bash
bash desktop/release.sh --notes "What's new, in a line"
```

It leaves `desktop/out/channel/` (`manifest.txt`, the bundle, their signatures and `get-update.sh`) to put at
the addresses in `channel.txt`. The signing key lives outside the repository, in
`~/.xenithos/desktop-signing-key.asc`. Keep a copy of it somewhere safe: without it, installed PCs can't be
sent updates any more.

### XenithOS Desktop 1 and 2

XenithOS Desktop 2 is built on Ubuntu 24.04. XenithOS Desktop 1 (versions 0.6.1 to 1.1.2) was built on
Debian 13, and a PC that has it can't be turned into 2 by an update: 2 is installed fresh, from its ISO.
The two read different update addresses (1 reads the GitHub release `desktop-stable`, 2 reads
`desktop-2-stable`), and 2's update bundle refuses to install on anything but Ubuntu underneath, so they
can't mix. `out/snapshots/` holds the last Debian-based source tree (1.1.2) as it was before the move.

## On the drive

| What | Where |
|---|---|
| GPT: 1 MB BIOS boot, 512 MB EFI system, the rest ext4 (label `XenithOS`) | the whole drive |
| A 2 GB swap file | `/swapfile` |
| XenithOS Console and its apps, yours to update without sudo | `/usr/lib/xenithos` |
| Your XenithOS Console data | `~/.local/share/xenithos/uda:/` |
| XenithOS Desktop's settings for your account | `~/.config/xenithos/` |
| What the installer did, step by step | `/var/log/xenithos-install.log` |
| What every update did | `/var/log/xenithos-update.log` |

- It starts with UEFI or BIOS, whichever the PC used to start the USB stick, and works with Secure Boot on
  (Ubuntu's signed shim and GRUB). The PC's boot menu lists it as "XenithOS".
- No GRUB menu: it goes straight to XenithOS's start-up screen. Press Esc as the PC starts to see the menu.
  After a start that didn't finish (the power went while it was starting, say) the menu shows by itself
  for 3 seconds, in case another entry is needed.

## Tested

In VirtualBox 7.2, on the 2.0.0 ISO and the builds that led to it, with BIOS, UEFI and UEFI with Secure
Boot: the boot menu and its stick check, the live system (Brave included), installing, the PC restarting by
itself into the installed system, switching to XenithOS Console and back, Settings, the Apps menu's groups,
and the boot menu after a start that didn't finish. A checklist of about forty things passes on the
installed system with Secure Boot and with BIOS: its name and language, the wallpaper, sound, the network,
Brave, Mullvad VPN, AppArmor, GRUB and the PC's boot entry, the log, apt, the updater, the console apps and
the Apps menu among them. A stick laid out the way `write-usb.ps1` lays it out starts with Secure Boot and
with BIOS (as a disk image in a VM), and `write-usb.ps1` has made a real stick that way from Windows 11,
with every file read back from the stick and matching the ISO's list.

Updating from Settings > Updates was tested end to end on XenithOS Desktop 1 (the same updater). XenithOS
Desktop 2's own update address has nothing on it yet, so there the updater was only seen to look, find
nothing and carry on with the system's updates. Not tested: a real PC, Wi-Fi, and sound you can hear.

## Not yet

- English only (the keyboard and time zone can be anything).
- NVIDIA cards use the open nouveau driver: fine for the desktop and XenithOS Console, slower in 3D games.
  NVIDIA's own driver isn't included.
- It takes the whole drive: no installing next to Windows.
- PCs only (64-bit x86). On a Raspberry Pi, use Raspberry Pi OS and `linux/install.sh`.
- Security tools that aren't in Ubuntu 24.04's archive (Metasploit, Burp Suite, ZAP, Ghidra and a few more).

## Building the ISO

On Windows, macOS or Linux with Docker:

```bash
bash desktop/build-in-docker.sh
```

On Debian 13 (or newer) with live-build (Debian's live-build makes the ISO; what it puts in it is Ubuntu):

```bash
sudo apt install live-build debootstrap ubuntu-keyring git python3 python3-pil librsvg2-bin fonts-inter grub-common
sudo bash desktop/build.sh
```

The ISO (about 2.5 GB), its SHA-256, the list of packages in it and the build's log land in `desktop/out/`. A
build takes 15 to 40 minutes, depending on the machine and the network, and needs about 20 GB of disk.

Options (both scripts): `--server HOST:PORT` gives XenithOS Console a XenithOS Cloud address to start with
(default: `server=` in the repository's `config.local.txt`, which is never committed), `--channel URL` says
where installed PCs look for updates (default: `channel.txt`), `--snapshot 20261115T000000Z` builds from
Ubuntu as it was then (snapshot.ubuntu.com), `--keep` keeps the build folder.

What goes in, all in this folder:

| Path | What |
|---|---|
| `VERSION` | XenithOS Desktop's version |
| `build.sh` | the ISO build: live-build's settings, then everything below put in place |
| `make-debs.sh` | makes the four packages and the update bundle; the ISO installs the same packages an update brings |
| `release.sh`, `bundle-install.sh`, `get-update.sh.in`, `channel.txt`, `keys/` | updates: the signed channel, the bundle's installer, a one-line way to run an update from a terminal, where updates live, the key's public half |
| `packages/` | Ubuntu package lists: `system.list` (the base), `desktop.list`, `apps.list`, `security.list`, and `live.list` (only on the ISO: the installer's needs) |
| `rootfs/` | what the `xenithos-desktop` package holds: the session, Settings, the updater, the Apps menu's groups, the look, the start-up screen |
| `debian/` | the packages' set-up scripts (they run on every update) |
| `archive/` | the `xenithos-archive` package: where Brave's and Mullvad VPN's packages come from, and their keys |
| `live/` | only on the ISO: the installer and what starts it |
| `hooks/` | run inside the new system while the ISO is built: XenithOS Console compiled from this checkout (`0100`), checks and Ubuntu's extras taken out (`0200`), the GRUB packages the installer needs (`0300`), the language (`1030`) and the icon themes' indexes (`9050`). The last two are numbered to come after live-build's own hooks that would undo them |
| `boot/` | the USB stick's boot menu (GRUB, for UEFI and BIOS) |
| `art/` | the logo (`logo.svg` and `logo-x.svg`, written by `draw-logo.py` from the logo's numbers in `tools/mkart.py`, which XenithOS Console and the website share) and `make-art.py`, which draws the icons, wallpapers, start-up screen, boot menu and window borders from it |
| `write-usb.ps1` | puts the ISO on a USB stick from Windows |

The installer is `live/usr/lib/xenithos-desktop/installer.py` (the window) and `live/usr/sbin/xenithos-install`
(the work: it can also be run by hand, see the top of the file). The session is `rootfs/usr/bin/xenithos-session`
and `rootfs/usr/lib/xenithos-desktop/xsession`; Settings is `rootfs/usr/lib/xenithos-desktop/settings.py`; the
updater is `rootfs/usr/lib/xenithos-desktop/update-helper`. In the same folder, `desktop-entries` keeps the
Apps menu (names, and the Security Tools groups; the groups' layout is
`rootfs/etc/xdg/xenithos-desktop/menus/xfce-applications.menu`), `tool-terminal` is what a typed tool's
menu entry opens, and `console-apps` puts XenithOS Console's optional apps on its home screen and takes
them off.

## Trying it in a VM

```bash
bash desktop/test-in-virtualbox.sh                 # UEFI
bash desktop/test-in-virtualbox.sh --secure-boot   # UEFI with Secure Boot
bash desktop/test-in-virtualbox.sh --bios          # an old PC
```

After installing, `bash desktop/test-in-virtualbox.sh --eject` takes the ISO out and starts the VM from its
drive; `--delete` removes the VM.
