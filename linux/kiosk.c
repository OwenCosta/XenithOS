/* Console mode's own small window manager (PCs and Raspberry Pis).
 *
 * In console mode XenithOS is the only program of its own X server (started with startx, the
 * way desktop mode starts), so apps from Flathub and Steam open as ordinary windows over
 * XenithOS's window instead of taking the screen away from it: nothing goes black when one
 * starts or quits. There's no desktop, so XenithOS manages those windows itself:
 *   - an app's window fills the screen;
 *   - dialogs, and windows that can't be resized (installers, say), are centred at their size;
 *   - the newest window is on top and has the keyboard;
 *   - when the last one goes, XenithOS's own window has the screen and the keyboard again.
 * The events are handled whenever XenithOS polls its controller (every frame, and every 20 ms
 * while an app runs).
 *
 * "Holding" keeps XenithOS's window in front of the app's while the guide is open, and while a
 * game left running from it waits behind the home screen; windows the app opens meanwhile go
 * behind XenithOS's. The guide also gets a picture of the screen (kiosk_wm_capture), and the
 * controller can work the pointer through the XTEST extension (libXtst, loaded when first
 * needed: without it there's just no pointer mode).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "platform_linux.h"

#define MAX_WINDOWS 128

static Display *dpy;
static Window root, self;
static int scr_w, scr_h;
static Window shown[MAX_WINDOWS];            /* apps' windows on screen, bottom to top */
static int nshown;
static int hold;                             /* XenithOS's window stays in front */
static Window check;                         /* EWMH's "a window manager is running" window */
static Atom a_type, a_dialog, a_splash, a_utility, a_active, a_state, a_fullscreen;
static volatile int other_wm;

static int quiet_errors(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;                                 /* windows come and go; a late request isn't a problem */
    return 0;
}

static int wm_check(Display *d, XErrorEvent *e)
{
    (void)d;
    if (e->error_code == BadAccess) other_wm = 1;
    return 0;
}

/* Enough of EWMH for toolkits to see a window manager here. Without one, GLFW (Minecraft) makes a
 * full-screen window override-redirect, which no window manager sees, so it never got the
 * keyboard: the mouse turned the camera (the game grabs the pointer) but keys went to XenithOS.
 * With it, GLFW and SDL ask for full screen with _NET_WM_STATE, handled in kiosk_wm_poll. */
static void ewmh_start(void)
{
    Atom a_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom a_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Atom a_name = XInternAtom(dpy, "_NET_WM_NAME", False), a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom supported[10];
    int n = 0;
    check = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, check, a_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(dpy, check, a_name, a_utf8, 8, PropModeReplace, (const unsigned char *)"XenithOS", 8);
    XChangeProperty(dpy, root, a_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    supported[n++] = a_supported;
    supported[n++] = a_check;
    supported[n++] = a_name;
    supported[n++] = a_state;
    supported[n++] = a_fullscreen;
    supported[n++] = a_active;
    supported[n++] = a_type;
    supported[n++] = a_dialog;
    supported[n++] = a_splash;
    supported[n++] = a_utility;
    XChangeProperty(dpy, root, a_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, n);
}

int kiosk_wm_start(unsigned long self_window)
{
    if (dpy) return 1;
    if (!(dpy = XOpenDisplay(NULL))) return 0;
    root = DefaultRootWindow(dpy);
    scr_w = DisplayWidth(dpy, DefaultScreen(dpy));
    scr_h = DisplayHeight(dpy, DefaultScreen(dpy));
    self = (Window)self_window;
    other_wm = 0;
    XSetErrorHandler(wm_check);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(dpy, False);
    XSetErrorHandler(quiet_errors);
    if (other_wm) {                          /* a desktop is already managing windows */
        XCloseDisplay(dpy);
        dpy = NULL;
        return 0;
    }
    a_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    a_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    a_splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    a_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    a_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    a_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    a_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    ewmh_start();
    /* Nothing else gives XenithOS the keyboard on its own X server. */
    XRaiseWindow(dpy, self);
    XSetInputFocus(dpy, self, RevertToPointerRoot, CurrentTime);
    XFlush(dpy);
    return 1;
}

int kiosk_wm_running(void)
{
    return dpy != NULL;
}

/* A dialog or a window that can't be resized: it keeps its size, in the middle. */
static int keeps_size(Window w)
{
    XSizeHints hints;
    long supplied = 0;
    Window parent = None;
    Atom type, *types = NULL;
    int format, keep = 0;
    unsigned long n = 0, left;
    if (XGetTransientForHint(dpy, w, &parent) && parent != None) return 1;
    if (XGetWMNormalHints(dpy, w, &hints, &supplied) && (hints.flags & PMinSize) && (hints.flags & PMaxSize) &&
        hints.min_width == hints.max_width && hints.min_height == hints.max_height)
        return 1;
    if (XGetWindowProperty(dpy, w, a_type, 0, 8, False, XA_ATOM, &type, &format, &n, &left, (unsigned char **)&types) == Success && types) {
        unsigned long i;
        for (i = 0; i < n; i++)
            if (types[i] == a_dialog || types[i] == a_splash || types[i] == a_utility) keep = 1;
        XFree(types);
    }
    return keep;
}

/* Tells a window where it is when we didn't move it (ICCCM: it may be waiting to hear). */
static void tell(Window w, int x, int y, int ww, int wh)
{
    XConfigureEvent ce;
    memset(&ce, 0, sizeof ce);
    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = w;
    ce.window = w;
    ce.x = x;
    ce.y = y;
    ce.width = ww;
    ce.height = wh;
    XSendEvent(dpy, w, False, StructureNotifyMask, (XEvent *)&ce);
}

/* Whether a window has asked for full screen (_NET_WM_STATE_FULLSCREEN, before it was mapped or
 * since). */
static int wants_fullscreen(Window w)
{
    Atom type, *states = NULL;
    int format, on = 0;
    unsigned long n = 0, left, i;
    if (XGetWindowProperty(dpy, w, a_state, 0, 16, False, XA_ATOM, &type, &format, &n, &left, (unsigned char **)&states) == Success && states) {
        for (i = 0; i < n; i++) if (states[i] == a_fullscreen) on = 1;
        XFree(states);
    }
    return on;
}

static void place(Window w)
{
    XWindowAttributes at;
    if (keeps_size(w) && !wants_fullscreen(w) && XGetWindowAttributes(dpy, w, &at)) {
        int ww = at.width < scr_w ? at.width : scr_w, wh = at.height < scr_h ? at.height : scr_h;
        XMoveResizeWindow(dpy, w, (scr_w - ww) / 2, (scr_h - wh) / 2, ww, wh);
        tell(w, (scr_w - ww) / 2, (scr_h - wh) / 2, ww, wh);
    } else {
        XMoveResizeWindow(dpy, w, 0, 0, scr_w, scr_h);
        tell(w, 0, 0, scr_w, scr_h);
    }
    XSetWindowBorderWidth(dpy, w, 0);
}

static int find(Window w)
{
    int i;
    for (i = 0; i < nshown; i++) if (shown[i] == w) return i;
    return -1;
}

static void forget(Window w)
{
    int i = find(w);
    if (i < 0) return;
    memmove(&shown[i], &shown[i + 1], sizeof shown[0] * (size_t)(nshown - i - 1));
    nshown--;
}

/* The newest window in front with the keyboard; all of the app's windows over XenithOS's in
 * their order (a dialog's main window shows around it), unless XenithOS is holding the front. */
static void focus(Window w)
{
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, a_active, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

static void focus_top(void)
{
    int i;
    if (hold || !nshown) {
        XRaiseWindow(dpy, self);
        focus(self);
        return;
    }
    for (i = 0; i < nshown; i++) XRaiseWindow(dpy, shown[i]);
    focus(shown[nshown - 1]);
}

/* A window asking to go full screen or back (_NET_WM_STATE). App windows already fill the screen;
 * this matters for ones that keep their size, and so the window knows where it stands. */
static void state_request(const XClientMessageEvent *m)
{
    Window w = m->window;
    int on;
    if ((Atom)m->data.l[1] != a_fullscreen && (Atom)m->data.l[2] != a_fullscreen) return;
    on = m->data.l[0] == 1 || (m->data.l[0] == 2 && !wants_fullscreen(w));
    if (on) XChangeProperty(dpy, w, a_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&a_fullscreen, 1);
    else XDeleteProperty(dpy, w, a_state);
    if (w != self && find(w) >= 0) place(w);
}

void kiosk_wm_poll(void)
{
    if (!dpy) return;
    while (XPending(dpy)) {
        XEvent e;
        XNextEvent(dpy, &e);
        switch (e.type) {
        case MapRequest: {
            Window w = e.xmaprequest.window;
            if (w != self) {
                place(w);
                forget(w);
                if (nshown < MAX_WINDOWS) shown[nshown++] = w;
            }
            if (hold && w != self) {         /* behind XenithOS until the game is back */
                XMapWindow(dpy, w);
                focus_top();
                break;
            }
            XMapRaised(dpy, w);
            if (w != self) focus(w);
            break;
        }
        case MapNotify: {
            /* A game that makes its own window full screen without asking (override-redirect)
             * still counts as an app's window: it gets the keyboard, and XenithOS stops drawing
             * under it. Menus and tooltips do that too, but they're small. */
            Window w = e.xmap.window;
            XWindowAttributes at;
            if (e.xmap.override_redirect && w != self && find(w) < 0 && XGetWindowAttributes(dpy, w, &at) &&
                    (long)at.width * at.height >= (long)scr_w * scr_h / 2) {
                if (nshown < MAX_WINDOWS) shown[nshown++] = w;
                if (hold) focus_top();
                else focus(w);
            }
            break;
        }
        case ConfigureRequest: {
            XConfigureRequestEvent *r = &e.xconfigurerequest;
            XWindowChanges c;
            if (r->window != self && find(r->window) >= 0) {   /* on screen: it stays where we put it */
                place(r->window);
                break;
            }
            memset(&c, 0, sizeof c);
            c.x = r->x;
            c.y = r->y;
            c.width = r->width;
            c.height = r->height;
            c.border_width = r->border_width;
            c.sibling = r->above;
            c.stack_mode = r->detail;
            XConfigureWindow(dpy, r->window, (unsigned)r->value_mask, &c);
            break;
        }
        case UnmapNotify:
            if (find(e.xunmap.window) >= 0) {
                forget(e.xunmap.window);
                focus_top();
            }
            break;
        case DestroyNotify:
            if (find(e.xdestroywindow.window) >= 0) {
                forget(e.xdestroywindow.window);
                focus_top();
            }
            break;
        case ClientMessage:
            if (e.xclient.message_type == a_active && find(e.xclient.window) >= 0) {
                forget(e.xclient.window);
                shown[nshown++] = e.xclient.window;
                focus_top();
            } else if (e.xclient.message_type == a_active && e.xclient.window == self && (hold || !nshown)) {
                focus_top();                 /* SDL_RaiseWindow asks this way once there's EWMH */
            } else if (e.xclient.message_type == a_state) {
                state_request(&e.xclient);
            }
            break;
        }
    }
    XFlush(dpy);
}

int kiosk_wm_windows(void)
{
    return nshown;
}

/* The game in front keeps the keyboard: if it has ended up with XenithOS's window or nobody (a
 * window that closed, a focus request that came too early), it goes back to the game. */
void kiosk_wm_keep_focus(void)
{
    Window f = None;
    int revert;
    if (!dpy || hold || !nshown) return;
    XGetInputFocus(dpy, &f, &revert);
    if (f == self || f == root || f == None || f == PointerRoot) {
        focus(shown[nshown - 1]);
        XFlush(dpy);
    }
}

/* The title of the app's window in front ("" when there's none). */
void kiosk_wm_top_title(char *out, int n)
{
    char *name = NULL;
    out[0] = 0;
    if (!dpy || !nshown || !XFetchName(dpy, shown[nshown - 1], &name) || !name) return;
    snprintf(out, (size_t)n, "%s", name);
    XFree(name);
}

void kiosk_wm_show_self(void)
{
    if (!dpy) return;
    nshown = 0;                              /* whatever the app left behind goes under us */
    hold = 0;
    focus_top();
    XFlush(dpy);
}

void kiosk_wm_hold(int on)
{
    if (!dpy) return;
    hold = on;
    focus_top();
    XFlush(dpy);
}

/* The screen as it is, scaled to w x h (0x00RRGGBB), for the guide to draw over. */
int kiosk_wm_capture(uint32_t *out, int w, int h)
{
    XImage *im;
    int x, y, fast;
    if (!dpy || w <= 0 || h <= 0) return 0;
    im = XGetImage(dpy, root, 0, 0, (unsigned)scr_w, (unsigned)scr_h, AllPlanes, ZPixmap);
    if (!im) return 0;
    fast = im->bits_per_pixel == 32 && im->red_mask == 0xff0000 && im->green_mask == 0xff00 && im->blue_mask == 0xff;
    for (y = 0; y < h; y++) {
        int sy = y * scr_h / h;
        const uint32_t *line = (const uint32_t *)(im->data + (size_t)sy * im->bytes_per_line);
        for (x = 0; x < w; x++) {
            int sx = x * scr_w / w;
            if (fast) {
                out[y * w + x] = line[sx] & 0xffffff;
            } else {
                unsigned long p = XGetPixel(im, sx, sy);
                unsigned long m[3] = {im->red_mask, im->green_mask, im->blue_mask};
                uint32_t c = 0;
                int k;
                for (k = 0; k < 3; k++) {    /* each channel scaled to 8 bits, whatever its mask */
                    unsigned long v = m[k] ? p & m[k] : 0, mm = m[k];
                    while (mm && !(mm & 1)) { mm >>= 1; v >>= 1; }
                    c = (c << 8) | (uint32_t)(mm ? v * 255 / mm : 0);
                }
                out[y * w + x] = c;
            }
        }
    }
    XDestroyImage(im);
    return 1;
}

/* --------------------------------------------------- the controller as a pointer (XTEST) */
typedef int (*xt_motion_fn)(Display *, int, int, unsigned long);
typedef int (*xt_button_fn)(Display *, unsigned int, Bool, unsigned long);
typedef int (*xt_key_fn)(Display *, unsigned int, Bool, unsigned long);
static xt_motion_fn xt_motion;
static xt_button_fn xt_button;
static xt_key_fn xt_key;

int kiosk_wm_can_point(void)
{
    static int tried;
    if (!tried) {
        void *lib = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
        tried = 1;
        if (lib) {
            xt_motion = (xt_motion_fn)dlsym(lib, "XTestFakeRelativeMotionEvent");
            xt_button = (xt_button_fn)dlsym(lib, "XTestFakeButtonEvent");
            xt_key = (xt_key_fn)dlsym(lib, "XTestFakeKeyEvent");
        }
    }
    return dpy && xt_motion && xt_button && xt_key;
}

void kiosk_wm_pointer_move(int dx, int dy)
{
    if (!kiosk_wm_can_point() || (!dx && !dy)) return;
    xt_motion(dpy, dx, dy, CurrentTime);
    XFlush(dpy);
}

void kiosk_wm_pointer_button(int button, int down)
{
    if (!kiosk_wm_can_point()) return;
    xt_button(dpy, (unsigned)button, down ? True : False, CurrentTime);
    XFlush(dpy);
}

void kiosk_wm_key(unsigned long keysym, int down)
{
    KeyCode kc;
    if (!kiosk_wm_can_point() || !(kc = XKeysymToKeycode(dpy, (KeySym)keysym))) return;
    xt_key(dpy, kc, down ? True : False, CurrentTime);
    XFlush(dpy);
}
