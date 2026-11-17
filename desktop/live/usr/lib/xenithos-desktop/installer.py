#!/usr/bin/env python3
"""XenithOS Desktop's installer window.

    xenithos-installer [--fullscreen]

It asks for an account and a drive, says plainly that the drive will be erased, and shows how
the install is going. /usr/sbin/xenithos-install does the work (through sudo, which the live
system's account has without a password).
"""
import json
import os
import re
import subprocess
import sys

# Its own look whatever the desktop's theme is (console mode has none).
os.environ["GTK_THEME"] = "Adwaita:dark"

import gi  # noqa: E402

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk  # noqa: E402

BACKEND = "/usr/sbin/xenithos-install"
LOGO = "/usr/share/icons/XenithOS/scalable/apps/xenithos.svg"
LOG = "/var/log/xenithos-install.log"
MIN_BYTES = 16_000_000_000
RESERVED = {"root", "daemon", "bin", "sys", "sync", "games", "man", "lp", "mail", "news", "uucp",
            "proxy", "www-data", "backup", "list", "irc", "nobody", "messagebus", "polkitd"}

CSS = b"""
* { font-family: Inter, sans-serif; }
window, .page { background-color: #07080a; color: #f1f3f6; }
.title { font-size: 30px; font-weight: 700; }
.subtitle { color: #8b93a0; font-size: 15px; }
.field { color: #c4c9d1; font-size: 13px; font-weight: 600; }
.small { color: #8b93a0; font-size: 12px; }
.card { background-color: #12151a; border: 1px solid #262b33; border-radius: 12px; padding: 18px 20px; }
.warning { background-color: #12151a; border: 1px solid #262b33; border-radius: 12px; padding: 16px 20px; }
.warning-text { font-size: 15px; font-weight: 600; }
.error { color: #ff6b73; font-weight: 600; }
entry { background-color: #0d0f12; color: #f1f3f6; border: 1px solid #262b33; border-radius: 8px;
        padding: 8px 12px; min-height: 22px; box-shadow: none; caret-color: #e50914; }
entry:focus { border-color: #f1f3f6; box-shadow: none; }
combobox button { background-image: none; background-color: #0d0f12; color: #f1f3f6; border: 1px solid #262b33;
                  border-radius: 8px; padding: 6px 12px; box-shadow: none; }
button.action { background-image: none; background-color: #1b1f26; color: #f1f3f6; border: 1px solid #262b33;
                border-radius: 10px; padding: 10px 26px; font-size: 15px; font-weight: 600;
                box-shadow: none; text-shadow: none; }
button.action:hover { background-color: #232832; }
button.action:focus { border-color: #f1f3f6; }
button.primary { background-color: #f1f3f6; color: #07080a; border-color: #f1f3f6; }
button.primary:hover { background-color: #ffffff; }
button.danger { background-color: #e50914; color: #ffffff; border-color: #e50914; }
button.danger:hover { background-color: #f5222d; }
button.action:disabled { background-color: #12151a; color: #4b525c; border-color: #1d2128; }
checkbutton { font-size: 15px; font-weight: 600; }
checkbutton check { background-image: none; background-color: #0d0f12; border: 2px solid #4b525c;
                    border-radius: 4px; min-width: 18px; min-height: 18px; margin-right: 10px; }
checkbutton check:checked { background-color: #e50914; border-color: #e50914; color: #ffffff; }
list { background-color: transparent; padding: 2px; }
row.drive { background-color: #12151a; border: 1px solid #262b33; border-radius: 12px; padding: 14px 18px;
            margin-bottom: 10px; outline: none; }
row.drive:hover { background-color: #161a20; }
row.drive:selected { background-color: #171b21; border: 2px solid #f1f3f6; }
row.drive:disabled { opacity: 0.45; }
.drive-name { font-size: 16px; font-weight: 700; color: #f1f3f6; }
.drive-size { font-size: 16px; font-weight: 600; color: #c4c9d1; }
.drive-info { color: #8b93a0; font-size: 13px; }
progressbar trough { background-color: #12151a; border: none; border-radius: 5px; min-height: 10px; }
progressbar progress { background-color: #e50914; border: none; border-radius: 5px; min-height: 10px; }
scrolledwindow, viewport { background-color: transparent; border: none; }
"""


def human_size(n):
    """Drive sizes the way drive makers count them: 500 GB, 1 TB, 1.5 TB."""
    if n >= 1e12:
        t = n / 1e12
        return f"{t:.0f} TB" if t >= 10 or abs(t - round(t)) < 0.05 else f"{t:.1f} TB"
    return f"{n / 1e9:.0f} GB"


def live_disk():
    """The drive XenithOS Desktop started from (the USB stick), so it can't be picked."""
    try:
        src = subprocess.run(["findmnt", "-no", "SOURCE", "/run/live/medium"], capture_output=True,
                             text=True).stdout.strip()
        if not src:
            return ""
        parent = subprocess.run(["lsblk", "-no", "PKNAME", src], capture_output=True, text=True).stdout.split()
        return "/dev/" + parent[0] if parent else src
    except OSError:
        return ""


def describe(disk):
    """What's on a drive now, in a few words."""
    parts = disk.get("children") or []
    fstypes = [(p.get("fstype") or "").lower() for p in parts]
    labels = " ".join((p.get("partlabel") or "") + " " + (p.get("label") or "") for p in parts).lower()
    if not parts:
        return "Nothing on it yet" if not disk.get("fstype") else "Formatted, with no partitions"
    if "ntfs" in fstypes or "microsoft" in labels or "bitlocker" in fstypes:
        what = "Windows is on it"
    elif "apfs" in fstypes or "hfsplus" in fstypes:
        what = "macOS is on it"
    elif any(f in ("ext4", "ext3", "btrfs", "xfs", "f2fs") for f in fstypes):
        what = "Linux is on it"
    else:
        what = "It has files on it"
    n = len(parts)
    return f"{what} ({n} partition{'s' if n != 1 else ''})"


def list_drives():
    out = subprocess.run(["lsblk", "-J", "-b", "-o", "NAME,PATH,SIZE,TYPE,MODEL,VENDOR,TRAN,RM,RO,FSTYPE,LABEL,PARTLABEL"],
                         capture_output=True, text=True).stdout
    try:
        devices = json.loads(out).get("blockdevices", [])
    except ValueError:
        devices = []
    live = os.path.realpath(live_disk()) if live_disk() else ""
    drives = []
    for d in devices:
        name = d.get("name") or ""
        if d.get("type") != "disk" or name.startswith(("loop", "zram", "sr", "ram", "fd")):
            continue
        path = d.get("path") or "/dev/" + name
        size = int(d.get("size") or 0)
        if size == 0:
            continue  # a card reader with no card, say
        vendor = (d.get("vendor") or "").strip()
        if vendor.upper() in ("ATA", "NVME", "USB"):
            vendor = ""  # how it's connected, not who made it
        model = " ".join((vendor + " " + (d.get("model") or "").strip()).split())
        tran = (d.get("tran") or "").lower()
        kind = {"nvme": "NVMe", "sata": "SATA", "ata": "SATA", "usb": "USB", "mmc": "SD/eMMC",
                "virtio": "Virtual", "scsi": "SCSI", "sas": "SAS"}.get(tran, tran.upper())
        if not kind and name.startswith("nvme"):
            kind = "NVMe"
        if not kind and name.startswith("mmcblk"):
            kind = "SD/eMMC"
        why_not = ""
        if live and os.path.realpath(path) == live:
            why_not = "The USB stick XenithOS Desktop started from"
        elif d.get("ro") in (True, "1", 1):
            why_not = "Can only be read"
        elif size < MIN_BYTES:
            why_not = "Too small: XenithOS Desktop needs 16 GB or more"
        drives.append({"path": path, "size": size, "model": model or "Drive", "kind": kind,
                       "about": describe(d), "why_not": why_not,
                       "windows": "Windows" in describe(d)})
    return drives


def timezones():
    """Area -> [city], from tzdata's list of zones."""
    zones = {}
    for tab in ("/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab"):
        try:
            with open(tab) as f:
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    tz = line.split("\t")[2].strip()
                    area, _, city = tz.partition("/")
                    if city:
                        zones.setdefault(area, set()).add(city)
            break
        except OSError:
            continue
    return {a: sorted(c) for a, c in sorted(zones.items())}


def keyboard_layouts():
    """[(code, name)] from X's keyboard list, sorted by name."""
    layouts = []
    try:
        with open("/usr/share/X11/xkb/rules/evdev.lst") as f:
            section = ""
            for line in f:
                if line.startswith("!"):
                    section = line[1:].strip()
                    continue
                if section == "layout" and line.strip():
                    code, _, name = line.strip().partition(" ")
                    layouts.append((code, name.strip()))
    except OSError:
        pass
    if not layouts:
        layouts = [("us", "English (US)")]
    return sorted(layouts, key=lambda l: l[1].lower())


class Installer(Gtk.Window):
    def __init__(self, fullscreen):
        super().__init__(title="Install XenithOS Desktop")
        self.set_default_size(1024, 720)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.installing = False
        self.proc = None
        self.connect("delete-event", lambda *a: self.installing)
        self.connect("destroy", Gtk.main_quit)
        if fullscreen:
            self.fullscreen()

        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=150)
        self.add(self.stack)
        self.stack.add_named(self.page_welcome(), "welcome")
        self.stack.add_named(self.page_account(), "account")
        self.stack.add_named(self.page_drive(), "drive")
        self.stack.add_named(self.page_installing(), "installing")
        self.stack.add_named(self.page_done(), "done")
        self.stack.add_named(self.page_restarting(), "restarting")
        self.stack.add_named(self.page_failed(), "failed")
        self.show_all()

    # ------------------------------------------------------------ building blocks

    def page(self, title, subtitle=None, logo=False):
        """A page: a centred column with a title, and a box for its content."""
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        outer.get_style_context().add_class("page")
        column = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14, halign=Gtk.Align.CENTER,
                         valign=Gtk.Align.CENTER, vexpand=True)
        column.set_size_request(680, -1)
        outer.pack_start(column, True, True, 32)
        if logo and os.path.exists(LOGO):
            pix = GdkPixbuf.Pixbuf.new_from_file_at_size(LOGO, 96, 96)
            column.pack_start(Gtk.Image.new_from_pixbuf(pix), False, False, 8)
        t = Gtk.Label(label=title, xalign=0 if not logo else 0.5)
        t.get_style_context().add_class("title")
        column.pack_start(t, False, False, 0)
        if subtitle:
            s = Gtk.Label(label=subtitle, xalign=0 if not logo else 0.5, wrap=True, max_width_chars=70)
            s.get_style_context().add_class("subtitle")
            column.pack_start(s, False, False, 0)
        body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        column.pack_start(body, False, False, 10)
        return outer, column, body

    def buttons(self, column, *specs):
        """A row of buttons at the right: (label, style, callback)."""
        row = Gtk.Box(spacing=12, halign=Gtk.Align.END)
        made = []
        for label, style, callback in specs:
            b = Gtk.Button(label=label)
            b.get_style_context().add_class("action")
            if style:
                b.get_style_context().add_class(style)
            b.connect("clicked", callback)
            row.pack_start(b, False, False, 0)
            made.append(b)
        column.pack_start(row, False, False, 8)
        return made

    @staticmethod
    def label(text, style=None, xalign=0.0, wrap=True):
        l = Gtk.Label(label=text, xalign=xalign, wrap=wrap)
        if style:
            l.get_style_context().add_class(style)
        return l

    # ------------------------------------------------------------ pages

    def page_welcome(self):
        outer, column, body = self.page(
            "Install XenithOS Desktop",
            "A desktop in XenithOS's own look, with XenithOS Console one click away. Installing takes a few minutes.",
            logo=True)
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        card.get_style_context().add_class("card")
        card.pack_start(self.label("You'll need:", "field"), False, False, 0)
        card.pack_start(self.label("•  a keyboard, to make your account"), False, False, 0)
        card.pack_start(self.label("•  a drive of 16 GB or more that can be erased"), False, False, 0)
        body.pack_start(card, False, False, 0)
        _, start = self.buttons(column, ("Not now", None, lambda b: self.destroy()),
                                ("Start", "primary", lambda b: self.go("account")))
        GLib.idle_add(start.grab_focus)
        return outer

    def page_account(self):
        outer, column, body = self.page(
            "Your account",
            "XenithOS runs as this account. Its password is for changing the PC's settings and updates.")
        grid = Gtk.Grid(column_spacing=18, row_spacing=12)
        body.pack_start(grid, False, False, 0)
        self.e_name = Gtk.Entry(hexpand=True, max_length=60)
        self.e_user = Gtk.Entry(max_length=32)
        self.e_host = Gtk.Entry(max_length=63)
        self.e_pass = Gtk.Entry(visibility=False, max_length=128)
        self.e_pass2 = Gtk.Entry(visibility=False, max_length=128)
        for e in (self.e_pass, self.e_pass2):
            e.set_input_purpose(Gtk.InputPurpose.PASSWORD)
        self.user_edited = self.host_edited = False
        self.e_name.connect("changed", self.name_changed)
        self.e_user.connect("changed", lambda e: self.mark_edited("user"))
        self.e_host.connect("changed", lambda e: self.mark_edited("host"))

        self.c_keyboard = Gtk.ComboBoxText()
        for code, name in keyboard_layouts():
            self.c_keyboard.append(code, name)
        self.c_keyboard.set_active_id("us")
        self.c_keyboard.connect("changed", self.keyboard_changed)

        self.zones = timezones()
        self.c_area = Gtk.ComboBoxText()
        self.c_city = Gtk.ComboBoxText(hexpand=True)
        self.c_area.append("UTC", "UTC")
        for area in self.zones:
            self.c_area.append(area, area)
        self.c_area.connect("changed", self.area_changed)
        current = ""
        try:
            current = open("/etc/timezone").read().strip()
        except OSError:
            pass
        area, _, city = current.partition("/")
        self.c_area.set_active_id(area if area in self.zones else "UTC")
        if city:
            self.c_city.set_active_id(city)
        tz_row = Gtk.Box(spacing=10)
        tz_row.pack_start(self.c_area, False, False, 0)
        tz_row.pack_start(self.c_city, True, True, 0)

        rows = [("Your name", self.e_name), ("User name", self.e_user), ("Computer's name", self.e_host),
                ("Password", self.e_pass), ("Password again", self.e_pass2),
                ("Keyboard", self.c_keyboard), ("Time zone", tz_row)]
        for i, (text, widget) in enumerate(rows):
            grid.attach(self.label(text, "field", wrap=False), 0, i, 1, 1)
            grid.attach(widget, 1, i, 1, 1)
        self.account_error = self.label("", "error")
        body.pack_start(self.account_error, False, False, 0)
        self.buttons(column, ("Back", None, lambda b: self.go("welcome")),
                     ("Next", "primary", self.account_next))
        self.e_pass2.connect("activate", self.account_next)
        return outer

    def page_drive(self):
        outer, column, body = self.page("Pick a drive", "XenithOS Desktop takes the whole drive you pick.")
        scroller = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroller.set_min_content_height(120)
        scroller.set_max_content_height(330)
        scroller.set_propagate_natural_height(True)
        self.drive_list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.SINGLE)
        self.drive_list.connect("row-selected", lambda l, r: self.drive_changed())
        scroller.add(self.drive_list)
        body.pack_start(scroller, False, False, 0)

        warning = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        warning.get_style_context().add_class("warning")
        self.warning_text = self.label(
            "Everything on this drive will be deleted, including any other OS, files and games. This can't be undone.",
            "warning-text")
        warning.pack_start(self.warning_text, False, False, 0)
        self.understand = Gtk.CheckButton(label="I understand, erase this drive")
        self.understand.connect("toggled", lambda b: self.drive_changed())
        warning.pack_start(self.understand, False, False, 0)
        body.pack_start(warning, False, False, 0)
        _, self.erase_button = self.buttons(column, ("Back", None, lambda b: self.go("account")),
                                            ("Erase and install", "danger", self.start_install))
        return outer

    def page_installing(self):
        outer, column, body = self.page("Installing XenithOS Desktop")
        self.step_label = self.label("Getting ready", "subtitle")
        body.pack_start(self.step_label, False, False, 0)
        self.progress = Gtk.ProgressBar()
        body.pack_start(self.progress, False, False, 6)
        body.pack_start(self.label("Leave the PC on and the USB stick in until it's done.", "small"), False, False, 0)
        return outer

    def page_done(self):
        outer, column, body = self.page(
            "XenithOS Desktop is installed",
            "Take out the USB stick, then restart. Your PC will start in XenithOS Desktop.", logo=True)
        _, self.restart_button = self.buttons(column, ("Not yet", None, lambda b: self.destroy()),
                                              ("Restart now", "primary", self.restart))
        return outer

    def page_restarting(self):
        outer, column, body = self.page(
            "Restarting",
            "Please wait for the PC to restart by itself. Don't turn it off. This can take a minute.", logo=True)
        bar = Gtk.ProgressBar()
        body.pack_start(bar, False, False, 0)
        GLib.timeout_add(120, lambda: bar.pulse() or True)
        return outer

    def page_failed(self):
        outer, column, body = self.page("The install didn't finish")
        self.failed_text = self.label("", "error")
        body.pack_start(self.failed_text, False, False, 0)
        body.pack_start(self.label(f"What happened, step by step, is in {LOG}. The drive may be part erased: "
                                   "installing again erases it properly.", "subtitle"), False, False, 0)
        self.buttons(column, ("Close", None, lambda b: self.destroy()),
                     ("Try again", "primary", lambda b: self.go("drive")))
        return outer

    # ------------------------------------------------------------ behaviour

    def go(self, name):
        if name == "drive":
            self.fill_drives()
        self.stack.set_visible_child_name(name)
        if name == "account":
            GLib.idle_add(self.e_name.grab_focus)

    def name_changed(self, entry):
        first = re.sub(r"[^a-z0-9]", "", (entry.get_text().strip().split() or [""])[0].lower())
        if first and not first[0].isalpha():
            first = "p" + first
        user = first[:32] or ""
        if not self.user_edited:
            self.e_user.set_text(user)
            self.user_edited = False
        if not self.host_edited:
            self.e_host.set_text(f"xenithos-{user}" if user else "xenithos")
            self.host_edited = False

    def mark_edited(self, which):
        # Typing in the field yourself stops it following your name.
        focus = self.get_focus()
        if which == "user" and focus is self.e_user:
            self.user_edited = True
        if which == "host" and focus is self.e_host:
            self.host_edited = True

    def keyboard_changed(self, combo):
        # Type the password with the layout the PC will have.
        code = combo.get_active_id()
        if code:
            subprocess.run(["setxkbmap", code], stderr=subprocess.DEVNULL)

    def area_changed(self, combo):
        area = combo.get_active_id()
        self.c_city.remove_all()
        if area == "UTC":
            self.c_city.append("UTC", "UTC")
        else:
            for city in self.zones.get(area, []):
                self.c_city.append(city, city.replace("_", " ").replace("/", ", "))
        self.c_city.set_active(0)

    def timezone(self):
        area, city = self.c_area.get_active_id(), self.c_city.get_active_id()
        if area == "UTC" or not city:
            return "Etc/UTC"
        return f"{area}/{city}"

    def account_next(self, *args):
        name = self.e_name.get_text().strip()
        user = self.e_user.get_text().strip()
        host = self.e_host.get_text().strip()
        problem = ""
        if not name:
            problem = "Type your name."
        elif not re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", user):
            problem = "The user name can only have small letters, numbers, - and _, and starts with a letter."
        elif user in RESERVED or user.startswith("systemd-"):
            problem = f"The user name {user} is taken by the system. Try another."
        elif not re.fullmatch(r"[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?", host):
            problem = "The computer's name can only have letters, numbers and - (not at the start or end)."
        elif not self.e_pass.get_text():
            problem = "Choose a password."
        elif self.e_pass.get_text() != self.e_pass2.get_text():
            problem = "The two passwords aren't the same."
        self.account_error.set_text(problem)
        if not problem:
            self.go("drive")

    def fill_drives(self):
        for row in self.drive_list.get_children():
            self.drive_list.remove(row)
        self.drives = list_drives()
        first = None
        for d in self.drives:
            row = Gtk.ListBoxRow()
            row.get_style_context().add_class("drive")
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            top = Gtk.Box(spacing=12)
            top.pack_start(self.label(d["model"], "drive-name", wrap=False), True, True, 0)
            size = f"{human_size(d['size'])}" + (f"  ·  {d['kind']}" if d["kind"] else "")
            top.pack_start(self.label(size, "drive-size", xalign=1.0, wrap=False), False, False, 0)
            box.pack_start(top, False, False, 0)
            box.pack_start(self.label(d["why_not"] or d["about"], "drive-info"), False, False, 0)
            row.add(box)
            row.drive = d
            row.set_sensitive(not d["why_not"])
            self.drive_list.add(row)
            if first is None and not d["why_not"]:
                first = row
        if not self.drives:
            row = Gtk.ListBoxRow(selectable=False)
            row.add(self.label("No drives found. Connect the drive to install on, then go back and come here again.",
                               "drive-info"))
            self.drive_list.add(row)
        self.drive_list.show_all()
        self.understand.set_active(False)
        if first is not None:
            self.drive_list.select_row(first)
            # Arrow keys pick a drive; Tab goes on to "I understand" and the buttons.
            GLib.idle_add(first.grab_focus)
        self.drive_changed()

    def selected_drive(self):
        row = self.drive_list.get_selected_row()
        return getattr(row, "drive", None) if row is not None else None

    def drive_changed(self):
        d = self.selected_drive()
        text = "Everything on this drive will be deleted, including any other OS, files and games. This can't be undone."
        if d and d.get("windows"):
            text = "Windows is on this drive. It, and everything else on the drive, will be deleted. This can't be undone."
        self.warning_text.set_text(text)
        self.erase_button.set_sensitive(bool(d) and not d["why_not"] and self.understand.get_active())

    def start_install(self, *args):
        d = self.selected_drive()
        if not d or d["why_not"] or not self.understand.get_active():
            return
        cmd = [BACKEND, "--disk", d["path"], "--erase", "--user", self.e_user.get_text().strip(),
               "--fullname", self.e_name.get_text().strip(), "--hostname", self.e_host.get_text().strip(),
               "--timezone", self.timezone(), "--keyboard", self.c_keyboard.get_active_id() or "us"]
        if os.geteuid() != 0:
            cmd = ["sudo", "-n"] + cmd
        self.installing = True
        self.progress.set_fraction(0.0)
        self.step_label.set_text("Getting ready")
        self.stack.set_visible_child_name("installing")
        self.last_error = ""
        try:
            self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, bufsize=1)
            self.proc.stdin.write(self.e_pass.get_text() + "\n")
            self.proc.stdin.close()
        except OSError as e:
            self.finish(False, f"The installer couldn't start: {e}")
            return
        GLib.io_add_watch(self.proc.stdout, GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, self.on_output)

    def on_output(self, stream, condition):
        line = stream.readline()
        if line:
            line = line.strip()
            if line.startswith("STEP "):
                pct, _, text = line[5:].partition(" ")
                if pct.isdigit():
                    self.progress.set_fraction(min(int(pct), 100) / 100)
                self.step_label.set_text(text)
            elif line.startswith("ERROR "):
                self.last_error = line[6:]
            elif line == "DONE":
                self.progress.set_fraction(1.0)
            elif line:
                self.last_error = line  # sudo's own complaints, say
            return True
        code = self.proc.wait()
        self.finish(code == 0, self.last_error or f"It stopped with code {code}.")
        return False

    def finish(self, ok, error=""):
        self.installing = False
        self.e_pass.set_text("")
        self.e_pass2.set_text("")
        if ok:
            self.stack.set_visible_child_name("done")
            GLib.idle_add(self.restart_button.grab_focus)
        else:
            self.failed_text.set_text(error)
            self.stack.set_visible_child_name("failed")

    def restart(self, *args):
        # Shutting the live system down takes a while with nothing else to show for it: say so.
        self.installing = True
        self.stack.set_visible_child_name("restarting")

        def go():
            for cmd in (["sudo", "-n", "systemctl", "reboot"], ["systemctl", "reboot"]):
                try:
                    if subprocess.run(cmd, stderr=subprocess.DEVNULL).returncode == 0:
                        break
                except OSError:
                    continue
            return False

        GLib.timeout_add(400, go)


def main():
    provider = Gtk.CssProvider()
    provider.load_from_data(CSS)
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), provider,
                                             Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    Installer("--fullscreen" in sys.argv[1:])
    Gtk.main()


if __name__ == "__main__":
    main()
