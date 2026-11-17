#!/usr/bin/env python3
"""Tube, the desktop version of XenithOS Console's Tube: YouTube through XenithOS Cloud.

    tube.py

Search, pick a video and it plays in the window. Space pauses, Left and Right skip 10 seconds,
F11 fills the screen and Esc goes back. It signs in with the XenithOS profile XenithOS Console
uses (Tube needs one), through the same XenithOS Cloud: the address is in XenithOS Console's
uda:/xenith/config.txt and the profile in uda:/xenith/profile.txt.

XenithOS Cloud does the YouTube part, as for the console (shared there with yt-dlp and ffmpeg):
POST /v1/tube/search and /v1/tube/start, GET /v1/tube/thumb/<id>.jpg, /v1/tube/chunk and
/v1/tube/stop (server/xenith_cloud.py). The stream it sends is fed to GStreamer's playbin,
which works out what it is. Tube asks for a stream made for a PC (q=pc); a Cloud that doesn't
make those sends the console's (426x240 pictures), which plays the same way.
"""
import os
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xui  # noqa: E402
from xui import Gdk, GdkPixbuf, GLib, Gtk, button, label  # noqa: E402

import gi  # noqa: E402

gi.require_version("Pango", "1.0")
from gi.repository import Pango  # noqa: E402

try:
    gi.require_version("Gst", "1.0")
    from gi.repository import Gst  # noqa: E402
    Gst.init(None)
    PLAYER_PARTS = all(Gst.ElementFactory.find(e) for e in ("playbin", "gtksink", "avidemux", "jpegdec"))
except (ValueError, ImportError):
    Gst = None
    PLAYER_PARTS = False

CARD_W, CARD_H = 288, 162
SUGGESTIONS = ("Music", "Speedruns", "Minecraft", "Retro games", "Space", "Xbox 360")

EXTRA_CSS = """
.xenith flowbox { background-color: transparent; }
.xenith flowboxchild { padding: 6px; border-radius: 14px; background-color: transparent; }
.xenith flowboxchild:hover, .xenith flowboxchild:focus { background-color: #15181d; }
.xenith .video { background-color: #000000; }
.xenith .controls { background-color: rgba(7, 8, 10, 0.92); padding: 10px 16px; }
.xenith button.icon { background-image: none; background-color: transparent; border: 1px solid transparent;
                      border-radius: 10px; padding: 6px 9px; box-shadow: none; color: #f1f3f6; }
.xenith button.icon:hover { background-color: #1b1f26; border-color: #262b33; }
.xenith button.chip { background-image: none; background-color: #12151a; color: #f1f3f6; border: 1px solid #262b33;
                      border-radius: 20px; padding: 8px 18px; font-weight: 600; box-shadow: none; }
.xenith button.chip:hover { background-color: #1b1f26; }
.xenith .clock { color: #c4c9d1; font-size: 13px; font-weight: 600; font-feature-settings: "tnum"; }
"""


# ------------------------------------------------------------------ XenithOS Cloud

def data_dir():
    return os.environ.get("XENITHOS_DATA") or os.path.expanduser("~/.local/share/xenithos")


def read_kv(name):
    found = {}
    try:
        with open(os.path.join(data_dir(), "uda:", "xenith", name), encoding="utf-8", errors="replace") as f:
            for line in f:
                key, sep, value = line.strip().partition("=")
                if sep and not key.startswith("#"):
                    found[key] = value
    except OSError:
        pass
    return found


def addresses():
    """Where XenithOS Cloud is: its address on this network first, when XenithOS Console found
    it there (route.txt), then its own."""
    main = read_kv("config.txt").get("server", "").strip()
    route = read_kv("route.txt")
    out = []
    if main and route.get("for") == main and route.get("lan") and route.get("id"):
        out.append(route["lan"])
    if main:
        out.append(main)
    return out


def kv(body, key):
    for line in body.splitlines():
        k, sep, v = line.partition("=")
        if sep and k == key:
            return v
    return ""


class Cloud:
    def __init__(self):
        self.servers = addresses()
        profile = read_kv("profile.txt")
        self.name = profile.get("name", "")
        self.token = profile.get("token", "")

    def ask(self, method, path, fields=None, timeout=20):
        """(status, body bytes); status 0 when no address answered."""
        data = urllib.parse.urlencode(fields).encode() if fields is not None else None
        for i, server in enumerate(self.servers):
            req = urllib.request.Request(f"http://{server}{path}", data=data, method=method)
            req.add_header("User-Agent", "XenithOS-Tube-Desktop")
            try:
                with urllib.request.urlopen(req, timeout=timeout) as r:
                    if i:                   # the network route didn't answer: stay with this one
                        self.servers = self.servers[i:]
                    return r.status, r.read()
            except urllib.error.HTTPError as e:
                return e.code, e.read()
            except (urllib.error.URLError, OSError):
                continue
        return 0, b""

    @staticmethod
    def why(status, body):
        message = kv(body.decode("utf-8", "replace"), "error")
        if message:
            return message
        if status == 0:
            return "XenithOS Cloud didn't answer. Is this PC online?"
        return f"XenithOS Cloud answered with error {status}."


def clock(seconds):
    s = max(0, int(seconds))
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60}:{s % 60:02d}"


def work_area():
    """The screen's room for windows (without the panel)."""
    display = Gdk.Display.get_default()
    monitor = display.get_primary_monitor() or display.get_monitor(0)
    return monitor.get_workarea()


def rounded(cr, x, y, w, h, r):
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -1.5708, 0)
    cr.arc(x + w - r, y + h - r, r, 0, 1.5708)
    cr.arc(x + r, y + h - r, r, 1.5708, 3.1416)
    cr.arc(x + r, y + r, r, 3.1416, 4.7124)
    cr.close_path()


class Thumb(Gtk.DrawingArea):
    """A result's picture, rounded, with its length in the corner."""

    def __init__(self, length):
        super().__init__()
        self.length = length
        self.pb = None
        self.set_size_request(CARD_W, CARD_H)
        self.connect("draw", self.on_draw)

    def on_draw(self, area, cr):
        w, h = CARD_W, CARD_H
        rounded(cr, 0, 0, w, h, 10)
        cr.clip()
        cr.set_source_rgb(0x1b / 255, 0x1f / 255, 0x26 / 255)
        cr.paint()
        if self.pb:
            Gdk.cairo_set_source_pixbuf(cr, self.pb, 0, 0)
            cr.paint()
        if self.length:
            cr.select_font_face("Inter", 0, 1)
            cr.set_font_size(12)
            ext = cr.text_extents(self.length)
            bw, bh = ext.width + 14, 22
            rounded(cr, w - bw - 8, h - bh - 8, bw, bh, 6)
            cr.set_source_rgba(0, 0, 0, 0.8)
            cr.fill()
            cr.set_source_rgb(1, 1, 1)
            cr.move_to(w - bw - 8 + 7 - ext.x_bearing, h - 8 - bh / 2 - ext.y_bearing - ext.height / 2)
            cr.show_text(self.length)


def cover(pb, w, h):
    sw, sh = pb.get_width(), pb.get_height()
    s = max(w / sw, h / sh)
    nw, nh = max(w, round(sw * s)), max(h, round(sh * s))
    scaled = pb.scale_simple(nw, nh, GdkPixbuf.InterpType.BILINEAR)
    return scaled.new_subpixbuf((nw - w) // 2, (nh - h) // 2, w, h).copy()


# ------------------------------------------------------------------ the window

class Tube(Gtk.Window):
    def __init__(self):
        super().__init__(title="Tube")
        self.get_style_context().add_class("xenith")
        self.set_icon_name("xenithos-app-tube")
        room = work_area()
        self.set_default_size(min(1280, int(room.width * 0.85)), min(800, int(room.height * 0.85)))
        self.connect("destroy", self.on_destroy)
        self.connect("key-press-event", self.on_key)

        self.cloud = Cloud()
        self.workers = ThreadPoolExecutor(max_workers=4)
        self.results = []
        self.searching = 0
        # The player
        self.player = self.src = None
        self.generation = 0             # each stream has its own; older feeders stop
        self.sid = ""
        self.video = None               # the result playing
        self.start_at = 0               # the second of the video the stream begins at
        self.duration = 0
        self.started_at = 0.0
        self.paused = False
        self.ended = False
        self.ticker = 0
        self.hide_timer = 0
        self.dragging_seek = False

        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=120)
        self.add(self.stack)
        if not PLAYER_PARTS:
            self.stack.add_named(self.notice("Tube needs one more update",
                                             "This PC doesn't have the video player parts Tube uses yet. "
                                             "Settings > Updates brings them.",
                                             "Open Settings", lambda b: self.run(["xenithos-settings", "--page", "updates"])),
                                 "notice")
        elif not self.cloud.servers:
            self.stack.add_named(self.notice("XenithOS Cloud isn't set up",
                                             "Tube plays YouTube through XenithOS Cloud, and this PC doesn't know its "
                                             "address. XenithOS Console's Settings shows what it uses."), "notice")
        elif not self.cloud.token:
            self.stack.add_named(self.notice("Sign in first",
                                             "Tube uses your XenithOS profile. Sign in on XenithOS Console (Profile), "
                                             "then open Tube again.",
                                             "Open XenithOS Console", lambda b: self.run(["xenithos-console"])),
                                 "notice")
        else:
            self.stack.add_named(self.build_search(), "search")
            self.stack.add_named(self.build_player(), "player")
        self.show_all()
        if self.stack.get_child_by_name("search"):
            self.entry.grab_focus()

    def notice(self, title, text, action=None, handler=None):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, valign=Gtk.Align.CENTER,
                      halign=Gtk.Align.CENTER)
        box.get_style_context().add_class("page")
        box.set_size_request(460, -1)
        box.pack_start(label(title, "title", xalign=0.5), False, False, 0)
        box.pack_start(label(text, "subtitle", xalign=0.5), False, False, 0)
        if action:
            b = button(action, "primary", handler)
            b.set_halign(Gtk.Align.CENTER)
            box.pack_start(b, False, False, 12)
        return box

    @staticmethod
    def run(cmd):
        try:
            import subprocess
            subprocess.Popen(cmd, start_new_session=True)
        except OSError:
            pass

    # ------------------------------------------------------------------ searching

    def build_search(self):
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        page.get_style_context().add_class("page")
        page.set_border_width(28)
        top = Gtk.Box(spacing=12)
        words = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        words.pack_start(label("Tube", "title"), False, False, 0)
        words.pack_start(label("YouTube through XenithOS Cloud", "subtitle"), False, False, 0)
        top.pack_start(words, True, True, 0)
        who = label(f"Signed in as {self.cloud.name}" if self.cloud.name else "", "small", wrap=False)
        who.set_valign(Gtk.Align.CENTER)
        top.pack_end(who, False, False, 0)
        page.pack_start(top, False, False, 0)

        bar = Gtk.Box(spacing=10)
        self.entry = Gtk.SearchEntry(placeholder_text="Search YouTube")
        self.entry.connect("activate", lambda e: self.search(e.get_text()))
        bar.pack_start(self.entry, True, True, 0)
        bar.pack_start(button("Search", "primary", lambda b: self.search(self.entry.get_text())), False, False, 0)
        page.pack_start(bar, False, False, 0)
        self.status = label("", "subtitle")
        page.pack_start(self.status, False, False, 0)

        self.chips = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, max_children_per_line=10, column_spacing=8,
                                 valign=Gtk.Align.START)
        for s in SUGGESTIONS:
            b = Gtk.Button(label=s)
            b.get_style_context().add_class("chip")
            b.connect("clicked", lambda w, s=s: (self.entry.set_text(s), self.search(s)))
            self.chips.add(b)
        page.pack_start(self.chips, False, False, 0)

        self.grid = Gtk.FlowBox(homogeneous=True, selection_mode=Gtk.SelectionMode.NONE, valign=Gtk.Align.START,
                                column_spacing=6, row_spacing=6, max_children_per_line=12,
                                activate_on_single_click=True)
        self.grid.connect("child-activated", lambda box, child: self.play(self.results[child.get_index()]))
        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroll.add(self.grid)
        page.pack_start(scroll, True, True, 0)
        return page

    def search(self, q):
        q = q.strip()
        if not q:
            return
        self.searching += 1
        mine = self.searching
        self.status.set_text(f"Looking for {q}")

        def work():
            return self.cloud.ask("POST", "/v1/tube/search", {"token": self.cloud.token, "q": q})

        def done(result):
            if mine != self.searching:
                return
            status, body = result
            if status != 200:
                self.status.set_text("The search didn't work. " + Cloud.why(status, body))
                return
            found = []
            for line in body.decode("utf-8", "replace").splitlines():
                f = line.split("|")
                if len(f) == 4 and len(f[0]) == 11:
                    found.append({"id": f[0], "title": f[1], "channel": f[2],
                                  "seconds": int(f[3]) if f[3].isdigit() else 0})
            self.show_results(q, found)

        xui.later(work, done)

    def show_results(self, q, found):
        self.results = found
        for child in self.grid.get_children():
            self.grid.remove(child)
        self.chips.set_visible(not found)
        self.status.set_text(f"{len(found)} videos for {q}" if found else f"Nothing found for {q}. Try different words.")
        for it in found:
            card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            thumb = Thumb(clock(it["seconds"]) if it["seconds"] else "")
            card.pack_start(thumb, False, False, 0)
            title = label(it["title"], "row-title")
            title.set_lines(2)
            title.set_ellipsize(Pango.EllipsizeMode.END)
            title.set_max_width_chars(30)
            title.set_size_request(CARD_W, -1)
            card.pack_start(title, False, False, 0)
            chan = label(it["channel"], "small", wrap=False)
            chan.set_ellipsize(Pango.EllipsizeMode.END)
            chan.set_max_width_chars(30)
            card.pack_start(chan, False, False, 0)
            card.set_tooltip_text(it["title"])
            self.grid.add(card)
            self.workers.submit(self.fetch_thumb, it["id"], thumb, self.searching)
        self.grid.show_all()

    def fetch_thumb(self, vid, thumb, mine):
        if mine != self.searching:
            return
        status, body = self.cloud.ask("GET", f"/v1/tube/thumb/{vid}.jpg", timeout=15)
        if status != 200 or not body:
            return
        try:
            loader = GdkPixbuf.PixbufLoader()
            loader.write(body)
            loader.close()
            pb = cover(loader.get_pixbuf(), CARD_W, CARD_H)
        except GLib.Error:
            return
        GLib.idle_add(self.set_thumb, thumb, pb)

    @staticmethod
    def set_thumb(thumb, pb):
        thumb.pb = pb
        thumb.queue_draw()
        return False

    # ------------------------------------------------------------------ playing

    def build_player(self):
        self.player = Gst.ElementFactory.make("playbin", "player")
        sink = Gst.ElementFactory.make("gtksink", None)
        self.player.set_property("video-sink", sink)
        self.player.connect("source-setup", self.on_source)
        bus = self.player.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.on_message)

        overlay = Gtk.Overlay()
        overlay.get_style_context().add_class("video")
        video = sink.props.widget
        video.get_style_context().add_class("video")
        events = Gtk.EventBox()
        events.add(video)
        events.add_events(Gdk.EventMask.POINTER_MOTION_MASK | Gdk.EventMask.BUTTON_PRESS_MASK)
        events.connect("motion-notify-event", lambda w, e: self.show_controls())
        events.connect("button-press-event", self.on_video_click)
        overlay.add(events)
        # Black over the picture while a video starts, so the last one's last frame doesn't show.
        self.curtain = Gtk.Box()
        self.curtain.get_style_context().add_class("video")
        self.curtain.set_no_show_all(True)
        overlay.add_overlay(self.curtain)
        overlay.set_overlay_pass_through(self.curtain, True)

        self.controls = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, valign=Gtk.Align.END)
        self.controls.get_style_context().add_class("controls")
        self.seek = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1, 1)
        self.seek.set_draw_value(False)
        self.seek.connect("button-press-event", lambda w, e: setattr(self, "dragging_seek", True) or False)
        self.seek.connect("button-release-event", self.on_seek_release)
        self.controls.pack_start(self.seek, False, False, 0)
        row = Gtk.Box(spacing=6)

        def icon(name, tip, action):
            b = Gtk.Button.new_from_icon_name(name, Gtk.IconSize.LARGE_TOOLBAR)
            b.get_style_context().add_class("icon")
            b.set_tooltip_text(tip)
            b.connect("clicked", lambda w: action())
            return b

        row.pack_start(icon("go-previous-symbolic", "Back to the videos (Esc)", self.stop), False, False, 0)
        self.pause_b = icon("media-playback-pause-symbolic", "Pause (Space)", self.toggle_pause)
        row.pack_start(self.pause_b, False, False, 0)
        row.pack_start(icon("media-seek-backward-symbolic", "10 seconds back (Left)", lambda: self.skip(-10)), False, False, 0)
        row.pack_start(icon("media-seek-forward-symbolic", "10 seconds on (Right)", lambda: self.skip(10)), False, False, 0)
        self.clock = label("", "clock", wrap=False)
        row.pack_start(self.clock, False, False, 8)
        self.now_title = label("", "row-title", wrap=False)
        self.now_title.set_ellipsize(Pango.EllipsizeMode.END)
        row.pack_start(self.now_title, True, True, 8)
        self.state = label("", "small", wrap=False)
        row.pack_start(self.state, False, False, 8)
        row.pack_end(icon("view-fullscreen-symbolic", "Fill the screen (F11)", self.toggle_full), False, False, 0)
        self.controls.pack_start(row, False, False, 0)
        overlay.add_overlay(self.controls)
        return overlay

    def play(self, it, at=0):
        """Asks XenithOS Cloud for a stream of it from second at, and plays it."""
        self.stop_stream()
        self.video = it
        self.start_at = max(0, int(at))
        self.duration = it.get("seconds", 0)
        self.ended = False
        self.paused = False
        self.started_at = time.monotonic()
        self.pause_icon()
        self.curtain.show()
        self.now_title.set_text(it["title"])
        self.state.set_text("Starting")
        self.clock.set_text(clock(self.start_at) + (" / " + clock(self.duration) if self.duration else ""))
        self.seek.set_range(0, max(1, self.duration))
        self.seek.set_value(self.start_at)
        self.stack.set_visible_child_name("player")
        self.show_controls()
        self.generation += 1
        mine = self.generation

        def work():
            return self.cloud.ask("POST", "/v1/tube/start",
                                  {"token": self.cloud.token, "id": it["id"], "t": str(self.start_at), "q": "pc"})

        def done(result):
            if mine != self.generation:
                return
            status, body = result
            text = body.decode("utf-8", "replace")
            if status != 200 or not kv(text, "session"):
                self.failed("This video won't play. " + Cloud.why(status, body))
                return
            self.sid = kv(text, "session")
            if kv(text, "duration").isdigit() and int(kv(text, "duration")) > 0:
                self.duration = int(kv(text, "duration"))
                self.seek.set_range(0, self.duration)
            self.player.set_property("uri", "appsrc://")
            self.player.set_state(Gst.State.PLAYING)
            if not self.ticker:
                self.ticker = GLib.timeout_add(250, self.tick)

        xui.later(work, done)

    def on_source(self, playbin, source):
        """playbin's appsrc: fed from XenithOS Cloud by a thread of its own."""
        source.set_property("format", Gst.Format.BYTES)
        source.set_property("max-bytes", 8 * 1024 * 1024)
        source.set_property("block", True)
        threading.Thread(target=self.feed, args=(source, self.sid, self.generation), daemon=True).start()

    def feed(self, src, sid, mine):
        off, misses = 0, 0
        while mine == self.generation:
            status, body = self.cloud.ask("GET", f"/v1/tube/chunk?s={sid}&off={off}", timeout=30)
            if mine != self.generation:
                break
            if status == 200 and body:
                misses = 0
                if src.emit("push-buffer", Gst.Buffer.new_wrapped(body)) != Gst.FlowReturn.OK:
                    break                    # the player stopped
                off += len(body)
            elif status == 204:
                time.sleep(0.25)             # not made yet
            elif status == 410:
                src.emit("end-of-stream")
                break
            elif status == 503 or (status and status != 200):
                GLib.idle_add(self.failed, "This video won't play. " + Cloud.why(status, body), mine)
                break
            else:
                misses += 1
                if misses >= 5:
                    GLib.idle_add(self.failed, "The connection to XenithOS Cloud was lost.", mine)
                    break
                time.sleep(1)

    def on_message(self, bus, msg):
        if msg.type == Gst.MessageType.EOS:
            self.ended = True
            self.state.set_text("The end")
            self.pause_b.set_sensitive(False)
            self.show_controls(keep=True)
        elif msg.type == Gst.MessageType.ERROR:
            err, _ = msg.parse_error()
            self.failed("It stopped playing: " + err.message)
        elif msg.type == Gst.MessageType.BUFFERING:
            percent = msg.parse_buffering()
            self.state.set_text("" if percent >= 100 else f"Loading {percent}%")

    def failed(self, why, mine=None):
        if mine is not None and mine != self.generation:
            return False
        self.stop_stream()
        self.state.set_text("")
        xui.ask("That didn't work", why, "OK", "", parent=self)
        self.stack.set_visible_child_name("search")
        return False

    def position(self):
        if self.player is None:
            return self.start_at
        ok, ns = self.player.query_position(Gst.Format.TIME)
        return self.start_at + (ns / Gst.SECOND if ok and ns > 0 else 0)

    def tick(self):
        if self.stack.get_visible_child_name() != "player":
            self.ticker = 0
            return False
        pos = self.position()
        if not self.dragging_seek:
            self.seek.set_value(min(pos, max(1, self.duration)))
        self.clock.set_text(clock(pos) + (" / " + clock(self.duration) if self.duration else ""))
        if self.state.get_text() == "Starting":
            if pos > self.start_at:
                self.state.set_text("")
                self.curtain.hide()
            elif time.monotonic() - self.started_at > 20:
                self.ticker = 0
                self.failed("It didn't start playing. Try it again in a moment.")
                return False
        return True

    def toggle_pause(self):
        if self.player is None or self.ended:
            return
        self.paused = not self.paused
        self.player.set_state(Gst.State.PAUSED if self.paused else Gst.State.PLAYING)
        self.pause_icon()
        self.show_controls(keep=self.paused)

    def pause_icon(self):
        self.pause_b.set_image(Gtk.Image.new_from_icon_name(
            "media-playback-start-symbolic" if self.paused else "media-playback-pause-symbolic", Gtk.IconSize.LARGE_TOOLBAR))
        self.pause_b.set_tooltip_text("Play (Space)" if self.paused else "Pause (Space)")

    def skip(self, seconds):
        if self.video is None:
            return
        target = self.position() + seconds
        if self.duration:
            target = min(target, self.duration - 1)
        self.play(self.video, max(0, target))

    def on_seek_release(self, scale, event):
        self.dragging_seek = False
        if self.video is not None:
            self.play(self.video, scale.get_value())
        return False

    def on_video_click(self, widget, event):
        if event.type == Gdk.EventType._2BUTTON_PRESS:
            self.toggle_full()
        elif event.button == 1:
            self.toggle_pause()
        return True

    def stop_stream(self):
        """Ends what's playing, here and on XenithOS Cloud."""
        self.generation += 1
        if self.player is not None:
            self.player.set_state(Gst.State.NULL)
        if self.sid:
            sid, self.sid = self.sid, ""
            self.workers.submit(self.cloud.ask, "GET", f"/v1/tube/stop?s={sid}", None, 10)
        if hasattr(self, "pause_b"):
            self.pause_b.set_sensitive(True)

    def stop(self):
        self.stop_stream()
        if self.is_full():
            self.unfullscreen()
        self.stack.set_visible_child_name("search")

    def is_full(self):
        win = self.get_window()
        return bool(win and win.get_state() & Gdk.WindowState.FULLSCREEN)

    def toggle_full(self):
        if self.is_full():
            self.unfullscreen()
        else:
            self.fullscreen()
        self.show_controls()

    def show_controls(self, keep=False):
        """The controls show when the pointer moves, and go again after 3 seconds of full screen."""
        if not hasattr(self, "controls"):
            return
        self.controls.show()
        win = self.get_window()
        if win:
            win.set_cursor(None)
        if self.hide_timer:
            GLib.source_remove(self.hide_timer)
            self.hide_timer = 0
        if not keep:
            self.hide_timer = GLib.timeout_add_seconds(3, self.hide_controls)

    def hide_controls(self):
        self.hide_timer = 0
        if self.is_full() and not self.paused and not self.ended:
            self.controls.hide()
            win = self.get_window()
            if win:
                win.set_cursor(Gdk.Cursor.new_from_name(win.get_display(), "none"))
        return False

    def on_key(self, widget, event):
        key = event.keyval
        ctrl = event.state & Gdk.ModifierType.CONTROL_MASK
        if ctrl and key in (Gdk.KEY_q, Gdk.KEY_w):
            self.destroy()
            return True
        if self.stack.get_visible_child_name() != "player":
            if (ctrl and key == Gdk.KEY_f) or (key == Gdk.KEY_slash and not self.entry.has_focus()):
                self.entry.grab_focus()
                return True
            return False
        self.show_controls()
        if key in (Gdk.KEY_space, Gdk.KEY_k):
            self.toggle_pause()
        elif key in (Gdk.KEY_Left, Gdk.KEY_j):
            self.skip(-10)
        elif key in (Gdk.KEY_Right, Gdk.KEY_l):
            self.skip(10)
        elif key in (Gdk.KEY_F11, Gdk.KEY_f):
            self.toggle_full()
        elif key == Gdk.KEY_Escape:
            if self.is_full():
                self.toggle_full()
            else:
                self.stop()
        else:
            return False
        return True

    def on_destroy(self, *args):
        self.stop_stream()
        self.workers.shutdown(wait=False)
        Gtk.main_quit()


def main():
    GLib.set_prgname("tube")
    Gdk.set_program_class("tube")
    xui.load_css()
    extra = Gtk.CssProvider()
    extra.load_from_data(EXTRA_CSS.encode())
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), extra, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 1)
    Tube()
    Gtk.main()


if __name__ == "__main__":
    main()
