/* SETTINGS > BLUETOOTH CONTROLLERS (PCs and Pis): pairing a controller with the controller alone.
 *
 * Opening it turns Bluetooth on and looks for devices nearby (plat_bt_* in platform.h). The
 * controllers it hears are listed; choosing one pairs it, trusts it so it comes back by itself
 * whenever it's turned on, and connects it, each step shown as it goes. A paired one can be
 * connected, disconnected or forgotten. Y shows every device, for a controller that doesn't say
 * what it is. Leaving stops looking; what's paired stays paired.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "gfx.h"
#include "icons.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define MAXSHOW 64

static struct plat_bt_device all[MAXSHOW], shown[MAXSHOW];

static int order(const void *a, const void *b)
{
    const struct plat_bt_device *x = a, *y = b;
    if (x->connected != y->connected) return y->connected - x->connected;
    if (x->paired != y->paired) return y->paired - x->paired;
    if ((x->rssi != 0) != (y->rssi != 0)) return (y->rssi != 0) - (x->rssi != 0);
    if (x->rssi != y->rssi) return y->rssi - x->rssi;
    return strcmp(x->name[0] ? x->name : x->addr, y->name[0] ? y->name : y->addr);
}

static int list(int every)
{
    int n = plat_bt_devices(all, MAXSHOW), i, k = 0;
    for (i = 0; i < n; i++)
        if (all[i].gamepad || (every && (all[i].name[0] || all[i].paired))) shown[k++] = all[i];
    qsort(shown, k, sizeof shown[0], order);
    return k;
}

static const char *name_of(const struct plat_bt_device *d)
{
    return d->name[0] ? d->name : "A controller with no name";
}

/* Signal bars, from how loud it is. */
static void bars(int x, int cy, int rssi, uint32_t on, uint32_t off)
{
    int k, lit = rssi == 0 ? 0 : rssi > -60 ? 3 : rssi > -75 ? 2 : 1;
    for (k = 0; k < 3; k++) {
        int h = 6 + k * 5;
        gfx_fill_rrect((float)(x + k * 7), (float)(cy + 8 - h), 4, (float)h, 2, k < lit ? on : off, 255);
    }
}

static const char *const steps[] = {
    "", "Pairing", "Setting it to come back by itself", "Connecting", "Disconnecting", "Forgetting it",
};

/* The step under way, over the list. */
static void working(const struct plat_bt_status *s, const char *name)
{
    int pw = SCR_W * 46 / 100, ph = 150 + gfx_line_h(FONT_TITLE) + gfx_line_h(FONT_BODY), px = (SCR_W - pw) / 2, py = (SCR_H - ph) / 2;
    char title[96];
    gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
    gfx_fill_rrect((float)px, (float)py, (float)pw, (float)ph, 14, C_SURFACE, 255);
    gfx_stroke_rrect((float)px, (float)py, (float)pw, (float)ph, 14, theme_contrast() ? 2 : 1, C_LINE, 255);
    snprintf(title, sizeof title, "%s", name);
    gfx_str_fit(FONT_TITLE, px + 32, py + 28, C_TEXT, title, pw - 64);
    gfx_str(FONT_BODY, px + 32, py + 36 + gfx_line_h(FONT_TITLE), C_SUB, steps[s->step < 6 ? s->step : 0]);
    ui_spinner(px + 52, py + ph - 52, 18, C_ACCENT);
    /* the three steps of pairing, as dots */
    if (s->step <= BT_CONNECTING) {
        int k;
        for (k = 1; k <= 3; k++)
            gfx_circle(px + pw - 118.0f + k * 22, py + ph - 52.0f, 6, k < s->step ? C_OK : k == s->step ? C_TEXT : C_SURFACE2, 255);
    }
}

void screen_bluetooth(void)
{
    struct plat_bt_status s;
    int sel = 0, first = 0, every = 0, n, i, last_step = BT_IDLE;
    char busy_name[64] = "", title[96], body[200], sel_addr[18] = "";
    float hl = 0;

    ui_loading_frame("Bluetooth", "Turning it on");
    plat_bt_open();
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, lw = (SCR_W - 2 * M) * 58 / 100, rx = M + lw + 24, rw = SCR_W - M - rx;
        int rowh = gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 26, visible, d, busy;
        plat_bt_poll(&s);
        n = s.state == BT_FAILED ? 0 : list(every);
        busy = s.step >= BT_PAIRING && s.step <= BT_FORGETTING;
        visible = (SCR_H - top - 70) / rowh;
        if (visible < 1) visible = 1;
        for (i = 0; sel_addr[0] && i < n; i++)          /* the list moves (connected first): so does sel */
            if (!strcmp(shown[i].addr, sel_addr)) sel = i;
        if (sel >= n) sel = n ? n - 1 : 0;

        /* a step has just ended */
        if (s.step != last_step && (s.step == BT_DONE || s.step == BT_STEP_FAILED)) {
            if (s.step == BT_DONE && last_step == BT_FORGETTING) {
                ui_toast_icon(ICON_CHECK);
                snprintf(title, sizeof title, "Forgot %s", busy_name);
                ui_toast(title, "Pair it again to use it here.");
            } else if (s.step == BT_DONE && last_step == BT_DISCONNECTING) {
                snprintf(title, sizeof title, "%s is disconnected", busy_name);
                ui_toast(title, "It stays paired. Press its home button to connect it again.");
            } else if (s.step == BT_DONE) {
                sfx(SFX_SUCCESS);
                ui_toast_icon(ICON_CONTROLLER);
                snprintf(title, sizeof title, "%s is connected", busy_name);
                ui_toast(title, "It's paired, and comes back by itself whenever it's turned on.");
            } else {
                sfx(SFX_ERROR);
                snprintf(title, sizeof title, "%s didn't %s", busy_name,
                         last_step == BT_FORGETTING ? "go" : last_step == BT_DISCONNECTING ? "disconnect" : "connect");
                ui_message(title, s.why);
                ui_fade_in();
            }
        }
        last_step = s.step;

        ui_poll();
        d = ui_nav_y();
        if (!busy && d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) {
            sfx(SFX_BACK);
            plat_bt_close();
            return;
        }
        if (!busy && PRESSED(y) && s.state != BT_FAILED) {
            every = !every;
            sel = first = 0;
            sfx(SFX_SELECT);
        }
        if (!busy && n && PRESSED(a)) {
            struct plat_bt_device *dv = &shown[sel];
            snprintf(busy_name, sizeof busy_name, "%s", name_of(dv));
            sfx(SFX_SELECT);
            if (!dv->paired || !dv->trusted) { plat_bt_pair(dv->addr); last_step = BT_PAIRING; }
            else if (!dv->connected) { plat_bt_connect(dv->addr); last_step = BT_CONNECTING; }
            else {
                static const char *const opts[] = {"Disconnect it", "Forget it", "Cancel"};
                snprintf(body, sizeof body, "%s is paired and connected.", busy_name);
                i = ui_dialog(busy_name, body, opts, 3);
                if (i == 0) { plat_bt_disconnect(dv->addr); last_step = BT_DISCONNECTING; }
                else if (i == 1) { plat_bt_forget(dv->addr); last_step = BT_FORGETTING; }
                ui_fade_in();
            }
        } else if (!busy && n && PRESSED(x) && shown[sel].paired) {
            snprintf(busy_name, sizeof busy_name, "%s", name_of(&shown[sel]));
            snprintf(title, sizeof title, "Forget %s?", busy_name);
            if (ui_confirm(title, "It won't connect here any more until it's paired again.", "Forget it", "Cancel"))
                { plat_bt_forget(shown[sel].addr); last_step = BT_FORGETTING; }
            ui_fade_in();
        }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        if (n) snprintf(sel_addr, sizeof sel_addr, "%s", shown[sel < n ? sel : 0].addr);

        ui_bg();
        ui_header("Bluetooth controllers", s.state == BT_FAILED ? "Pair a controller here, with the controller alone"
                  : s.scanning ? "Looking for controllers nearby" : "Pair a controller here, with the controller alone");
        if (s.state == BT_FAILED) {
            ui_panel(M, top, lw, SCR_H - top - 70);
            icon_draw(ICON_CONTROLLER, M + lw / 2.0f, top + 90.0f, 64, C_DIM, C_SURFACE);
            gfx_str_c(FONT_LABEL, M + lw / 2, top + 140, C_TEXT, "Bluetooth isn't working");
            gfx_wrap(FONT_BODY, M + 32, top + 140 + gfx_line_h(FONT_LABEL) + 14, lw - 64, 4, C_SUB, s.why, 6);
        } else if (s.state == BT_STARTING && !n) {
            ui_panel(M, top, lw, SCR_H - top - 70);
            ui_spinner(M + lw / 2, top + 110, 22, C_ACCENT);
            gfx_str_c(FONT_BODY, M + lw / 2, top + 156, C_SUB, "Turning Bluetooth on");
        } else if (!n) {
            ui_panel(M, top, lw, SCR_H - top - 70);
            ui_spinner(M + lw / 2, top + 110, 22, C_ACCENT);
            gfx_str_c(FONT_LABEL, M + lw / 2, top + 156, C_TEXT, every ? "Nothing nearby yet" : "No controllers nearby yet");
            gfx_wrap(FONT_BODY, M + 32, top + 156 + gfx_line_h(FONT_LABEL) + 14, lw - 64, 4, C_SUB,
                     "Put your controller in pairing mode and keep it close. It shows up here as soon as it's heard.", 4);
        } else {
            hl = ui_ease(hl, (float)(sel - first), 0.45f);
            ui_select_box((float)M, top + hl * rowh, (float)lw, rowh - 6.0f, 10);
            for (i = 0; i < visible && first + i < n; i++) {
                struct plat_bt_device *dv = &shown[first + i];
                int y = top + i * rowh, on = first + i == sel, ty = y + (rowh - 6 - gfx_line_h(FONT_BODY_SB) - gfx_line_h(FONT_SMALL)) / 2;
                const char *what = dv->connected ? "Connected" : dv->paired && dv->trusted ? "Paired, turned off or asleep"
                                 : dv->paired ? "Paired" : dv->gamepad ? "Ready to pair" : "A device, maybe not a controller";
                icon_draw(dv->gamepad ? ICON_CONTROLLER : ICON_NETWORK, M + 40.0f, y + (rowh - 6) / 2.0f, 34,
                          on ? 0xFFFFFF : C_SUB, on ? C_SEL : C_BG);
                gfx_str_fit(FONT_BODY_SB, M + 74, ty, on ? 0xFFFFFF : C_TEXT, name_of(dv), lw - 74 - 70);
                if (dv->connected) gfx_circle(M + 80.0f, ty + gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) / 2.0f, 4, C_OK, 255);
                gfx_str_fit(FONT_SMALL, M + (dv->connected ? 90 : 74), ty + gfx_line_h(FONT_BODY_SB), C_SUB, what, lw - 74 - 70);
                if (!dv->connected) bars(M + lw - 50, y + (rowh - 6) / 2, dv->rssi, on ? 0xFFFFFF : C_SUB, C_SURFACE2);
            }
        }
        /* how to put the usual controllers in pairing mode */
        ui_panel(rx, top, rw, SCR_H - top - 70);
        {
            int y = top + 22, w = rw - 48;
            gfx_str(FONT_BODY_SB, rx + 24, y, C_TEXT, "Pairing mode");
            y += gfx_line_h(FONT_BODY_SB) + 10;
            y += (gfx_line_h(FONT_SMALL) + 3) * gfx_wrap(FONT_SMALL, rx + 24, y, w, 3, C_SUB,
                 "Xbox controllers: hold the pair button on top until the Xbox button flashes quickly.", 4) + 10;
            y += (gfx_line_h(FONT_SMALL) + 3) * gfx_wrap(FONT_SMALL, rx + 24, y, w, 3, C_SUB,
                 "PlayStation controllers: hold PS and Share, or Create on newer ones, until the light flashes.", 4) + 10;
            y += (gfx_line_h(FONT_SMALL) + 3) * gfx_wrap(FONT_SMALL, rx + 24, y, w, 3, C_SUB,
                 "Switch Pro and others: hold the small sync or pair button until the lights run.", 4) + 10;
            gfx_wrap(FONT_SMALL, rx + 24, y, w, 3, C_DIM,
                     "Once it's paired it comes back by itself: just turn it on.", 3);
        }
        if (busy) working(&s, busy_name);
        if (busy) ui_hints("B=Leave");
        else if (s.state == BT_FAILED) ui_hints("B=Back");
        else if (!n) ui_hints(every ? "Y=Only controllers;B=Back" : "Y=Show every device;B=Back");
        else {
            const struct plat_bt_device *dv = &shown[sel];
            snprintf(body, sizeof body, "%s;%sY=%s;B=Back",
                     !dv->paired || !dv->trusted ? "A=Pair" : !dv->connected ? "A=Connect" : "A=Options",
                     dv->paired ? "X=Forget;" : "", every ? "Only controllers" : "Show every device");
            ui_hints(body);
        }
        ui_end_frame();
    }
}
