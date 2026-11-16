#!/usr/bin/env python3
"""Your own apps on XenithOS Console's home screen.

Any app in the desktop's Apps menu can go on XenithOS Console's home screen as well. There it is
two files in XenithOS Console's apps folder (~/.local/share/xenithos/uda:/apps):

    own-<name>.app     what XenithOS Console reads (linux/apps_linux.c): name=, what to run and,
                       for anything but a game, pointer=1 (the controller starts as the
                       pointer). The desktop= line is for this file only: which Apps menu
                       entry it stands for.
    own-<name>.ximg    its picture, drawn here from the app's icon and name

What to run is one of two lines:

    flatpak=<name on Flathub>    an app from Flathub, run XenithOS Console's own way: it gets the
                                 controllers, and XenithOS Console takes the entry off when the
                                 app is removed from the PC
    exec=<this file> run own-<name>    anything else: this file starts the Apps menu entry's
                                 own command

XenithOS Console opens the app full screen over itself. Its guide (the Xbox button, or holding
Esc) can switch the controller between the pointer, a keyboard and playing the game.

    ownapps.py list            a line for each app: its id|1 or 0 (on the home screen)|its name
    ownapps.py add ID          puts it on the home screen (ID as "list" prints it)
    ownapps.py remove ID       takes it off again
    ownapps.py run NAME        what an exec= entry starts: the app itself

Settings > Console apps uses the same functions.
"""
import os
import re
import shutil
import struct
import sys
import unicodedata
import zlib

import gi

gi.require_version("Gio", "2.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

SELF = os.path.abspath(__file__)
PREFIX = "own-"
W, H = 320, 180                         # a home screen picture, as tools/mkart.py makes them
FIELD_CODES = ("%f", "%F", "%u", "%U", "%d", "%D", "%n", "%N", "%i", "%k", "%v", "%m")
# The colour a card sits on, by what kind of app it is (tools/mkart.py's, plus one for the rest).
CARD_COLOURS = (("Game", (17, 27, 52)), ("AudioVideo", (34, 20, 48)), ("Audio", (34, 20, 48)),
                ("Video", (34, 20, 48)), ("Graphics", (34, 20, 48)), ("Network", (12, 38, 44)))
CARD_PLAIN = (20, 24, 32)
# A name on Flathub as XenithOS Console takes it (valid_ref in linux/apps_linux.c).
FLATHUB_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_-]*(\.[A-Za-z0-9_-]+){2,}")
# Prism Launcher. XenithOS Console starts it straight into Minecraft and adds an entry for the
# launcher's own window next to it, so this one is called what it opens.
MINECRAFT = "org.prismlauncher.PrismLauncher"


class Problem(Exception):
    """Something to tell the person, in a sentence."""


def data_dir():
    return os.environ.get("XENITHOS_DATA") or os.path.expanduser("~/.local/share/xenithos")


def apps_dir():
    return os.path.join(data_dir(), "uda:", "apps")


def read_app(path):
    """A .app file's lines as a dict."""
    found = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                key, sep, value = line.rstrip("\r\n").partition("=")
                if sep:
                    found[key] = value
    except OSError:
        pass
    return found


def entry(ident):
    """The Apps menu's entry with this id, or None when the PC has none (any more)."""
    try:
        return Gio.DesktopAppInfo.new(ident)
    except TypeError:                   # PyGObject's way of saying there's no such entry
        return None


def kinds(info):
    """The entry's Categories, as a list."""
    return [c for c in (info.get_categories() or "").split(";") if c]


def flathub_name(info):
    """The app's name on Flathub when this is a Flathub app's main entry, else ""."""
    name = info.get_string("X-Flatpak") or ""
    if name and info.get_id() == name + ".desktop" and len(name) <= 120 and FLATHUB_NAME.fullmatch(name):
        return name
    return ""


def home_screen():
    """What the files in XenithOS Console's apps folder say: (the Apps menu id -> the name of
    its files without the ending, for the apps put there from here; the Flathub names of the
    apps XenithOS Console's own Store put there)."""
    ours, stores = {}, set()
    try:
        names = os.listdir(apps_dir())
    except OSError:
        return ours, stores
    for name in names:
        if not name.endswith(".app"):
            continue
        says = read_app(os.path.join(apps_dir(), name))
        if name.startswith(PREFIX):
            if says.get("desktop"):
                ours[says["desktop"]] = name[:-4]
        elif says.get("flatpak", "").split():
            stores.add(says["flatpak"].split()[0])
    return ours, stores


def added():
    """Apps menu id -> the name of its files (without the ending), for what's on the home screen."""
    return home_screen()[0]


def offered():
    """The Apps menu's apps that can go on the home screen, by name. Left out: XenithOS Desktop's
    own entries (its doors to XenithOS Console and Settings, and the tools that only open a
    terminal on themselves), the desktop's control panels, and the apps XenithOS Console's own
    Store already put on the home screen."""
    stores = home_screen()[1]
    apps = []
    for info in Gio.AppInfo.get_all():
        ident = info.get_id() or ""
        if not ident or ident.startswith("xenithos-") or not info.should_show():
            continue
        if not (info.get_commandline() or "").strip():
            continue
        if "Settings" in kinds(info) or (info.get_string("X-Flatpak") or "") in stores:
            continue
        apps.append(info)
    apps.sort(key=lambda a: (a.get_display_name() or a.get_id()).lower())
    return apps


def file_name(ident):
    """A name for an app's two files that XenithOS Console can take: small letters, digits and
    dashes, not too long, and not one another app has."""
    base = ident[:-8] if ident.endswith(".desktop") else ident
    name = PREFIX + (re.sub(r"[^a-z0-9]+", "-", base.lower()).strip("-")[:40].strip("-") or "app")
    taken = read_app(os.path.join(apps_dir(), name + ".app")).get("desktop")
    if taken and taken != ident:
        name = "%s-%08x" % (name[:36], zlib.crc32(ident.encode()))
    return name


def one_line(text):
    return " ".join((text or "").split())


def plain(text):
    """Text as XenithOS Console's own font has it: plain letters (an é becomes an e)."""
    spelled = unicodedata.normalize("NFKD", text or "")
    return one_line("".join(c for c in spelled if 32 <= ord(c) < 127))


def shown_name(info):
    """What the app is called on the home screen."""
    if flathub_name(info) == MINECRAFT:
        return "Minecraft"
    return one_line(info.get_display_name() or info.get_name() or "") or info.get_id()


def what_it_is(info):
    """What could go under the name on the picture, best first: what kind of app the entry says
    it is, then the start of its description. Never the name again."""
    name = shown_name(info).lower()
    said = [one_line(info.get_generic_name() or ""),
            one_line(info.get_description() or "").split(". ")[0].rstrip(".")]
    return [words for words in said if words and words.lower() != name] + ["From the desktop"]


def picture(info, path):
    """Draws the app's home screen picture: its icon, its name and what it is, on a flat card."""
    import cairo
    gi.require_version("Gdk", "3.0")
    gi.require_version("Gtk", "3.0")
    gi.require_version("Pango", "1.0")
    gi.require_version("PangoCairo", "1.0")
    from gi.repository import Gdk, Gtk, Pango, PangoCairo

    categories = kinds(info)
    colour = next((c for kind, c in CARD_COLOURS if kind in categories), CARD_PLAIN)
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    cr = cairo.Context(surface)
    cr.set_source_rgb(*(v / 255 for v in colour))
    cr.paint()

    left = 22
    size = 64
    pixbuf = None
    try:
        theme = Gtk.IconTheme.get_default() if Gdk.Screen.get_default() else None
        if theme is None:
            theme = Gtk.IconTheme.new()
            theme.set_custom_theme("XenithOS")
        found = theme.lookup_by_gicon(info.get_icon(), size, Gtk.IconLookupFlags.FORCE_SIZE) if info.get_icon() else None
        pixbuf = found.load_icon() if found else None
    except GLib.Error:
        pixbuf = None
    if pixbuf is not None:
        Gdk.cairo_set_source_pixbuf(cr, pixbuf, left, (H - pixbuf.get_height()) // 2)
        cr.paint()
        left += size + 16

    room = W - left - 18

    def layout_of(text, weight, pixels, lines):
        layout = PangoCairo.create_layout(cr)
        font = Pango.FontDescription()
        font.set_family("Inter")
        font.set_weight(weight)
        font.set_absolute_size(pixels * Pango.SCALE)
        layout.set_font_description(font)
        if lines:
            layout.set_width(room * Pango.SCALE)
            layout.set_wrap(Pango.WrapMode.WORD_CHAR)
            layout.set_ellipsize(Pango.EllipsizeMode.END)
            layout.set_height(-lines)
        layout.set_text(text, -1)
        return layout

    # The name as big as fits on two lines with no word cut in two, then what it is under it.
    name = shown_name(info)
    title = None
    for pixels in range(40, 17, -2):
        title = layout_of(name, Pango.Weight.HEAVY, pixels, 2)
        whole = all(layout_of(word, Pango.Weight.HEAVY, pixels, 0).get_pixel_size()[0] <= room for word in name.split())
        if whole and not title.is_ellipsized():
            break
    about = None
    for words in what_it_is(info):          # the first that fits on its line; the last always does
        about = layout_of(words, Pango.Weight.MEDIUM, 15, 1)
        if not about.is_ellipsized():
            break
    title_h = title.get_pixel_size()[1]
    about_h = about.get_pixel_size()[1]
    y = (H - (title_h + 8 + about_h)) // 2
    cr.set_source_rgb(236 / 255, 241 / 255, 250 / 255)
    cr.move_to(left, y)
    PangoCairo.show_layout(cr, title)
    cr.set_source_rgb(170 / 255, 182 / 255, 206 / 255)
    cr.move_to(left + 1, y + title_h + 8)
    PangoCairo.show_layout(cr, about)
    surface.flush()

    # cairo keeps blue, green, red, alpha; XIMG wants red, green, blue, alpha.
    stride = surface.get_stride()
    data = bytes(surface.get_data())
    pixels = b"".join(data[row * stride:row * stride + W * 4] for row in range(H))
    raw = bytearray(W * H * 4)
    raw[0::4] = pixels[2::4]
    raw[1::4] = pixels[1::4]
    raw[2::4] = pixels[0::4]
    raw[3::4] = b"\xff" * (W * H)
    head = b"XIMG" + struct.pack(">HHBBHI", W, H, 1, 1, 0, len(raw))
    with open(path + ".part", "wb") as f:
        f.write(head + zlib.compress(bytes(raw), 9))
    os.replace(path + ".part", path)
    return surface


def add(ident):
    """Puts the Apps menu's entry ident on XenithOS Console's home screen."""
    info = entry(ident)
    if info is None:
        raise Problem("That app isn't on this PC any more.")
    try:
        os.makedirs(apps_dir(), exist_ok=True)
    except OSError as e:
        raise Problem("XenithOS Console's apps folder couldn't be made: %s." % e.strerror)
    name = added().get(ident) or file_name(ident)
    base = ident[:-8] if ident.endswith(".desktop") else ident
    called = plain(shown_name(info))[:39].strip() or plain(base)[:39].strip() or "App"
    flathub = flathub_name(info)
    lines = ["name=" + called,
             "flatpak=" + flathub if flathub else "exec=%s run %s" % (SELF, name)]
    if "Game" not in kinds(info):
        lines.append("pointer=1")           # anything but a game wants a pointer first
    lines.append("desktop=" + ident)
    path = os.path.join(apps_dir(), name)
    try:
        with open(path + ".app.part", "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        os.replace(path + ".app.part", path + ".app")
    except OSError as e:
        raise Problem("It couldn't be put on the home screen: %s." % e.strerror)
    try:
        picture(info, path + ".ximg")
    except Exception as e:      # no picture is fine: the home screen then shows its name on a card
        print("ownapps: no picture for %s: %s" % (ident, e), file=sys.stderr)
    return name


def remove(ident):
    """Takes ident off XenithOS Console's home screen. The app itself stays on the PC."""
    name = added().get(ident)
    if not name:
        return
    for ending in (".app", ".ximg"):
        try:
            os.remove(os.path.join(apps_dir(), name + ending))
        except FileNotFoundError:
            pass
        except OSError as e:
            raise Problem("It couldn't be taken off the home screen: %s." % e.strerror)


def command(info):
    """The app's command as a list, from its entry's Exec line with the placeholders taken out."""
    try:
        words = GLib.shell_parse_argv(info.get_string("Exec") or "")[1]
    except GLib.Error:
        words = (info.get_string("Exec") or "").split()
    out = []
    for word in words:
        if word in FIELD_CODES:
            continue
        if word == "%c":
            word = info.get_display_name() or ""
        word = re.sub(r"%[fFuUdDnNickvm]", "", word).replace("%%", "%")
        out.append(word)
    # An app from Flathub sees the controllers only when Flatpak is told to show it the PC's
    # devices (XenithOS Console says the same for the ones it starts itself).
    if info.get_string("X-Flatpak") and len(out) > 2 and os.path.basename(out[0]) == "flatpak" \
            and out[1] == "run" and "--device=all" not in out:
        out.insert(2, "--device=all")
    if out and info.get_boolean("Terminal"):
        # One that stays this program for as long as its window is open, so XenithOS Console
        # knows when the app is closed.
        if shutil.which("xfce4-terminal"):
            out = ["xfce4-terminal", "--disable-server", "-x"] + out
        else:
            out = ["x-terminal-emulator", "-e"] + out
    return out


def run(name):
    """Becomes the app that the home screen entry NAME stands for. The last line printed here is
    what XenithOS Console shows when an app is gone again straight away, after "<its name>
    stopped straight away:"."""
    ident = read_app(os.path.join(apps_dir(), name + ".app")).get("desktop", "")
    info = entry(ident) if ident else None
    if info is None:
        print("It isn't on this PC any more. PC Settings > Console apps takes it off the home screen.")
        return 1
    argv = command(info)
    if not argv:
        print("Its entry in the Apps menu doesn't say what to run.")
        return 1
    folder = info.get_string("Path")
    if folder:
        try:
            os.chdir(folder)
        except OSError:
            pass
    else:
        os.chdir(os.path.expanduser("~"))
    try:
        os.execvp(argv[0], argv)
    except OSError as e:
        print("%s: %s." % (argv[0], e.strerror))
        return 1


def main(argv):
    if len(argv) == 2 and argv[1] == "list":
        on = added()
        for info in offered():
            print("%s|%d|%s" % (info.get_id(), 1 if info.get_id() in on else 0, one_line(info.get_display_name())))
        return 0
    if len(argv) == 3 and argv[1] in ("add", "remove", "run"):
        try:
            if argv[1] == "run":
                return run(argv[2])
            (add if argv[1] == "add" else remove)(argv[2])
        except Problem as e:
            print(e, file=sys.stderr)
            return 1
        return 0
    print("ownapps.py list | add ID | remove ID | run NAME   (see the top of this file)", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
