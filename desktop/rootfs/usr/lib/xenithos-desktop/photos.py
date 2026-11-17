#!/usr/bin/env python3
"""Photos, the desktop version of XenithOS Console's Photos: made for the mouse and keyboard.

    photos.py [PICTURE]

It shows the pictures XenithOS Console's Photos shows (photos and pictures folders in XenithOS
Console's data, and ~/Pictures) and a photos or pictures folder on any USB stick or drive, two
folders deep, newest first, with the folders at the side.

A picture opens in the window. The mouse wheel zooms where the pointer is, dragging moves a
zoomed picture, and a double click shows it at full size or fits it again. The arrow keys go
through them, R turns one, F5 is a slideshow, F11 fills the screen and Delete moves a picture to
the Trash. "Use as wallpaper" makes it the desktop's wallpaper, as Settings > Look does.

Given a picture (from Files' Open With), it opens that one with the rest of its folder.
Thumbnails are kept in ~/.cache/xenithos/photos.
"""
import hashlib
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xui  # noqa: E402
from xui import Gdk, GdkPixbuf, GLib, Gtk, button, label  # noqa: E402

import cairo  # noqa: E402
import gi  # noqa: E402

gi.require_version("Gio", "2.0")
gi.require_version("Pango", "1.0")
from gi.repository import Gio, Pango  # noqa: E402

THUMB_W, THUMB_H = 220, 165       # a tile at least; it grows to fill the grid
THUMB_KEPT = (330, 248)           # what's kept in the cache, sharp enough for a grown tile
LARGEST = 4096                    # a picture is opened at most this big: plenty to zoom into
CACHE = os.path.join(GLib.get_user_cache_dir(), "xenithos", "photos")
FOLDER_NAMES = ("photos", "Photos", "pictures", "Pictures")
MOST = 2000                       # (the grid holds every one, so not too many)
SLIDE_SECONDS = 5
WANTED = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".heic", ".heif", ".avif"}

EXTRA_CSS = """
.xenith flowbox { background-color: transparent; }
.xenith flowboxchild { padding: 3px; border-radius: 13px; background-color: transparent; }
.xenith flowboxchild:hover, .xenith flowboxchild:focus { background-color: #f1f3f6; }
.xenith .toolbar { background-color: #0b0d10; border-bottom: 1px solid #1d2128; padding: 8px 14px; }
.xenith button.icon { background-image: none; background-color: transparent; border: 1px solid transparent;
                      border-radius: 10px; padding: 6px 9px; box-shadow: none; color: #f1f3f6; }
.xenith button.icon:hover { background-color: #1b1f26; border-color: #262b33; }
.xenith button.icon:disabled { color: #4b525c; }
.xenith .note { color: #46a758; font-weight: 600; font-size: 13px; }
"""


def picture_endings():
    """The picture files this PC can open (GdkPixbuf's formats), of the kinds Photos shows."""
    found = set()
    for f in GdkPixbuf.Pixbuf.get_formats():
        if not f.is_disabled():
            found.update("." + e.lower() for e in f.get_extensions())
    return (found & WANTED) or {".jpg", ".jpeg", ".png", ".gif", ".bmp"}


ENDINGS = picture_endings()


class Picture:
    __slots__ = ("path", "mtime", "size", "folder")

    def __init__(self, path, mtime, size, folder):
        self.path, self.mtime, self.size, self.folder = path, mtime, size, folder

    @property
    def name(self):
        return os.path.basename(self.path)


def places():
    """Where pictures are looked for: (the folder, what it's called at the side)."""
    found = []
    pictures = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_PICTURES) or os.path.expanduser("~/Pictures")
    found.append((pictures, os.path.basename(pictures.rstrip("/")) or "Pictures"))
    data = os.environ.get("XENITHOS_DATA") or os.path.expanduser("~/.local/share/xenithos")
    for n in FOLDER_NAMES:
        found.append((os.path.join(data, "uda:", n), "XenithOS Console's " + n.lower()))
    user = GLib.get_user_name()
    for media in ("/media/" + user, "/run/media/" + user):
        try:
            drives = sorted(os.listdir(media))
        except OSError:
            continue
        for d in drives:
            for n in FOLDER_NAMES:
                found.append((os.path.join(media, d, n), d + " " + n.lower()))
    seen, out = set(), []
    for path, called in found:
        real = os.path.realpath(path)
        if real not in seen and os.path.isdir(path):
            seen.add(real)
            out.append((path, called))
    return out


def scan():
    """Every picture in places(), two folders deep, newest first; and the folders they're in,
    as (folder, what it's called), in the order they were found."""
    pictures, folders, seen = [], [], set()

    def walk(path, called, depth):
        try:
            entries = sorted(os.scandir(path), key=lambda e: e.name.lower())
        except OSError:
            return
        here = []
        for e in entries:
            if e.name.startswith(".") or len(pictures) + len(here) >= MOST:
                continue
            try:
                if e.is_dir(follow_symlinks=False):
                    continue
                if os.path.splitext(e.name)[1].lower() not in ENDINGS:
                    continue
                st = e.stat()
            except OSError:
                continue
            real = os.path.realpath(e.path)
            if st.st_size > 0 and real not in seen:
                seen.add(real)
                here.append(Picture(e.path, st.st_mtime, st.st_size, path))
        if here:
            folders.append((path, called))
            pictures.extend(here)
        if depth < 2:
            for e in entries:
                try:
                    if not e.name.startswith(".") and e.is_dir(follow_symlinks=False):
                        walk(e.path, called + " / " + e.name, depth + 1)
                except OSError:
                    pass

    for path, called in places():
        walk(path, called, 0)
    pictures.sort(key=lambda p: p.mtime, reverse=True)
    return pictures, folders


def in_folder(path):
    """The pictures next to path (a picture opened from Files), by name."""
    folder = os.path.dirname(os.path.abspath(path))
    out = []
    try:
        for e in sorted(os.scandir(folder), key=lambda e: e.name.lower()):
            if os.path.splitext(e.name)[1].lower() in ENDINGS and not e.name.startswith("."):
                try:
                    st = e.stat()
                    out.append(Picture(e.path, st.st_mtime, st.st_size, folder))
                except OSError:
                    pass
    except OSError:
        pass
    target = os.path.abspath(path)
    if os.path.isfile(target) and not any(os.path.abspath(p.path) == target for p in out):
        st = os.stat(target)          # a picture of a kind Photos doesn't list, opened anyway
        out.insert(0, Picture(target, st.st_mtime, st.st_size, folder))
    return out


def cover(pb, w, h):
    """pb scaled to fill w x h, with what sticks out cut off evenly."""
    sw, sh = pb.get_width(), pb.get_height()
    s = max(w / sw, h / sh)
    nw, nh = max(w, round(sw * s)), max(h, round(sh * s))
    scaled = pb.scale_simple(nw, nh, GdkPixbuf.InterpType.BILINEAR)
    return scaled.new_subpixbuf((nw - w) // 2, (nh - h) // 2, w, h).copy()


def thumbnail(p):
    """p's thumbnail (from the cache when it's been made before), or None."""
    key = hashlib.sha1(f"{p.path}|{p.mtime}|{p.size}|{THUMB_KEPT}".encode()).hexdigest()
    cached = os.path.join(CACHE, key + ".png")
    try:
        if os.path.exists(cached):
            return GdkPixbuf.Pixbuf.new_from_file(cached)
        pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(p.path, THUMB_KEPT[0] * 2, THUMB_KEPT[1] * 2, True)
        pb = pb.apply_embedded_orientation() or pb
        pb = cover(pb, *THUMB_KEPT)
        os.makedirs(CACHE, exist_ok=True)
        pb.savev(cached + ".part", "png", [], [])
        os.replace(cached + ".part", cached)
        return pb
    except (GLib.Error, OSError):
        return None


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


class Tile(Gtk.DrawingArea):
    """A picture in the grid: its thumbnail, with rounded corners, once it's made."""

    def __init__(self, picture):
        super().__init__()
        self.picture = picture
        self.pb = None
        self.set_size_request(THUMB_W, THUMB_H)
        self.set_tooltip_text(picture.name)
        self.connect("draw", self.on_draw)

    def on_draw(self, area, cr):
        w, h = area.get_allocated_width(), area.get_allocated_height()
        rounded(cr, 0, 0, w, h, 10)
        cr.clip()
        cr.set_source_rgb(0x12 / 255, 0x15 / 255, 0x1a / 255)
        cr.paint()
        if self.pb:
            # Scaled to fill the tile however wide the grid makes it, what sticks out cut off.
            pw, ph = self.pb.get_width(), self.pb.get_height()
            s = max(w / pw, h / ph)
            cr.translate((w - pw * s) / 2, (h - ph * s) / 2)
            cr.scale(s, s)
            Gdk.cairo_set_source_pixbuf(cr, self.pb, 0, 0)
            cr.get_source().set_filter(cairo.FILTER_GOOD)
            cr.paint()


class Photos(Gtk.Window):
    def __init__(self, open_path=None):
        super().__init__(title="Photos")
        self.get_style_context().add_class("xenith")
        self.set_icon_name("xenithos-app-photos")
        room = work_area()
        self.set_default_size(min(1200, int(room.width * 0.85)), min(780, int(room.height * 0.85)))
        self.connect("destroy", Gtk.main_quit)
        self.connect("key-press-event", self.on_key)
        self.connect("focus-in-event", self.on_focus)

        self.all, self.folders, self.shown = [], [], []
        self.folder = None              # the folder shown in the grid (None: all of them)
        self.from_files = bool(open_path)
        self.scanned = 0
        self.workers = ThreadPoolExecutor(max_workers=3)
        self.generation = 0             # thumbnails asked for by an older grid are dropped

        # The viewer's state
        self.index = 0
        self.pb = None                  # the picture as opened (turned the way it was taken)
        self.view = None                # ...and as turned by R
        self.turn = 0
        self.zoom = None                # None: fits the window
        self.cx = self.cy = 0.0         # the point of the picture in the middle of the window
        self.drag = None
        self.slideshow = 0
        self.note_timer = 0
        self.refill_later = False
        self.filling = False

        self.stack = Gtk.Stack(transition_type=Gtk.StackTransitionType.CROSSFADE, transition_duration=120)
        self.stack.add_named(self.build_library(), "library")
        self.stack.add_named(self.build_viewer(), "viewer")
        self.add(self.stack)
        self.show_all()

        if open_path:
            self.all = in_folder(open_path)
            self.folders = []
            self.shown = list(self.all)
            self.fill_sidebar()
            self.fill_grid()
            start = next((i for i, p in enumerate(self.shown) if os.path.abspath(p.path) == os.path.abspath(open_path)), 0)
            self.open(start)
        else:
            self.rescan()

    # ------------------------------------------------------------------ the library

    def build_library(self):
        page = Gtk.Box()
        side = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        side.get_style_context().add_class("sidebar")
        side.set_size_request(250, -1)
        head = label("Photos", "title")
        head.set_margin_start(22)
        head.set_margin_top(22)
        head.set_margin_bottom(12)
        side.pack_start(head, False, False, 0)
        self.side_list = Gtk.ListBox()
        self.side_list.get_style_context().add_class("sidebar")
        self.side_list.connect("row-selected", self.on_folder)
        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroll.add(self.side_list)
        side.pack_start(scroll, True, True, 0)
        page.pack_start(side, False, False, 0)

        main = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        main.get_style_context().add_class("page")
        main.set_border_width(24)
        top = Gtk.Box(spacing=12)
        words = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        self.grid_title = label("All pictures", "title")
        self.grid_sub = label("", "subtitle")
        words.pack_start(self.grid_title, False, False, 0)
        words.pack_start(self.grid_sub, False, False, 0)
        top.pack_start(words, True, True, 0)
        self.show_button = button("Show in Files", None, lambda b: self.show_in_files(self.folder or self.pictures_folder()))
        self.slides_button = button("Slideshow", "primary", lambda b: self.open(0, slideshow=True))
        for b in (self.show_button, self.slides_button):
            b.set_valign(Gtk.Align.CENTER)
            top.pack_end(b, False, False, 0)
        main.pack_start(top, False, False, 0)

        self.grid = Gtk.FlowBox(homogeneous=True, selection_mode=Gtk.SelectionMode.NONE, valign=Gtk.Align.START,
                                column_spacing=8, row_spacing=8, max_children_per_line=40, activate_on_single_click=True)
        self.grid.connect("child-activated", lambda box, child: self.open(child.get_index()))
        self.grid_scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        self.grid_scroll.add(self.grid)

        self.empty = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, valign=Gtk.Align.CENTER,
                             halign=Gtk.Align.CENTER)
        self.empty.pack_start(label("No pictures yet", "title", xalign=0.5), False, False, 0)
        self.empty.pack_start(label("Photos shows the pictures in your Pictures folder, and in a photos or pictures "
                                    "folder on a USB stick or drive.", "subtitle", xalign=0.5), False, False, 0)
        opener = button("Open the Pictures folder", "primary", lambda b: self.show_in_files(self.pictures_folder()))
        opener.set_halign(Gtk.Align.CENTER)
        self.empty.pack_start(opener, False, False, 12)

        self.grid_or_empty = Gtk.Stack()
        self.grid_or_empty.add_named(self.grid_scroll, "grid")
        self.grid_or_empty.add_named(self.empty, "empty")
        main.pack_start(self.grid_or_empty, True, True, 0)
        page.pack_start(main, True, True, 0)
        return page

    def pictures_folder(self):
        folder = GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_PICTURES) or os.path.expanduser("~/Pictures")
        os.makedirs(folder, exist_ok=True)
        return folder

    def rescan(self):
        def done(result):
            self.all, self.folders = result
            self.scanned = time.monotonic()
            self.fill_sidebar()
            self.pick_folder(self.folder)

        xui.later(scan, done)

    def on_focus(self, *args):
        # Back from somewhere else after a while: pictures may have come or gone.
        if not self.from_files and self.scanned and time.monotonic() - self.scanned > 30 \
                and self.stack.get_visible_child_name() == "library":
            self.rescan()

    def fill_sidebar(self):
        self.filling = True
        for row in self.side_list.get_children():
            self.side_list.remove(row)
        rows = [(None, "All pictures", len(self.all))]
        for path, called in self.folders:
            rows.append((path, called, sum(1 for p in self.all if p.folder == path)))
        for path, called, count in rows:
            row = Gtk.ListBoxRow()
            row.folder = path
            box = Gtk.Box(spacing=8)
            name = label(called, None, wrap=False)
            name.set_ellipsize(Pango.EllipsizeMode.END)
            box.pack_start(name, True, True, 0)
            box.pack_end(label(str(count), "small", wrap=False), False, False, 0)
            row.add(box)
            if path:
                row.set_tooltip_text(path)
            self.side_list.add(row)
        self.side_list.show_all()
        self.filling = False

    def on_folder(self, box, row):
        if row is not None and not self.filling:
            self.pick_folder(row.folder)

    def pick_folder(self, folder):
        if folder and not any(f == folder for f, _ in self.folders):
            folder = None
        self.folder = folder
        for row in self.side_list.get_children():
            if row.folder == folder and self.side_list.get_selected_row() is not row:
                self.filling = True
                self.side_list.select_row(row)
                self.filling = False
        self.shown = [p for p in self.all if folder is None or p.folder == folder]
        called = next((c for f, c in self.folders if f == folder), "All pictures")
        self.grid_title.set_text(called)
        n = len(self.shown)
        self.grid_sub.set_text(("1 picture" if n == 1 else f"{n} pictures") + (", newest first" if n > 1 else ""))
        self.slides_button.set_sensitive(n > 0)
        self.fill_grid()

    def fill_grid(self):
        self.generation += 1
        generation = self.generation
        for child in self.grid.get_children():
            self.grid.remove(child)
        for p in self.shown:
            tile = Tile(p)
            self.grid.add(tile)
            self.workers.submit(self.make_thumbnail, tile, generation)
        self.grid.show_all()
        # "No pictures yet" only once the folders have been looked through.
        nothing = not self.shown and self.scanned and not self.from_files
        self.grid_or_empty.set_visible_child_name("empty" if nothing else "grid")

    def make_thumbnail(self, tile, generation):
        if generation != self.generation:
            return
        pb = thumbnail(tile.picture)
        if pb is not None:
            GLib.idle_add(self.set_thumbnail, tile, pb)

    def set_thumbnail(self, tile, pb):
        tile.pb = pb
        tile.queue_draw()
        return False

    # ------------------------------------------------------------------ the viewer

    def build_viewer(self):
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        page.get_style_context().add_class("page")
        self.toolbar = Gtk.Box(spacing=6)
        self.toolbar.get_style_context().add_class("toolbar")

        def icon(name, tip, action):
            b = Gtk.Button.new_from_icon_name(name, Gtk.IconSize.LARGE_TOOLBAR)
            b.get_style_context().add_class("icon")
            b.set_tooltip_text(tip)
            b.connect("clicked", lambda w: action())
            return b

        back = Gtk.Button(label="Back", image=Gtk.Image.new_from_icon_name("go-previous-symbolic", Gtk.IconSize.BUTTON),
                          always_show_image=True)
        back.get_style_context().add_class("icon")
        back.set_tooltip_text("Back to the pictures (Esc)")
        back.connect("clicked", lambda w: self.close_viewer())
        self.toolbar.pack_start(back, False, False, 0)
        words = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, valign=Gtk.Align.CENTER)
        self.v_name = label("", "row-title", wrap=False)
        self.v_name.set_ellipsize(Pango.EllipsizeMode.END)
        self.v_where = label("", "small", wrap=False)
        words.pack_start(self.v_name, False, False, 0)
        words.pack_start(self.v_where, False, False, 0)
        self.toolbar.pack_start(words, True, True, 8)
        self.v_note = label("", "note", wrap=False)
        self.toolbar.pack_start(self.v_note, False, False, 8)
        self.prev_b = icon("go-previous", "The one before (Left)", lambda: self.step(-1))
        self.next_b = icon("go-next", "The next one (Right)", lambda: self.step(1))
        for w in (self.prev_b, self.next_b,
                  icon("object-rotate-right", "Turn it (R)", lambda: self.rotate(90)),
                  icon("zoom-fit-best", "Fit it in the window (0)", self.fit),
                  icon("view-fullscreen", "Fill the screen (F11)", self.toggle_full),
                  icon("folder-open", "Show in Files", lambda: self.show_in_files(self.current().path)),
                  icon("user-trash", "Move to the Trash (Delete)", self.trash)):
            self.toolbar.pack_start(w, False, False, 0)
        for w in (button("Use as wallpaper", None, lambda b: self.wallpaper()),
                  button("Slideshow", "primary", lambda b: self.start_slideshow())):
            w.set_valign(Gtk.Align.CENTER)
            w.set_margin_start(6)
            self.toolbar.pack_start(w, False, False, 0)
        page.pack_start(self.toolbar, False, False, 0)

        self.area = Gtk.DrawingArea(can_focus=True)
        self.area.add_events(Gdk.EventMask.SCROLL_MASK | Gdk.EventMask.SMOOTH_SCROLL_MASK |
                             Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK |
                             Gdk.EventMask.POINTER_MOTION_MASK)
        self.area.connect("draw", self.on_draw)
        self.area.connect("scroll-event", self.on_scroll)
        self.area.connect("button-press-event", self.on_press)
        self.area.connect("button-release-event", self.on_release)
        self.area.connect("motion-notify-event", self.on_motion)
        page.pack_start(self.area, True, True, 0)
        return page

    def current(self):
        return self.shown[self.index]

    def open(self, index, slideshow=False):
        if not self.shown:
            return
        self.index = max(0, min(index, len(self.shown) - 1))
        self.stack.set_visible_child_name("viewer")
        self.load()
        self.area.grab_focus()
        if slideshow:
            self.start_slideshow()

    def load(self):
        p = self.current()
        self.turn, self.zoom, self.drag = 0, None, None
        try:
            pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(p.path, LARGEST, LARGEST, True)
            self.pb = pb.apply_embedded_orientation() or pb
        except GLib.Error:
            self.pb = None
        self.turned()
        self.v_name.set_text(p.name)
        where = next((c for f, c in self.folders if f == p.folder), os.path.basename(p.folder.rstrip("/")) or p.folder)
        self.v_where.set_text(f"{self.index + 1} of {len(self.shown)}    {where}")
        self.prev_b.set_sensitive(self.index > 0)
        self.next_b.set_sensitive(self.index < len(self.shown) - 1)

    def turned(self):
        rot = {0: GdkPixbuf.PixbufRotation.NONE, 90: GdkPixbuf.PixbufRotation.CLOCKWISE,
               180: GdkPixbuf.PixbufRotation.UPSIDEDOWN, 270: GdkPixbuf.PixbufRotation.COUNTERCLOCKWISE}[self.turn]
        self.view = self.pb.rotate_simple(rot) if self.pb is not None else None
        self.zoom = None
        if self.view is not None:
            self.cx, self.cy = self.view.get_width() / 2, self.view.get_height() / 2
        self.area.queue_draw()

    def close_viewer(self):
        self.stop_slideshow()
        if self.is_full():
            self.unfullscreen()
        if self.from_files and not self.folders:
            # Opened from Files: the library comes first now.
            self.from_files = False
            self.all, self.shown = [], []
            self.rescan()
        self.stack.set_visible_child_name("library")
        self.pb = self.view = None
        if self.refill_later:           # a picture went to the Trash: the grid loses it too
            self.refill_later = False
            self.pick_folder(self.folder)

    def step(self, d):
        if 0 <= self.index + d < len(self.shown):
            self.index += d
            self.load()
            return True
        return False

    def rotate(self, degrees):
        self.turn = (self.turn + degrees) % 360
        self.turned()

    # Where the picture is drawn, at what size.
    def fit_scale(self):
        w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
        return min(w / self.view.get_width(), h / self.view.get_height(), 1.0)

    def scale(self):
        return self.zoom or self.fit_scale()

    def origin(self, s):
        w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
        iw, ih = self.view.get_width() * s, self.view.get_height() * s
        if self.zoom is None or iw <= w:
            x = (w - iw) / 2
        else:
            x = min(0, max(w - iw, w / 2 - self.cx * s))
        if self.zoom is None or ih <= h:
            y = (h - ih) / 2
        else:
            y = min(0, max(h - ih, h / 2 - self.cy * s))
        return x, y

    def on_draw(self, area, cr):
        w, h = area.get_allocated_width(), area.get_allocated_height()
        cr.set_source_rgb(7 / 255, 8 / 255, 10 / 255)
        cr.paint()
        if self.view is None:
            if self.shown:
                cr.set_source_rgb(0x8b / 255, 0x93 / 255, 0xa0 / 255)
                cr.select_font_face("Inter")
                cr.set_font_size(16)
                text = "This picture couldn't be opened."
                ext = cr.text_extents(text)
                cr.move_to((w - ext.width) / 2, h / 2)
                cr.show_text(text)
            return
        s = self.scale()
        x, y = self.origin(s)
        cr.translate(x, y)
        cr.scale(s, s)
        Gdk.cairo_set_source_pixbuf(cr, self.view, 0, 0)
        cr.get_source().set_filter(cairo.FILTER_GOOD if s < 3 else cairo.FILTER_NEAREST)
        cr.paint()

    def zoom_at(self, factor, px, py):
        if self.view is None:
            return
        old = self.scale()
        x, y = self.origin(old)
        ix, iy = (px - x) / old, (py - y) / old             # the picture's point under the pointer
        new = max(self.fit_scale(), min(old * factor, 8.0))
        if new <= self.fit_scale() + 1e-6:
            self.zoom = None
        else:
            w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
            self.zoom = new
            self.cx, self.cy = ix + (w / 2 - px) / new, iy + (h / 2 - py) / new
        self.area.queue_draw()

    def fit(self):
        self.zoom = None
        self.area.queue_draw()

    def on_scroll(self, area, event):
        self.stop_slideshow()
        if event.direction == Gdk.ScrollDirection.UP:
            d = 1
        elif event.direction == Gdk.ScrollDirection.DOWN:
            d = -1
        elif event.direction == Gdk.ScrollDirection.SMOOTH:
            d = -event.delta_y
        else:
            return False
        if d:
            self.zoom_at(1.25 ** d, event.x, event.y)
        return True

    def on_press(self, area, event):
        self.stop_slideshow()
        self.area.grab_focus()
        if event.button == 1 and event.type == Gdk.EventType._2BUTTON_PRESS:
            if self.zoom is None:
                self.zoom_at(1.0 / self.fit_scale() if self.view is not None else 1, event.x, event.y)
            else:
                self.fit()
        elif event.button == 1 and self.zoom is not None:
            self.drag = (event.x, event.y, self.cx, self.cy)
        return True

    def on_release(self, area, event):
        self.drag = None
        return True

    def on_motion(self, area, event):
        if self.drag and self.zoom:
            sx, sy, cx, cy = self.drag
            self.cx = cx - (event.x - sx) / self.zoom
            self.cy = cy - (event.y - sy) / self.zoom
            self.area.queue_draw()
        return True

    # ------------------------------------------------------------------ the rest

    def is_full(self):
        win = self.get_window()
        return bool(win and win.get_state() & Gdk.WindowState.FULLSCREEN)

    def toggle_full(self):
        if self.is_full():
            self.unfullscreen()
            self.toolbar.show()
        else:
            self.fullscreen()

    def start_slideshow(self):
        if not self.shown:
            return
        if self.stack.get_visible_child_name() != "viewer":
            self.open(0)
        self.stop_slideshow()
        self.fullscreen()
        self.toolbar.hide()
        self.slideshow = GLib.timeout_add_seconds(SLIDE_SECONDS, self.next_slide)

    def next_slide(self):
        if not self.step(1):
            self.index = -1
            self.step(1)                # round again from the first
        return True

    def stop_slideshow(self):
        if self.slideshow:
            GLib.source_remove(self.slideshow)
            self.slideshow = 0
            self.toolbar.show()
            if self.is_full():
                self.unfullscreen()

    def note(self, text):
        self.v_note.set_text(text)
        if self.note_timer:
            GLib.source_remove(self.note_timer)
        self.note_timer = GLib.timeout_add_seconds(4, self.clear_note)

    def clear_note(self):
        self.v_note.set_text("")
        self.note_timer = 0
        return False

    def wallpaper(self):
        xui.set_wallpaper(self.current().path)
        self.note("It's your wallpaper now")

    def show_in_files(self, path):
        uri = Gio.File.new_for_path(path).get_uri()
        try:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            bus.call_sync("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                          "org.freedesktop.FileManager1", "ShowItems" if os.path.isfile(path) else "ShowFolders",
                          GLib.Variant("(ass)", ([uri], "")), None, Gio.DBusCallFlags.NONE, 3000, None)
        except GLib.Error:
            Gtk.show_uri_on_window(self, Gio.File.new_for_path(os.path.dirname(path) if os.path.isfile(path) else path).get_uri(),
                                   Gtk.get_current_event_time())

    def trash(self):
        if not self.shown:
            return
        self.stop_slideshow()
        p = self.current()
        if not xui.ask("Move it to the Trash?", f"{p.name} goes to the Trash, where you can get it back from.",
                       "Move to Trash", "Not now", danger=True, parent=self):
            return
        try:
            Gio.File.new_for_path(p.path).trash(None)
        except GLib.Error as e:
            xui.ask("It couldn't be moved to the Trash", e.message, "OK", "", parent=self)
            return
        for lst in (self.shown, self.all):
            if p in lst:
                lst.remove(p)
        self.fill_sidebar()
        if not self.shown:
            self.close_viewer()
            self.pick_folder(self.folder)
            return
        self.index = min(self.index, len(self.shown) - 1)
        self.load()
        self.refill_later = True

    def on_key(self, widget, event):
        key = event.keyval
        ctrl = event.state & Gdk.ModifierType.CONTROL_MASK
        if ctrl and key in (Gdk.KEY_q, Gdk.KEY_w):
            self.destroy()
            return True
        if key == Gdk.KEY_F11:
            self.toggle_full()
            return True
        if self.stack.get_visible_child_name() != "viewer":
            if key == Gdk.KEY_F5:
                self.open(0, slideshow=True)
                return True
            return False
        if self.slideshow:
            self.stop_slideshow()
            return True
        if key == Gdk.KEY_Escape:
            if self.is_full():
                self.toggle_full()
            else:
                self.close_viewer()
        elif key in (Gdk.KEY_Right, Gdk.KEY_Page_Down, Gdk.KEY_space):
            self.step(1)
        elif key in (Gdk.KEY_Left, Gdk.KEY_Page_Up, Gdk.KEY_BackSpace):
            self.step(-1)
        elif key == Gdk.KEY_Home:
            self.step(-self.index)
        elif key == Gdk.KEY_End:
            self.step(len(self.shown) - 1 - self.index)
        elif key in (Gdk.KEY_r, Gdk.KEY_R):
            self.rotate(270 if event.state & Gdk.ModifierType.SHIFT_MASK else 90)
        elif key in (Gdk.KEY_plus, Gdk.KEY_equal, Gdk.KEY_KP_Add):
            w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
            self.zoom_at(1.25, w / 2, h / 2)
        elif key in (Gdk.KEY_minus, Gdk.KEY_KP_Subtract):
            w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
            self.zoom_at(0.8, w / 2, h / 2)
        elif key in (Gdk.KEY_0, Gdk.KEY_KP_0):
            self.fit()
        elif key == Gdk.KEY_1 and self.view is not None:
            w, h = self.area.get_allocated_width(), self.area.get_allocated_height()
            self.zoom_at(1.0 / self.scale(), w / 2, h / 2)
        elif key == Gdk.KEY_F5:
            self.start_slideshow()
        elif key == Gdk.KEY_Delete:
            self.trash()
        else:
            return False
        return True


def main():
    GLib.set_prgname("photos")
    Gdk.set_program_class("photos")
    xui.load_css()
    extra = Gtk.CssProvider()
    extra.load_from_data(EXTRA_CSS.encode())
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), extra, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 1)
    paths = [a for a in sys.argv[1:] if not a.startswith("-")]
    path = paths[0] if paths else None
    if path and path.startswith("file://"):
        path = Gio.File.new_for_uri(path).get_path()
    Photos(path if path and os.path.isfile(path) else None)
    Gtk.main()


if __name__ == "__main__":
    main()
