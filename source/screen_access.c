/* SETTINGS > ACCESSIBILITY: bigger text, high contrast, and the controller's buttons and sticks
 * moved to other jobs. Each one is put to use straight away, here and in every app; leaving
 * saves them. */
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

/* ---------------------------------------------------------------- buttons */

static const char *const jobs[PADB_COUNT] = {
    "Choose and confirm", "Go back", "Button X", "Button Y", "Left bumper", "Right bumper",
    "Left trigger", "Right trigger", "Start", "Back", "Left stick, pressed in", "Right stick, pressed in",
};

static int moved_count(void)
{
    int b, n = 0;
    for (b = 0; b < PADB_COUNT; b++)
        if (ui_pad_from(b) != b) n++;
    return n;
}

/* What Accessibility's row says about them. */
static void buttons_value(char *out, int n)
{
    int b, k = moved_count();
    if (!k) { snprintf(out, n, "As they come"); return; }
    for (b = 0; b < PADB_COUNT && k == 2; b++)
        if (ui_pad_from(b) != b) {
            snprintf(out, n, "%s and %s swapped", ui_pad_name(b), ui_pad_name(ui_pad_from(b)));
            return;
        }
    snprintf(out, n, "%d moved", k);
}

/* Job j is done by button p from now on. The job p did goes to j's old button, so every job
 * still has a button, and the D-pad (never moved) can always get back here. */
static void give(int j, int p)
{
    int k, old = ui_pad_from(j);
    for (k = 0; k < PADB_COUNT && ui_pad_from(k) != p; k++)
        ;
    if (k == PADB_COUNT || k == j) return;
    st.pad_map[j] = (signed char)(p == j ? 0 : p + 1);
    st.pad_map[k] = (signed char)(old == k ? 0 : old + 1);
}

static void buttons_screen(void)
{
    int sel = 0, first = 0, i, waiting = -1, changed = 0;
    uint64_t wait_from = 0;
    struct pad raw_before;
    float hl = 0;
    char title[64], body[96];
    ui_fade_in();
    for (;;) {
        int n = PADB_COUNT + 1, top = HEADER_H + 16, rowh = gfx_line_h(FONT_BODY_SB) + 30, w = SCR_W - 2 * M;
        int visible = (SCR_H - top - 70) / rowh, d;
        ui_poll();
        if (waiting >= 0) {
            /* The next button pressed on the controller itself (one held when asking doesn't
             * count until it's let go), or nothing for five seconds and it stays as it was. */
            int b, got = -1;
            for (b = 0; b < PADB_COUNT && got < 0; b++)
                if (ui_pad_held(&ui_raw, b) && !ui_pad_held(&raw_before, b)) got = b;
            raw_before = ui_raw;
            if (got >= 0) {
                int old = ui_pad_from(waiting);
                give(waiting, got);
                changed = 1;
                sfx(SFX_SELECT);
                if (got != old) {
                    snprintf(title, sizeof title, "%s now does %s's job", ui_pad_name(got), ui_pad_name(waiting));
                    snprintf(body, sizeof body, "And %s does what %s did.", ui_pad_name(old), ui_pad_name(got));
                    ui_toast(title, body);
                }
                waiting = -1;
                ui_settle_input();          /* that press was for this, not for the screen */
            } else if (ui_ms() - wait_from > 5000) {
                waiting = -1;
                sfx(SFX_BACK);
                ui_settle_input();
            }
        } else {
            d = ui_nav_y();
            if (d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
            if (PRESSED(b)) {
                if (changed) settings_save();
                sfx(SFX_BACK);
                return;
            }
            if (PRESSED(a)) {
                if (sel < PADB_COUNT) {
                    waiting = sel;
                    wait_from = ui_ms();
                    raw_before = ui_raw;
                    sfx(SFX_SELECT);
                } else if (moved_count()) {
                    memset(st.pad_map, 0, sizeof st.pad_map);
                    changed = 1;
                    sfx(SFX_SELECT);
                    ui_toast("Every button is back", "Each one does its own job again.");
                }
            }
        }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;

        ui_bg();
        ui_header("Buttons", "Which button does each job. Choose one, then press the button for it.");
        hl = ui_ease(hl, (float)(sel - first), 0.45f);
        ui_select_box((float)M, top + hl * rowh, (float)w, rowh - 6.0f, 10);
        for (i = 0; i < visible && first + i < n; i++) {
            int idx = first + i, y = top + i * rowh, cy = y + (rowh - 6) / 2, s = idx == sel;
            if (idx == PADB_COUNT) {
                gfx_str_vc(FONT_BODY_SB, M + 22, cy, s ? 0xFFFFFF : C_TEXT, "Put every button back");
                continue;
            }
            {
                int from = ui_pad_from(idx), rx, lx = M + 22 + gfx_str_w(FONT_SMALL, "START") + 20 + 16;
                ui_button_glyph_as(M + 22, cy, ui_pad_name(idx));
                gfx_str_vc(FONT_BODY_SB, lx, cy, s ? 0xFFFFFF : C_TEXT, jobs[idx]);
                /* the button doing it now, on the right */
                rx = M + w - 22 - gfx_str_w(FONT_SMALL, ui_pad_name(from)) - 20;
                if (from <= PADB_Y) rx = M + w - 22 - (gfx_line_h(FONT_SMALL) + 4);
                ui_button_glyph_as(rx, cy, ui_pad_name(from));
                if (from != idx) gfx_str_r(FONT_SMALL, rx - 12, cy - gfx_line_h(FONT_SMALL) / 2, C_ACCENT_HI, "Moved");
            }
        }
        if (waiting >= 0) {
            /* asking, over the list */
            int pw = SCR_W * 46 / 100, ph = 190 + 2 * (gfx_line_h(FONT_BODY) - 21), px = (SCR_W - pw) / 2, py = (SCR_H - ph) / 2;
            float f = (ui_ms() - wait_from) / 5000.0f;
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
            gfx_fill_rrect((float)px, (float)py, (float)pw, (float)ph, 14, C_SURFACE, 255);
            gfx_stroke_rrect((float)px, (float)py, (float)pw, (float)ph, 14, theme_contrast() ? 2 : 1, C_LINE, 255);
            snprintf(title, sizeof title, "Press the button for %s's job", ui_pad_name(waiting));
            gfx_str_fit(FONT_TITLE, px + 32, py + 26, C_TEXT, title, pw - 64);
            gfx_str(FONT_BODY, px + 32, py + 34 + gfx_line_h(FONT_TITLE), C_SUB, jobs[waiting]);
            gfx_str(FONT_BODY, px + 32, py + 40 + gfx_line_h(FONT_TITLE) + gfx_line_h(FONT_BODY), C_SUB,
                    "Or wait, and it stays as it is.");
            gfx_ring(px + pw - 44.0f, py + ph - 44.0f, 14, 4, C_SURFACE2, 255);
            gfx_arc(px + pw - 44.0f, py + ph - 44.0f, 14, 4, -1.5708f, -1.5708f + 6.2832f * (1 - f), C_ACCENT, 255);
        }
        ui_hints(waiting >= 0 ? "" : sel < PADB_COUNT ? "A=Choose a button for it;B=Back" : "A=Put them back;B=Back");
        ui_end_frame();
    }
}

/* ------------------------------------------------------------------ screen */

enum { A_TEXT, A_CONTRAST, A_BUTTONS, A_STICKS, A_COUNT };

static const char *const label[A_COUNT] = {"Bigger text", "High contrast", "Buttons", "Swap the sticks"};
static const char *const about[A_COUNT] = {
    "Words a size up, in XenithOS and its apps.",
    "White words on black, and edges that stand out.",
    "Have any button do another one's job.",
    "The left stick does the right stick's job, and the other way round.",
};

void screen_access(void)
{
    int sel = 0, changed = 0, i;
    float hl = 0;
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, rowh = gfx_line_h(FONT_BODY_SB) + gfx_line_h(FONT_SMALL) + 30, w = SCR_W - 2 * M, d;
        char v[48];
        ui_poll();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < A_COUNT) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) {
            if (changed) settings_save();
            sfx(SFX_BACK);
            return;
        }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            switch (sel) {
            case A_TEXT:
                st.text_large = !st.text_large;
                gfx_text_size(st.text_large);
                changed = 1;
                break;
            case A_CONTRAST:
                st.high_contrast = !st.high_contrast;
                theme_set_contrast(st.high_contrast);
                changed = 1;
                break;
            case A_BUTTONS:
                buttons_screen();
                ui_fade_in();
                break;
            case A_STICKS:
                st.swap_sticks = !st.swap_sticks;
                changed = 1;
                break;
            }
        }

        ui_bg();
        ui_header("Accessibility", "For XenithOS and every app. Changes are saved to your USB stick");
        hl = ui_ease(hl, (float)sel, 0.45f);
        ui_select_box((float)M, top + hl * rowh, (float)w, rowh - 6.0f, 10);
        for (i = 0; i < A_COUNT; i++) {
            int y = top + i * rowh, s = i == sel, ty = y + (rowh - 6 - gfx_line_h(FONT_BODY_SB) - gfx_line_h(FONT_SMALL) - 2) / 2;
            gfx_str(FONT_BODY_SB, M + 22, ty, s ? 0xFFFFFF : C_TEXT, label[i]);
            gfx_str_fit(FONT_SMALL, M + 22, ty + gfx_line_h(FONT_BODY_SB) + 2, C_SUB, about[i], w - 44 - 260);
            if (i == A_BUTTONS) buttons_value(v, sizeof v);
            else snprintf(v, sizeof v, "%s", (i == A_TEXT ? st.text_large : i == A_CONTRAST ? st.high_contrast : st.swap_sticks) ? "On" : "Off");
            gfx_str_r(FONT_BODY_SB, M + w - (i == A_BUTTONS ? 46 : 22), y + (rowh - 6) / 2 - gfx_line_h(FONT_BODY_SB) / 2,
                      s ? 0xFFFFFF : C_SUB, v);
            if (i == A_BUTTONS) {
                int cy = y + (rowh - 6) / 2;
                gfx_line(M + w - 30.0f, cy - 7.0f, M + w - 24.0f, (float)cy, 3, s ? 0xFFFFFF : C_DIM, 255);
                gfx_line(M + w - 30.0f, cy + 7.0f, M + w - 24.0f, (float)cy, 3, s ? 0xFFFFFF : C_DIM, 255);
            }
        }
        ui_hints(sel == A_BUTTONS ? "A=Open;B=Back" : "A=Switch;B=Back");
        ui_end_frame();
    }
}
