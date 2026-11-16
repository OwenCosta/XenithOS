"""What XenithOS Desktop's own apps share (Settings, Security Tools, the small questions): its
look as GTK CSS, the user's desktop choices (~/.config/xenithos/desktop.conf), and a few helpers.
"""
import os
import subprocess
import threading

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk  # noqa: E402

LOGO = "/usr/share/icons/XenithOS/scalable/apps/xenithos.svg"
CONF_DIR = os.path.join(os.path.expanduser("~"), ".config", "xenithos")
CONF = os.path.join(CONF_DIR, "desktop.conf")
RED = "#e50914"

# The colours people can pick in Settings > Look. Red is XenithOS's own.
ACCENTS = [
    ("Red", "#e50914"), ("Orange", "#f76b15"), ("Amber", "#f5a524"), ("Green", "#30a46c"),
    ("Teal", "#12a594"), ("Blue", "#3b82f6"), ("Violet", "#8e4ec6"), ("Pink", "#e93d82"),
    ("White", "#f1f3f6"),
]


def read_conf():
    conf = {}
    try:
        with open(CONF) as f:
            for line in f:
                key, sep, value = line.strip().partition("=")
                if sep and not key.startswith("#"):
                    conf[key] = value
    except OSError:
        pass
    return conf


def write_conf(**changes):
    conf = read_conf()
    conf.update({k: str(v) for k, v in changes.items()})
    os.makedirs(CONF_DIR, exist_ok=True)
    tmp = CONF + ".tmp"
    with open(tmp, "w") as f:
        for key in sorted(conf):
            f.write(f"{key}={conf[key]}\n")
    os.replace(tmp, CONF)
    return conf


def accent():
    value = read_conf().get("accent", RED)
    return value if len(value) == 7 and value.startswith("#") else RED


def on_accent(colour):
    """Black or white, whichever reads on the colour."""
    r, g, b = (int(colour[i:i + 2], 16) for i in (1, 3, 5))
    return "#07080a" if (0.299 * r + 0.587 * g + 0.114 * b) > 150 else "#ffffff"


def css(colour=None):
    a = colour or accent()
    return f"""
* {{ font-family: Inter, sans-serif; }}
window.xenith, .xenith .page {{ background-color: #07080a; color: #f1f3f6; }}
.xenith .sidebar {{ background-color: #0b0d10; border-right: 1px solid #1d2128; }}
.xenith .sidebar row {{ padding: 10px 14px; border-radius: 8px; margin: 2px 8px; color: #c4c9d1; }}
.xenith .sidebar row:hover {{ background-color: #15181d; }}
.xenith .sidebar row:selected {{ background-color: #1b1f26; color: #ffffff; }}
.xenith .sidebar row label {{ font-size: 14px; font-weight: 600; }}
.xenith .title {{ font-size: 26px; font-weight: 700; color: #f1f3f6; }}
.xenith .heading {{ font-size: 15px; font-weight: 700; color: #f1f3f6; }}
.xenith .subtitle {{ color: #8b93a0; font-size: 14px; }}
.xenith .small {{ color: #8b93a0; font-size: 12px; }}
.xenith .value {{ color: #c4c9d1; font-size: 14px; }}
.xenith .error {{ color: #ff6b73; font-weight: 600; }}
.xenith .good {{ color: #46a758; font-weight: 600; }}
.xenith .card {{ background-color: #12151a; border: 1px solid #262b33; border-radius: 12px; padding: 16px 18px; }}
.xenith list.rows {{ background-color: #12151a; border: 1px solid #262b33; border-radius: 12px; }}
.xenith list.rows row {{ padding: 12px 16px; border-bottom: 1px solid #1d2128; }}
.xenith list.rows row:last-child {{ border-bottom: none; }}
.xenith list.rows row:hover {{ background-color: #161a20; }}
.xenith list.rows row:selected {{ background-color: #1b1f26; }}
.xenith .row-title {{ font-size: 14px; font-weight: 600; color: #f1f3f6; }}
.xenith entry {{ background-color: #0d0f12; color: #f1f3f6; border: 1px solid #262b33; border-radius: 8px;
                padding: 7px 12px; box-shadow: none; caret-color: {a}; }}
.xenith entry:focus {{ border-color: #f1f3f6; box-shadow: none; }}
.xenith button.action {{ background-image: none; background-color: #1b1f26; color: #f1f3f6; border: 1px solid #262b33;
                        border-radius: 10px; padding: 8px 20px; font-size: 14px; font-weight: 600;
                        box-shadow: none; text-shadow: none; }}
.xenith button.action:hover {{ background-color: #232832; }}
.xenith button.action:focus {{ border-color: #f1f3f6; }}
.xenith button.action:disabled {{ background-color: #12151a; color: #4b525c; border-color: #1d2128; }}
.xenith button.primary {{ background-color: #f1f3f6; color: #07080a; border-color: #f1f3f6; }}
.xenith button.primary:hover {{ background-color: #ffffff; }}
.xenith button.primary label {{ color: #07080a; }}
.xenith button.danger {{ background-color: #e50914; color: #ffffff; border-color: #e50914; }}
.xenith button.danger:hover {{ background-color: #f5222d; }}
.xenith button.swatch {{ min-width: 34px; min-height: 34px; border-radius: 17px; padding: 0; border: 2px solid transparent;
                        background-image: none; box-shadow: none; }}
.xenith button.swatch.chosen {{ border-color: #f1f3f6; }}
.xenith button.picture {{ padding: 3px; border-radius: 10px; border: 2px solid transparent; background-image: none;
                         background-color: transparent; box-shadow: none; }}
.xenith button.picture.chosen {{ border-color: #f1f3f6; }}
.xenith switch {{ background-image: none; background-color: #262b33; border: none; border-radius: 14px; min-width: 46px; }}
.xenith switch:checked {{ background-color: {a}; }}
.xenith switch slider {{ background-image: none; background-color: #f1f3f6; border: none; border-radius: 12px;
                        min-width: 22px; min-height: 22px; margin: 2px; box-shadow: none; }}
.xenith scale trough {{ background-color: #262b33; border: none; border-radius: 4px; min-height: 6px; }}
.xenith scale highlight {{ background-color: {a}; border: none; border-radius: 4px; }}
.xenith scale slider {{ background-image: none; background-color: #f1f3f6; border: none; border-radius: 9px;
                       min-width: 18px; min-height: 18px; box-shadow: none; }}
.xenith progressbar trough {{ background-color: #12151a; border: none; border-radius: 5px; min-height: 8px; }}
.xenith progressbar progress {{ background-color: {a}; border: none; border-radius: 5px; min-height: 8px; }}
.xenith combobox button {{ background-image: none; background-color: #0d0f12; color: #f1f3f6; border: 1px solid #262b33;
                          border-radius: 8px; padding: 6px 12px; box-shadow: none; }}
.xenith scrolledwindow, .xenith viewport {{ background-color: transparent; border: none; }}
.xenith textview, .xenith textview text {{ background-color: #0b0d10; color: #c4c9d1; font-family: monospace; font-size: 12px; }}
.xenith .signal {{ color: #8b93a0; font-size: 12px; font-weight: 600; }}
.xenith .tag {{ background-color: #1b1f26; border-radius: 6px; padding: 2px 8px; color: #c4c9d1; font-size: 12px; font-weight: 600; }}
.xenith .tag.on {{ background-color: {a}; color: {on_accent(a)}; }}
"""


_provider = None


def load_css(colour=None):
    """Puts XenithOS's look on this app's windows (again, after the colour changes)."""
    global _provider
    screen = Gdk.Screen.get_default()
    if _provider is not None:
        Gtk.StyleContext.remove_provider_for_screen(screen, _provider)
    _provider = Gtk.CssProvider()
    _provider.load_from_data(css(colour).encode())
    Gtk.StyleContext.add_provider_for_screen(screen, _provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)


def label(text, style=None, xalign=0.0, wrap=True, selectable=False):
    widget = Gtk.Label(label=text, xalign=xalign, wrap=wrap, selectable=selectable)
    if style:
        for name in style.split():
            widget.get_style_context().add_class(name)
    return widget


def button(text, style=None, on_click=None):
    widget = Gtk.Button(label=text)
    widget.get_style_context().add_class("action")
    if style:
        widget.get_style_context().add_class(style)
    if on_click:
        widget.connect("clicked", on_click)
    return widget


def card(*children, spacing=10):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=spacing)
    box.get_style_context().add_class("card")
    for child in children:
        box.pack_start(child, False, False, 0)
    return box


def setting_row(title, about, control):
    """A line in a card: what it is on the left, its switch or button on the right."""
    row = Gtk.Box(spacing=16)
    text = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, valign=Gtk.Align.CENTER)
    text.pack_start(label(title, "row-title"), False, False, 0)
    if about:
        text.pack_start(label(about, "small"), False, False, 0)
    row.pack_start(text, True, True, 0)
    control.set_valign(Gtk.Align.CENTER)
    row.pack_end(control, False, False, 0)
    return row


def logo(size):
    try:
        return Gtk.Image.new_from_pixbuf(GdkPixbuf.Pixbuf.new_from_file_at_size(LOGO, size, size))
    except GLib.Error:
        return Gtk.Image()


def run(cmd, timeout=30, stdin=None):
    """Runs a command; returns (exit code, what it printed). Never raises."""
    try:
        # Nothing to read unless it's given something: never the terminal Settings was opened from.
        feed = {"input": stdin} if stdin is not None else {"stdin": subprocess.DEVNULL}
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, **feed)
        return p.returncode, (p.stdout + p.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return 1, str(e)


def later(work, done=None):
    """Runs work() off the window's thread, then done(result) back on it."""
    def body():
        result = work()
        if done:
            GLib.idle_add(done, result)
    threading.Thread(target=body, daemon=True).start()


def ask(title, text, yes="OK", no="Not now", danger=False, parent=None):
    """A small question in XenithOS's look. True if the person chose yes."""
    load_css()
    dialog = Gtk.Window(title=title, modal=True, resizable=False, window_position=Gtk.WindowPosition.CENTER)
    dialog.get_style_context().add_class("xenith")
    dialog.set_type_hint(Gdk.WindowTypeHint.DIALOG)
    dialog.set_keep_above(True)
    if parent:
        dialog.set_transient_for(parent)
    answer = {"yes": False}
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
    box.set_border_width(26)
    box.set_size_request(420, -1)
    box.pack_start(label(title, "title"), False, False, 0)
    if text:
        box.pack_start(label(text, "subtitle"), False, False, 0)
    buttons = Gtk.Box(spacing=12, halign=Gtk.Align.END)

    def choose(value):
        answer["yes"] = value
        dialog.destroy()

    buttons.pack_start(button(no, None, lambda b: choose(False)), False, False, 0)
    go = button(yes, "danger" if danger else "primary", lambda b: choose(True))
    buttons.pack_start(go, False, False, 0)
    box.pack_start(buttons, False, False, 12)
    dialog.add(box)
    dialog.connect("destroy", Gtk.main_quit)
    dialog.connect("key-press-event", lambda w, e: e.keyval == Gdk.KEY_Escape and dialog.destroy())
    dialog.show_all()
    go.grab_focus()
    Gtk.main()
    return answer["yes"]
