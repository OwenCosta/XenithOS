#!/usr/bin/env python3
"""XenithOS Desktop's Settings: Wi-Fi, sound, the desktop's look, what the PC starts in,
XenithOS Console's apps, security, updates and what this PC is.

    xenithos-settings [--page wifi|sound|look|startup|apps|security|updates|about]
    settings.py --notify-updates     (at desktop start: says when updates are ready)

It works in the desktop and from XenithOS Console's home screen (as the "PC Settings" app).
"""
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import consoleupdate  # noqa: E402
import ownapps  # noqa: E402
import xui  # noqa: E402
from xui import Gdk, GdkPixbuf, GLib, Gtk, button, card, label, later, run, setting_row  # noqa: E402

STATUS = "/var/lib/xenithos-desktop/update-status"
RESTART_MARKS = ("/run/reboot-required", "/run/xenithos-desktop-restart")
HELPER = "/usr/lib/xenithos-desktop/update-helper"
SECURITY_HELPER = "/usr/lib/xenithos-desktop/security-helper"
CONSOLE_APPS = "/usr/lib/xenithos-desktop/console-apps"
WALLPAPERS = "/usr/share/backgrounds/xenithos"
CONF_DIR = xui.CONF_DIR


def have(program):
    return shutil.which(program) is not None


def split_fields(line):
    """nmcli -t output: fields split at ':' (a ':' inside a field is written '\\:')."""
    parts = re.split(r"(?<!\\):", line)
    return [p.replace("\\:", ":").replace("\\\\", "\\") for p in parts]


def os_version():
    try:
        for line in open("/etc/os-release"):
            if line.startswith("VERSION="):
                return line.split("=", 1)[1].strip().strip('"')
    except OSError:
        pass
    return ""


# ------------------------------------------------------------------ pages

class Page:
    title = ""
    icon = ""

    def __init__(self, window):
        self.window = window
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.built = False

    def show(self):
        if not self.built:
            self.built = True
            self.build()
            self.box.show_all()
        self.refresh()

    def build(self):
        pass

    def refresh(self):
        pass

    def add(self, widget):
        self.box.pack_start(widget, False, False, 0)
        return widget


class WifiPage(Page):
    title, icon = "Wi-Fi", "network-wireless"

    def build(self):
        self.switch = Gtk.Switch()
        self.switch.connect("state-set", self.toggle)
        self.status = label("", "small")
        self.add(card(setting_row("Wi-Fi", "Join a network without a cable", self.switch), self.status))
        self.message = label("", "error")
        self.add(self.message)
        self.list = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        self.list.get_style_context().add_class("rows")
        self.list.connect("row-activated", self.row_chosen)
        scroller = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroller.set_min_content_height(250)
        scroller.set_max_content_height(330)
        scroller.set_propagate_natural_height(True)
        scroller.add(self.list)
        self.add(scroller)
        row = Gtk.Box(spacing=10)
        row.pack_start(button("Look again", None, lambda b: self.refresh(rescan=True)), False, False, 0)
        row.pack_start(button("Join a hidden network", None, self.join_hidden), False, False, 0)
        if have("nm-connection-editor"):
            row.pack_end(button("More network settings", None, lambda b: subprocess.Popen(["nm-connection-editor"])),
                         False, False, 0)
        self.add(row)
        self.busy = False
        self.setting = False

    def refresh(self, rescan=False):
        if self.busy:
            return
        self.busy = True
        self.status.set_text("Looking for networks")

        def work():
            if not have("nmcli"):
                return None
            code, radio = run(["nmcli", "-t", "-f", "WIFI", "radio"])
            if code != 0:
                return None                                   # NetworkManager isn't running
            _, devices = run(["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"])
            _, known = run(["nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"])
            code, nets = run(["nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list",
                              "--rescan", "yes" if rescan else "auto"], timeout=40)
            return radio, devices, known, nets if code == 0 else ""

        later(work, self.loaded)

    def loaded(self, result):
        self.busy = False
        for child in self.list.get_children():
            self.list.remove(child)
        if result is None:
            self.status.set_text("The network service (NetworkManager) isn't running, so Settings can't manage networks.")
            self.switch.set_sensitive(False)
            return
        radio, devices, known, nets = result
        wifi_device = wired = None
        connected = ""
        for line in devices.splitlines():
            f = split_fields(line)
            if len(f) < 4:
                continue
            if f[1] == "wifi":
                wifi_device = f[0]
                if f[2].startswith("connected"):
                    connected = f[3]
            elif f[1] == "ethernet" and f[2].startswith("connected"):
                wired = f[0]
        self.device = wifi_device
        self.setting = True
        self.switch.set_sensitive(wifi_device is not None)
        self.switch.set_active(radio.strip() == "enabled" and wifi_device is not None)
        self.setting = False
        self.known = {split_fields(l)[0] for l in known.splitlines() if l.endswith(":802-11-wireless")}
        if wifi_device is None:
            text = "This PC has no Wi-Fi adapter that XenithOS Desktop can use."
        elif radio.strip() != "enabled":
            text = "Wi-Fi is off."
        elif connected:
            text = f"Connected to {connected}."
        else:
            text = "Not connected. Choose a network below."
        if wired:
            text += " A network cable is connected too." if wifi_device else " The network cable is connected."
        self.status.set_text(text)

        best = {}
        for line in nets.splitlines():
            f = split_fields(line)
            if len(f) < 4 or not f[1]:
                continue
            signal = int(f[2]) if f[2].isdigit() else 0
            if f[1] not in best or signal > best[f[1]][0] or f[0] == "*":
                best[f[1]] = (signal, f[3], f[0] == "*" or (f[1] in best and best[f[1]][2]))
        for ssid, (signal, security, in_use) in sorted(best.items(), key=lambda kv: (-kv[1][2], -kv[1][0])):
            row = Gtk.ListBoxRow()
            line = Gtk.Box(spacing=12)
            text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
            text.pack_start(label(ssid, "row-title", wrap=False), False, False, 0)
            locked = security not in ("", "--")
            about = "Connected" if in_use else ("Saved" if ssid in self.known else ("Needs a password" if locked else "Open"))
            text.pack_start(label(about, "small"), False, False, 0)
            line.pack_start(text, True, True, 0)
            strength = "Strong" if signal >= 67 else "Good" if signal >= 40 else "Weak"
            line.pack_end(label(strength, "signal", xalign=1.0, wrap=False), False, False, 0)
            if in_use or ssid in self.known:
                forget = button("Forget", None, lambda b, s=ssid: self.forget(s))
                line.pack_end(forget, False, False, 0)
            row.add(line)
            row.ssid, row.locked, row.in_use = ssid, locked, in_use
            self.list.add(row)
        if not best and wifi_device and radio.strip() == "enabled":
            row = Gtk.ListBoxRow(activatable=False)
            row.add(label("No networks found yet. Try Look again in a moment.", "small"))
            self.list.add(row)
        self.list.show_all()

    def toggle(self, switch, state):
        if self.setting:
            return False
        later(lambda: run(["nmcli", "radio", "wifi", "on" if state else "off"]),
              lambda r: GLib.timeout_add(1500, lambda: self.refresh(rescan=state) and False))
        return False

    def row_chosen(self, listbox, row):
        if getattr(row, "in_use", True):
            return
        ssid = row.ssid
        if ssid in self.known:
            self.connect(["nmcli", "connection", "up", "id", ssid], ssid)
        elif not row.locked:
            self.connect(["nmcli", "device", "wifi", "connect", ssid], ssid)
        else:
            password = self.ask_password(f"Join {ssid}", "Type the network's password.")
            if password:
                self.connect(["nmcli", "device", "wifi", "connect", ssid, "password", password], ssid, new=True)

    def join_hidden(self, *args):
        name = self.ask_password("Join a hidden network", "Type the network's name.", secret=False)
        if not name:
            return
        password = self.ask_password(f"Join {name}", "Type its password, or leave it empty if it has none.", allow_empty=True)
        cmd = ["nmcli", "device", "wifi", "connect", name, "hidden", "yes"]
        if password:
            cmd += ["password", password]
        self.connect(cmd, name, new=True)

    def connect(self, cmd, ssid, new=False):
        self.message.set_text("")
        self.status.set_text(f"Joining {ssid}")

        def work():
            code, out = run(cmd, timeout=60)
            if code != 0 and new:
                run(["nmcli", "connection", "delete", "id", ssid])      # a half-made one would say "Saved"
            return code, out

        def done(result):
            code, out = result
            if code != 0:
                wrong = "secrets were required" in out.lower() or "password" in out.lower()
                self.message.set_text(f"Couldn't join {ssid}. " + ("Check the password and try again." if wrong else
                                                                    out.splitlines()[-1] if out else ""))
            self.refresh()

        later(work, done)

    def forget(self, ssid):
        later(lambda: run(["nmcli", "connection", "delete", "id", ssid]), lambda r: self.refresh())

    def ask_password(self, title, text, secret=True, allow_empty=False):
        dialog = Gtk.Dialog(title=title, transient_for=self.window, modal=True)
        dialog.get_style_context().add_class("xenith")
        box = dialog.get_content_area()
        box.set_border_width(22)
        box.set_spacing(10)
        box.add(label(title, "title"))
        box.add(label(text, "subtitle"))
        entry = Gtk.Entry(visibility=not secret, activates_default=True, width_chars=32)
        box.add(entry)
        if secret:
            show = Gtk.CheckButton(label="Show what I type")
            show.connect("toggled", lambda b: entry.set_visibility(b.get_active()))
            box.add(show)
        dialog.add_action_widget(button("Not now"), Gtk.ResponseType.CANCEL)
        go = button("Join" if secret else "Next", "primary")
        go.set_can_default(True)
        dialog.add_action_widget(go, Gtk.ResponseType.OK)
        dialog.set_default_response(Gtk.ResponseType.OK)
        dialog.show_all()
        answer = dialog.run()
        value = entry.get_text()
        dialog.destroy()
        if answer != Gtk.ResponseType.OK:
            return None
        return value if (value or allow_empty) else None


class SoundPage(Page):
    title, icon = "Sound", "audio-volume-high"

    def build(self):
        self.setting = False
        self.out = Gtk.ComboBoxText(hexpand=True)
        self.out.connect("changed", lambda c: self.pick("sink"))
        self.out_volume = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.out_volume.set_draw_value(False)
        self.out_volume.set_hexpand(True)
        self.out_volume.connect("value-changed", lambda s: self.volume("sink"))
        self.out_mute = Gtk.Switch()
        self.out_mute.connect("state-set", lambda s, state: self.mute("sink", state))
        test = button("Play a test sound", None, self.test)
        self.out_percent = label("", "value", xalign=1.0, wrap=False)
        self.out_percent.set_size_request(48, -1)
        level = Gtk.Box(spacing=12)
        level.pack_start(self.out_volume, True, True, 0)
        level.pack_start(self.out_percent, False, False, 0)
        self.add(label("Speakers and headphones", "heading"))
        self.add(card(self.out, level, setting_row("Mute", None, self.out_mute), test))

        self.inp = Gtk.ComboBoxText(hexpand=True)
        self.inp.connect("changed", lambda c: self.pick("source"))
        self.in_volume = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.in_volume.set_draw_value(False)
        self.in_volume.connect("value-changed", lambda s: self.volume("source"))
        self.in_mute = Gtk.Switch()
        self.in_mute.connect("state-set", lambda s, state: self.mute("source", state))
        self.add(label("Microphone", "heading"))
        self.add(card(self.inp, self.in_volume, setting_row("Mute", None, self.in_mute)))
        self.note = label("", "small")
        self.add(self.note)
        if have("pavucontrol"):
            row = Gtk.Box()
            row.pack_start(button("Sound mixer", None, lambda b: subprocess.Popen(["pavucontrol"])), False, False, 0)
            self.add(row)

    def widgets(self, kind):
        return (self.out, self.out_volume, self.out_mute) if kind == "sink" else (self.inp, self.in_volume, self.in_mute)

    def refresh(self):
        def work():
            if not have("wpctl") or not have("pw-dump"):
                return None
            code, out = run(["pw-dump"], timeout=10)
            nodes = {"sink": [], "source": []}
            try:
                for obj in json.loads(out) if code == 0 else []:
                    props = (obj.get("info") or {}).get("props") or {}
                    kind = {"Audio/Sink": "sink", "Audio/Source": "source"}.get(props.get("media.class"))
                    if kind:
                        nodes[kind].append((str(obj["id"]), props.get("node.description") or props.get("node.name") or "Sound device"))
            except (ValueError, KeyError):
                pass
            state = {}
            for kind, target in (("sink", "@DEFAULT_AUDIO_SINK@"), ("source", "@DEFAULT_AUDIO_SOURCE@")):
                _, inspect = run(["wpctl", "inspect", target])
                m = re.match(r"id (\d+)", inspect)
                _, vol = run(["wpctl", "get-volume", target])
                v = re.search(r"Volume: ([\d.]+)", vol)
                state[kind] = (m.group(1) if m else "", float(v.group(1)) if v else None, "MUTED" in vol)
            return nodes, state

        later(work, self.loaded)

    def loaded(self, result):
        if result is None:
            self.note.set_text("The sound system (PipeWire) isn't running, so there's nothing to set here.")
            return
        nodes, state = result
        self.setting = True
        for kind in ("sink", "source"):
            combo, scale, mute = self.widgets(kind)
            combo.remove_all()
            for node_id, name in nodes[kind]:
                combo.append(node_id, name)
            default, volume, muted = state[kind]
            if default:
                combo.set_active_id(default)
            usable = volume is not None
            for w in (combo, scale, mute):
                w.set_sensitive(usable)
            if usable:
                scale.set_value(round(min(volume, 1.0) * 100))
                mute.set_active(muted)
        v = state["sink"][1]
        self.out_percent.set_text(f"{round(min(v, 1.0) * 100)}%" if v is not None else "")
        self.note.set_text("" if nodes["sink"] else "No speakers or headphones found.")
        self.setting = False

    def pick(self, kind):
        combo = self.widgets(kind)[0]
        if self.setting or not combo.get_active_id():
            return
        later(lambda: run(["wpctl", "set-default", combo.get_active_id()]), lambda r: self.refresh())

    def volume(self, kind):
        if self.setting:
            return
        combo, scale, _ = self.widgets(kind)
        target = "@DEFAULT_AUDIO_SINK@" if kind == "sink" else "@DEFAULT_AUDIO_SOURCE@"
        value = scale.get_value() / 100
        if kind == "sink":
            self.out_percent.set_text(f"{round(value * 100)}%")
        later(lambda: run(["wpctl", "set-volume", target, f"{value:.2f}"]))

    def mute(self, kind, state):
        if self.setting:
            return False
        target = "@DEFAULT_AUDIO_SINK@" if kind == "sink" else "@DEFAULT_AUDIO_SOURCE@"
        later(lambda: run(["wpctl", "set-mute", target, "1" if state else "0"]))
        return False

    def test(self, *args):
        path = os.path.join(GLib.get_user_cache_dir(), "xenithos-test-sound.wav")
        if not os.path.exists(path):
            rate = 44100
            with wave.open(path, "wb") as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(rate)
                frames = bytearray()
                for note, length in ((523.25, 0.16), (659.25, 0.16), (783.99, 0.34)):
                    n = int(rate * length)
                    for i in range(n):
                        fade = min(1.0, i / 400, (n - i) / 2500)
                        s = int(9000 * fade * math.sin(2 * math.pi * note * i / rate))
                        frames += struct.pack("<hh", s, s)
                w.writeframes(bytes(frames))
        player = "pw-play" if have("pw-play") else "aplay"
        subprocess.Popen([player, path], stderr=subprocess.DEVNULL)


def xfconf_set(channel, prop, kind, value):
    return run(["xfconf-query", "-c", channel, "-p", prop, "-n", "-t", kind, "-s", str(value)])[0] == 0


def xfconf_get(channel, prop):
    code, out = run(["xfconf-query", "-c", channel, "-p", prop])
    return out.strip() if code == 0 else ""


class LookPage(Page):
    title, icon = "Look", "preferences-desktop-theme"

    def build(self):
        self.add(label("Colour", "heading"))
        row = Gtk.Box(spacing=10)
        self.swatches = []
        for name, colour in xui.ACCENTS:
            b = Gtk.Button()
            b.get_style_context().add_class("swatch")
            b.set_tooltip_text(name)
            css = Gtk.CssProvider()
            css.load_from_data(f"button {{ background-color: {colour}; }}".encode())
            b.get_style_context().add_provider(css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 1)
            b.connect("clicked", lambda btn, n=name, c=colour: self.set_accent(n, c))
            row.pack_start(b, False, False, 0)
            self.swatches.append((b, colour))
        self.add(card(label("What's switched on, the chosen tab and the open window take this colour.", "small"), row))

        self.add(label("Wallpaper", "heading"))
        self.pictures = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=5,
                                    column_spacing=6, row_spacing=6, homogeneous=True)
        self.picture_buttons = []
        try:
            files = sorted(f for f in os.listdir(WALLPAPERS) if f.lower().endswith((".png", ".jpg", ".jpeg")))
        except OSError:
            files = []
        for name in files:
            path = os.path.join(WALLPAPERS, name)
            try:
                pix = GdkPixbuf.Pixbuf.new_from_file_at_scale(path, 150, 84, False)
            except GLib.Error:
                continue
            b = Gtk.Button(image=Gtk.Image.new_from_pixbuf(pix))
            b.get_style_context().add_class("picture")
            b.connect("clicked", lambda btn, p=path: self.set_wallpaper(p))
            self.pictures.add(b)
            self.picture_buttons.append((b, path))
        choose = Gtk.Box()
        choose.pack_start(button("Choose a picture", None, self.choose_picture), False, False, 0)
        self.add(card(self.pictures, choose))

        self.add(label("Panel and windows", "heading"))
        self.panel_place = self.choice(("Bottom", "Top"), self.set_panel_place)
        self.panel_size = self.choice(("Small", "Medium", "Large"), self.set_panel_size)
        self.buttons_side = self.choice(("Right", "Left"), self.set_buttons_side)
        self.text_size = self.choice(("Small", "Normal", "Large", "Larger"), self.set_text_size)
        self.bigger = Gtk.Switch()
        self.bigger.connect("state-set", self.set_bigger)
        self.add(card(setting_row("Panel", "Where the bar with the Apps menu and clock sits", self.panel_place),
                      setting_row("Panel size", None, self.panel_size),
                      setting_row("Window buttons", "Close, bigger and smaller", self.buttons_side),
                      setting_row("Text size", None, self.text_size),
                      setting_row("Everything twice as big", "For very sharp screens (4K) where things look tiny", self.bigger)))
        self.setting = False

    def choice(self, names, on_change):
        combo = Gtk.ComboBoxText()
        for n in names:
            combo.append(n, n)
        combo.connect("changed", lambda c: None if self.setting else on_change(c.get_active_id()))
        return combo

    def refresh(self):
        self.setting = True
        current = xui.accent()
        for b, colour in self.swatches:
            ctx = b.get_style_context()
            ctx.add_class("chosen") if colour == current else ctx.remove_class("chosen")
        wall = xui.read_conf().get("wallpaper", os.path.join(WALLPAPERS, "wallpaper.png"))
        for b, path in self.picture_buttons:
            ctx = b.get_style_context()
            ctx.add_class("chosen") if path == wall else ctx.remove_class("chosen")
        position = xfconf_get("xfce4-panel", "/panels/panel-1/position")
        self.panel_place.set_active_id("Top" if position.startswith("p=6") else "Bottom")
        size = xfconf_get("xfce4-panel", "/panels/panel-1/size")
        size = int(size) if size.isdigit() else 44
        self.panel_size.set_active_id("Small" if size < 40 else "Large" if size > 48 else "Medium")
        layout = xfconf_get("xfwm4", "/general/button_layout")
        self.buttons_side.set_active_id("Left" if layout.endswith("|") and layout != "|" else "Right")
        dpi = xfconf_get("xsettings", "/Xft/DPI")
        dpi = int(dpi) if dpi.lstrip("-").isdigit() else -1
        self.text_size.set_active_id("Small" if 0 < dpi < 92 else "Large" if 100 < dpi < 118 else "Larger" if dpi >= 118 else "Normal")
        self.bigger.set_active(xfconf_get("xsettings", "/Gdk/WindowScalingFactor") == "2")
        self.setting = False

    def set_accent(self, name, colour):
        theme = "XenithOS" if colour == xui.RED else f"XenithOS-{name}"
        if not os.path.isdir(f"/usr/share/themes/{theme}"):
            theme = "XenithOS"
        xui.write_conf(accent=colour)
        xfconf_set("xsettings", "/Net/ThemeName", "string", theme)
        xfconf_set("xfce4-terminal", "/color-cursor", "string", colour)
        # Apps that open in XenithOS Console have no desktop to ask, so they read this file.
        folder = os.path.join(GLib.get_user_config_dir(), "gtk-3.0")
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "settings.ini"), "w") as f:
            f.write(f"[Settings]\ngtk-theme-name={theme}\ngtk-icon-theme-name=XenithOS\ngtk-font-name=Inter 10\n"
                    "gtk-application-prefer-dark-theme=true\n")
        xui.load_css(colour)
        self.refresh()

    def set_wallpaper(self, path):
        xui.set_wallpaper(path)            # (Photos' "Use as wallpaper" does the same)
        self.refresh()

    def choose_picture(self, *args):
        dialog = Gtk.FileChooserDialog(title="Choose a picture", transient_for=self.window, action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons("Not now", Gtk.ResponseType.CANCEL, "Use it", Gtk.ResponseType.OK)
        pictures = Gtk.FileFilter()
        pictures.set_name("Pictures")
        pictures.add_pixbuf_formats()
        dialog.add_filter(pictures)
        folder = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_PICTURES)
        if folder and os.path.isdir(folder):
            dialog.set_current_folder(folder)
        if dialog.run() == Gtk.ResponseType.OK and dialog.get_filename():
            self.set_wallpaper(dialog.get_filename())
        dialog.destroy()

    def set_panel_place(self, where):
        xfconf_set("xfce4-panel", "/panels/panel-1/position", "string", "p=6;x=0;y=0" if where == "Top" else "p=10;x=0;y=0")

    def set_panel_size(self, size):
        px = {"Small": 34, "Medium": 44, "Large": 54}.get(size, 44)
        xfconf_set("xfce4-panel", "/panels/panel-1/size", "uint", px)
        xfconf_set("xfce4-panel", "/panels/panel-1/icon-size", "uint", px // 2)

    def set_buttons_side(self, side):
        xfconf_set("xfwm4", "/general/button_layout", "string", "CMH|" if side == "Left" else "|HMC")
        xfconf_set("xfwm4", "/general/title_alignment", "string", "center" if side == "Left" else "left")

    def set_text_size(self, size):
        xfconf_set("xsettings", "/Xft/DPI", "int", {"Small": 86, "Normal": -1, "Large": 110, "Larger": 125}.get(size, -1))

    def set_bigger(self, switch, state):
        if self.setting:
            return False
        xfconf_set("xsettings", "/Gdk/WindowScalingFactor", "int", 2 if state else 1)
        xfconf_set("xsettings", "/Gtk/CursorThemeSize", "int", 48 if state else 24)
        return False


class StartupPage(Page):
    title, icon = "Start-up", "system-reboot"

    def build(self):
        self.setting = False
        self.mode = Gtk.ComboBoxText()
        self.mode.append("desktop", "The desktop")
        self.mode.append("console", "XenithOS Console")
        self.mode.connect("changed", self.changed)
        self.add(card(setting_row("When the PC starts, open", "XenithOS Console is the full-screen XenithOS, made for a controller",
                                  self.mode)))
        go = button("Return to XenithOS Console", "primary", lambda b: subprocess.Popen(["/usr/bin/xenithos-console"]))
        self.add(card(setting_row("XenithOS Console", "Its power menu, Switch to desktop mode, comes back here", go)))

    def refresh(self):
        self.setting = True
        try:
            mode = open(os.path.join(CONF_DIR, "start-mode")).read().strip()
        except OSError:
            mode = "desktop"
        self.mode.set_active_id("console" if mode == "console" else "desktop")
        self.setting = False

    def changed(self, combo):
        if self.setting:
            return
        os.makedirs(CONF_DIR, exist_ok=True)
        with open(os.path.join(CONF_DIR, "start-mode"), "w") as f:
            f.write((combo.get_active_id() or "desktop") + "\n")


class ConsoleAppsPage(Page):
    title, icon = "Console apps", "applications-games"

    def build(self):
        self.add(label("XenithOS Console's apps that don't come installed. The ones you install show on its home "
                       "screen, and in the Apps menu here on the desktop. Removing one keeps what you made "
                       "with it.", "subtitle"))
        self.rows = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.add(self.rows)
        self.note = label("", "error")
        self.add(self.note)

        # Apps of the Apps menu, on XenithOS Console's home screen too (ownapps.py).
        self.add(label("Your own apps", "heading"))
        self.add(label("Apps on this PC can go on XenithOS Console's home screen as well, and open full screen "
                       "there. In a game the controller plays the game. In any other app it moves the pointer. "
                       "The Xbox button opens the guide, where Controller changes that or makes it a keyboard "
                       "and mouse.", "subtitle"))
        self.find = Gtk.SearchEntry(placeholder_text="Find an app")
        self.find.connect("search-changed", lambda entry: self.own.invalidate_filter())
        self.add(self.find)
        self.own = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
        self.own.get_style_context().add_class("rows")
        self.own.set_filter_func(lambda row: self.find.get_text().strip().lower() in row.words)
        self.add(self.own)
        self.own_note = label("", "error")
        self.add(self.own_note)

    def refresh_own(self):
        """The Apps menu's apps, the ones on the home screen first. The list keeps this order
        while it's looked at: adding one changes its line where it is."""
        on = ownapps.added()
        for child in self.own.get_children():
            self.own.remove(child)
        apps = ownapps.offered()
        listed = {info.get_id() for info in apps}
        # On the home screen, but not an app the list offers: one that has been removed from the
        # PC since (that entry only says so when it's opened), or one added some other way.
        extra = []
        for ident in sorted(set(on) - listed):
            info = ownapps.entry(ident)
            if info is None:
                called = ownapps.read_app(os.path.join(ownapps.apps_dir(), on[ident] + ".app")).get("name") or ident
                extra.append(self.own_row(ident, called, "", None, True, gone=True))
            else:
                apps.append(info)
        apps.sort(key=lambda info: (info.get_id() not in on, (info.get_display_name() or "").lower()))
        for row in extra:
            self.own.add(row)
        for info in apps:
            # The Apps menu's name for it; "Find an app" also knows what its home screen entry is called.
            self.own.add(self.own_row(info.get_id(), info.get_display_name() or info.get_id(),
                                      (info.get_generic_name() or "") + " " + ownapps.shown_name(info),
                                      info.get_icon(), info.get_id() in on))
        if not apps and not extra:
            row = Gtk.ListBoxRow(activatable=False)
            row.words = ""
            row.add(label("No apps were found in the Apps menu.", "small"))
            self.own.add(row)
        self.own.show_all()

    def own_row(self, ident, name, also, icon, there, gone=False):
        """One app's line: its icon and name, whether it's on the home screen, and the button."""
        row = Gtk.ListBoxRow(activatable=False)
        row.ident, row.there, row.gone = ident, there, gone
        row.words = (name + " " + also).lower()
        line = Gtk.Box(spacing=14)
        picture = Gtk.Image.new_from_gicon(icon, Gtk.IconSize.DND) if icon else Gtk.Image.new_from_icon_name(
            "application-x-executable", Gtk.IconSize.DND)
        picture.set_pixel_size(32)
        line.pack_start(picture, False, False, 0)
        text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, valign=Gtk.Align.CENTER)
        text.pack_start(label(name, "row-title"), False, False, 0)
        row.about = label("", "small")
        row.about.set_no_show_all(True)
        text.pack_start(row.about, False, False, 0)
        line.pack_start(text, True, True, 0)
        row.change = button("", None, lambda b: self.change_own(row))
        row.change.set_valign(Gtk.Align.CENTER)
        line.pack_end(row.change, False, False, 0)
        row.add(line)
        self.show_own(row)
        return row

    def show_own(self, row):
        about = ("It isn't on this PC any more." if row.gone else "On XenithOS Console's home screen.") if row.there else ""
        row.about.set_text(about)
        row.about.set_visible(bool(about))
        row.change.set_label("Remove" if row.there else "Add")

    def change_own(self, row):
        self.own_note.set_text("")
        try:
            if row.there:
                ownapps.remove(row.ident)
            else:
                ownapps.add(row.ident)
        except ownapps.Problem as e:
            self.own_note.set_text(str(e))
            return
        row.there = not row.there
        if row.gone:                    # nothing left that could be added again
            self.own.remove(row)
        else:
            self.show_own(row)

    def refresh(self):
        self.refresh_own()
        self.refresh_console()

    def refresh_console(self):
        def work():
            code, out = run([CONSOLE_APPS, "list"])
            return [line.split("|", 3) for line in out.splitlines() if line.count("|") >= 3] if code == 0 else []

        def done(apps):
            for child in self.rows.get_children():
                self.rows.remove(child)
            if not apps:
                self.rows.pack_start(card(label("There's nothing more to install here.", "small")), False, False, 0)
            for app, installed, name, about in apps:
                there = installed == "1"
                change = button("Remove" if there else "Install", None if there else "primary",
                                lambda b, app=app, there=there: self.change(app, there))
                self.rows.pack_start(card(setting_row(name, about + ("\nInstalled." if there else ""), change)),
                                     False, False, 0)
            self.rows.show_all()

        later(work, done)

    def change(self, app, there):
        self.note.set_text("")

        def done(result):
            code, out = result
            if code != 0:
                self.note.set_text(out.splitlines()[-1] if out else "That didn't work.")
            self.refresh_console()

        later(lambda: run([CONSOLE_APPS, "remove" if there else "install", app]), done)


class SecurityPage(Page):
    title, icon = "Security", "security-high"

    def build(self):
        self.setting = False
        self.firewall = Gtk.Switch()
        self.firewall.connect("state-set", lambda s, state: self.helper("firewall", state))
        self.mac = Gtk.Switch()
        self.mac.connect("state-set", lambda s, state: self.helper("wifi-address", state))
        self.note = label("", "error")
        self.add(card(setting_row("Firewall", "Turns away connections from other devices that this PC didn't ask for. "
                                  "Leave it off to host a game server for others on your network.", self.firewall),
                      setting_row("Hide this PC's Wi-Fi address", "Uses a different made-up hardware address on each Wi-Fi "
                                  "network, so the PC is harder to follow from place to place", self.mac),
                      self.note))
        self.vpn = label("", "small")
        vpn_row = setting_row("Mullvad VPN", None, button("Open Mullvad VPN", None, lambda b: self.launch("mullvad-vpn")))
        self.add(card(vpn_row, self.vpn))

    def launch(self, program):
        path = shutil.which(program) or ("/opt/Mullvad VPN/mullvad-vpn" if program == "mullvad-vpn" else None)
        if path and os.path.exists(path):
            subprocess.Popen([path])
        else:
            self.note.set_text(f"{program} isn't installed. Settings > Updates brings it.")

    def refresh(self):
        def work():
            _, status = run([SECURITY_HELPER, "status"])
            if not have("mullvad"):
                vpn = "Mullvad VPN isn't installed."
            else:
                code, out = run(["mullvad", "status"])
                vpn = out.splitlines()[0] if code == 0 and out else "Mullvad VPN's service isn't running."
            return status, vpn

        def done(result):
            status, vpn = result
            self.setting = True
            self.firewall.set_active("firewall=on" in status)
            self.mac.set_active("wifi-address=random" in status)
            self.firewall.set_sensitive("firewall=" in status)
            self.setting = False
            self.vpn.set_text(vpn)

        later(work, done)

    def helper(self, what, state):
        if self.setting:
            return False
        self.note.set_text("")

        def done(result):
            code, out = result
            if code != 0:
                self.note.set_text("That needs the PC's password, and it wasn't given." if code in (126, 127)
                                   else (out.splitlines()[-1] if out else "That didn't work."))
            self.refresh()

        later(lambda: run(["pkexec", SECURITY_HELPER, what, "on" if state else "off"], timeout=120), done)
        return False


class UpdatesPage(Page):
    title, icon = "Updates", "system-software-update"

    def build(self):
        self.headline = label("", "heading")
        self.details = label("", "subtitle")
        self.progress = Gtk.ProgressBar()
        self.step = label("", "small")
        self.check_button = button("Look for updates", None, lambda b: self.check())
        self.update_button = button("Update now", "primary", lambda b: self.apply())
        self.restart_button = button("Restart now", "primary", lambda b: subprocess.Popen(["systemctl", "reboot"]))
        row = Gtk.Box(spacing=10)
        for b in (self.check_button, self.update_button, self.restart_button):
            row.pack_start(b, False, False, 0)
        self.add(card(self.headline, self.details, self.progress, self.step, row))
        self.add(label("One button updates XenithOS Desktop, XenithOS Console, your apps (Brave, Mullvad VPN and the rest) "
                       "and the system underneath. Security fixes also install themselves once a day.", "small"))
        self.working = False
        self.info = {}
        self.console = None
        self.shown_once = False

    def refresh(self):
        if self.working:
            return
        self.read_status()
        self.render()
        if not self.shown_once:
            self.shown_once = True
            later(consoleupdate.check, self.console_checked)

    def console_checked(self, result):
        self.console = result
        if not self.working:
            self.render()

    def read_status(self):
        self.info = {}
        try:
            for line in open(STATUS):
                k, _, v = line.strip().partition("=")
                self.info[k] = v
        except OSError:
            pass
        # Whether a restart is still needed is asked now, not read from the last check: the
        # marks are gone once the PC has restarted.
        self.info["restart"] = "1" if any(os.path.exists(p) for p in RESTART_MARKS) else "0"

    def pending(self):
        i = self.info
        desktop = i.get("desktop_available") and i.get("desktop_available") != i.get("desktop_installed")
        packages = int(i.get("packages") or 0) if (i.get("packages") or "0").isdigit() else 0
        console = bool(self.console and self.console.get("available") and self.console.get("newer"))
        return desktop, packages, console

    def render(self):
        desktop, packages, console = self.pending()
        i = self.info
        version = i.get("desktop_installed") or os_version() or "?"
        lines = []
        if desktop:
            lines.append(f"XenithOS Desktop {i['desktop_available']}" + (f": {i['notes']}" if i.get("notes") else ""))
        if console:
            lines.append(f"XenithOS Console {self.console['available']}")
        if packages:
            lines.append(f"{packages} update{'s' if packages != 1 else ''} for apps and the system")
        restart = i.get("restart") == "1"
        if restart and not lines:
            self.headline.set_text("Restart to finish updating")
        elif lines:
            self.headline.set_text("Updates are ready")
        elif "checked" in i:
            self.headline.set_text(f"XenithOS Desktop {version} is up to date")
        else:
            self.headline.set_text(f"XenithOS Desktop {version}")
        when = ""
        if i.get("checked", "").isdigit():
            ago = int(time.time()) - int(i["checked"])
            when = "Checked just now." if ago < 120 else f"Checked {ago // 60} minutes ago." if ago < 7200 else \
                   f"Checked {ago // 3600} hours ago." if ago < 172800 else f"Checked {ago // 86400} days ago."
        text = "\n".join(lines) if lines else (when or "Look for updates to see what's new.")
        if lines and when:
            text += "\n" + when
        if i.get("error"):
            text += "\n" + i["error"]
        self.details.set_text(text)
        self.progress.set_visible(False)
        self.step.set_text("")
        self.check_button.set_sensitive(True)
        self.update_button.set_visible(bool(lines))
        self.update_button.set_sensitive(True)
        self.restart_button.set_visible(restart)

    def helper(self, action, on_line, on_done):
        """Runs the root helper, feeding its lines to on_line(kind, text)."""
        try:
            # Away from any terminal Settings was opened from: the helper would otherwise be a
            # background program of that terminal, which gets stopped when it touches it.
            proc = subprocess.Popen(["sudo", "-n", HELPER, action], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, bufsize=1, start_new_session=True)
        except OSError as e:
            on_done(1, str(e))
            return
        state = {"error": ""}

        def read(stream, condition):
            line = stream.readline()
            if line:
                kind, _, rest = line.strip().partition(" ")
                if kind == "ERROR":
                    state["error"] = rest
                elif kind not in ("STEP", "INFO", "DONE") and line.strip():
                    state["error"] = line.strip()            # sudo's own words, say
                on_line(kind, rest)
                return True
            on_done(proc.wait(), state["error"])
            return False

        GLib.io_add_watch(proc.stdout, GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, read)

    def busy(self, text):
        self.working = True
        self.check_button.set_sensitive(False)
        self.update_button.set_sensitive(False)
        self.restart_button.set_visible(False)
        self.progress.set_visible(True)
        self.progress.set_fraction(0.0)
        self.step.set_text(text)

    def check(self):
        self.busy("Looking for updates")
        later(consoleupdate.check, self.console_checked)

        def line(kind, rest):
            if kind == "STEP":
                pct, _, text = rest.partition(" ")
                if pct.isdigit():
                    self.progress.set_fraction(int(pct) / 100)

        def done(code, error):
            self.working = False
            self.read_status()
            if code != 0 and error:
                self.info["error"] = error
            self.render()

        self.helper("check", line, done)

    def apply(self):
        self.busy("Getting ready")
        self.headline.set_text("Updating")
        self.details.set_text("Keep the PC on until it's done. You can keep using it.")
        _, _, console = self.pending()

        def line(kind, rest):
            if kind == "STEP":
                pct, _, text = rest.partition(" ")
                if pct.isdigit():
                    self.progress.set_fraction(int(pct) / 100 * (0.8 if console else 1.0))
                self.step.set_text(text)

        def system_done(code, error):
            if code != 0:
                return self.finish(error or "The update didn't finish.")
            if not console:
                return self.finish("")
            self.step.set_text(f"Building XenithOS Console {self.console['available']} (a minute or two)")
            self.progress.set_fraction(0.82)
            later(lambda: consoleupdate.update(self.console), lambda problem: self.finish(problem or ""))

        self.helper("apply", line, system_done)

    def finish(self, problem):
        self.working = False
        self.read_status()
        self.console = None
        later(consoleupdate.check, self.console_checked)
        self.render()
        if problem:
            self.headline.set_text("The update didn't finish")
            self.details.set_text(problem + "\nThe PC still works as before. Try again in a while.")
        elif self.info.get("restart") == "1":
            self.headline.set_text("Restart to finish updating")
            self.details.set_text("Everything is installed. Some of it starts working after a restart.")


class AboutPage(Page):
    title, icon = "About", "help-about"

    def build(self):
        top = Gtk.Box(spacing=22)
        top.pack_start(xui.logo(110), False, False, 0)
        names = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4, valign=Gtk.Align.CENTER)
        names.pack_start(label("XenithOS Desktop", "title"), False, False, 0)
        self.version = label("", "subtitle")
        names.pack_start(self.version, False, False, 0)
        top.pack_start(names, True, True, 0)
        self.add(top)
        self.grid = Gtk.Grid(column_spacing=24, row_spacing=10)
        self.add(card(self.grid))
        row = Gtk.Box(spacing=10)
        row.pack_start(button("XenithOS website", None, lambda b: self.open("https://owencosta.diy/xenithos/")), False, False, 0)
        row.pack_start(button("Source code", None, lambda b: self.open("https://github.com/OwenCosta/XenithOS")), False, False, 0)
        self.add(row)
        self.add(label("XenithOS Desktop is free software, built on Ubuntu 24.04 with the XFCE desktop. "
                       "The apps that come with it are their makers' own.", "small"))

    def open(self, url):
        subprocess.Popen(["xdg-open", url], stderr=subprocess.DEVNULL)

    def refresh(self):
        def work():
            facts = []
            _, console = run(["/usr/lib/xenithos-desktop/console-version"])
            facts.append(("XenithOS Console", console or "not installed"))
            try:
                facts.append(("Computer's name", open("/etc/hostname").read().strip()))
            except OSError:
                pass
            try:
                cpu = next(l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name"))
                facts.append(("Processor", re.sub(r"\s+", " ", cpu)))
            except (OSError, StopIteration):
                pass
            try:
                kb = int(next(l.split()[1] for l in open("/proc/meminfo") if l.startswith("MemTotal")))
                facts.append(("Memory", f"{kb / 1048576:.1f} GB"))
            except (OSError, StopIteration, ValueError):
                pass
            _, pci = run(["lspci"])
            for l in pci.splitlines():
                if re.search(r"VGA compatible|3D controller|Display controller", l):
                    facts.append(("Graphics", re.sub(r" \(rev \w+\)$", "", l.split(": ", 1)[-1])))
            try:
                total, _, free = shutil.disk_usage("/")
                facts.append(("Drive", f"{free / 1e9:.0f} GB free of {total / 1e9:.0f} GB"))
            except OSError:
                pass
            _, kernel = run(["uname", "-r"])
            facts.append(("Linux", kernel))
            return facts

        def done(facts):
            self.version.set_text(f"Version {os_version() or '?'}")
            for child in self.grid.get_children():
                self.grid.remove(child)
            for i, (name, value) in enumerate(facts):
                self.grid.attach(label(name, "row-title", wrap=False), 0, i, 1, 1)
                fact = label(value if len(value) <= 70 else value[:68] + "...", "value", wrap=False, selectable=True)
                self.grid.attach(fact, 1, i, 1, 1)
            self.grid.show_all()

        later(work, done)


PAGES = [("wifi", WifiPage), ("sound", SoundPage), ("look", LookPage), ("startup", StartupPage),
         ("apps", ConsoleAppsPage), ("security", SecurityPage), ("updates", UpdatesPage), ("about", AboutPage)]


class Settings(Gtk.Window):
    def __init__(self, first):
        super().__init__(title="Settings")
        self.get_style_context().add_class("xenith")
        self.set_default_size(980, 700)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_icon_name("preferences-system")
        self.connect("destroy", Gtk.main_quit)
        outer = Gtk.Box()
        self.add(outer)

        side = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        side.get_style_context().add_class("sidebar")
        side.set_size_request(220, -1)
        head = label("Settings", "title")
        head.set_margin_top(22)
        head.set_margin_start(22)
        head.set_margin_bottom(14)
        side.pack_start(head, False, False, 0)
        self.menu = Gtk.ListBox(selection_mode=Gtk.SelectionMode.SINGLE)
        self.menu.connect("row-selected", self.chosen)
        side.pack_start(self.menu, True, True, 0)
        outer.pack_start(side, False, False, 0)

        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=120)
        self.pages = {}
        for key, cls in PAGES:
            page = cls(self)
            self.pages[key] = page
            holder = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
            holder.get_style_context().add_class("page")
            holder.set_border_width(30)
            holder.pack_start(label(page.title, "title"), False, False, 0)
            holder.pack_start(page.box, False, False, 6)
            scroller = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
            scroller.add(holder)
            self.stack.add_named(scroller, key)
            row = Gtk.ListBoxRow()
            line = Gtk.Box(spacing=12)
            line.pack_start(Gtk.Image.new_from_icon_name(page.icon, Gtk.IconSize.LARGE_TOOLBAR), False, False, 0)
            line.pack_start(label(page.title, wrap=False), True, True, 0)
            row.add(line)
            row.key = key
            self.menu.add(row)
        outer.pack_start(self.stack, True, True, 0)
        self.show_all()
        keys = [k for k, _ in PAGES]
        self.menu.select_row(self.menu.get_row_at_index(keys.index(first) if first in keys else 0))

    def chosen(self, menu, row):
        if row is None:
            return
        self.stack.set_visible_child_name(row.key)
        try:
            self.pages[row.key].show()
        except Exception as e:                                  # one page's trouble doesn't take Settings down
            print(f"settings: {row.key}: {e}", file=sys.stderr)


def notify_updates():
    """At desktop start: one notification when updates are ready (once a day at most)."""
    info = {}
    try:
        for line in open(STATUS):
            k, _, v = line.strip().partition("=")
            info[k] = v
    except OSError:
        return
    ready = (info.get("desktop_available") and info.get("desktop_available") != info.get("desktop_installed")) or \
            (info.get("packages") or "0") not in ("0", "")
    stamp = os.path.join(CONF_DIR, "update-notified")
    try:
        if time.time() - os.path.getmtime(stamp) < 72000:
            return
    except OSError:
        pass
    if not ready or not have("notify-send"):
        return
    os.makedirs(CONF_DIR, exist_ok=True)
    open(stamp, "w").close()
    code, out = run(["notify-send", "-a", "XenithOS Desktop", "-i", "system-software-update", "-A", "open=Update",
                     "Updates are ready", "XenithOS Desktop has updates to install."], timeout=600)
    if out.strip() == "open":
        subprocess.Popen(["/usr/bin/xenithos-settings", "--page", "updates"])


def main():
    args = sys.argv[1:]
    if "--notify-updates" in args:
        return notify_updates()
    first = args[args.index("--page") + 1] if "--page" in args and args.index("--page") + 1 < len(args) else "wifi"
    xui.load_css()
    Settings(first)
    Gtk.main()


if __name__ == "__main__":
    main()
