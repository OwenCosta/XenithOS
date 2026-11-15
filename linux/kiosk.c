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
 */
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
static Atom a_type, a_dialog, a_splash, a_utility, a_active;
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

static void place(Window w)
{
    XWindowAttributes at;
    if (keeps_size(w) && XGetWindowAttributes(dpy, w, &at)) {
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

static void focus_top(void)
{
    Window w = nshown ? shown[nshown - 1] : self;
    XRaiseWindow(dpy, w);
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
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
            XMapRaised(dpy, w);
            if (w != self) XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
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

void kiosk_wm_show_self(void)
{
    if (!dpy) return;
    nshown = 0;                              /* whatever the app left behind goes under us */
    focus_top();
    XFlush(dpy);
}
