/* On-screen keyboard for gamertags and passcodes. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "gfx.h"
#include "platform.h"
#include "keyboard.h"
#include "theme.h"
#include "ui.h"

enum { K_SHIFT = 1, K_SPACE, K_DEL, K_DONE };

struct key { int code; char label[8]; int row; float x, w; };   /* x, w in key units */

static struct key keys[64];
static int nkeys, nrows;

static void add(int row, float x, float w, int code, const char *label)
{
    struct key *k = &keys[nkeys++];
    k->row = row; k->x = x; k->w = w; k->code = code;
    snprintf(k->label, sizeof k->label, "%s", label);
}

static void layout(int mode)
{
    nkeys = 0;
    if (mode == KB_PIN) {
        static const char *const rows[] = {"123", "456", "789"};
        int r, i;
        for (r = 0; r < 3; r++)
            for (i = 0; i < 3; i++) {
                char l[2] = {rows[r][i], 0};
                add(r, (float)i, 1, rows[r][i], l);
            }
        add(3, 0, 1, K_DEL, "Del");
        add(3, 1, 1, '0', "0");
        add(3, 2, 1, K_DONE, "Done");
        nrows = 4;
    } else {
        static const char *const rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
        int r, i;
        for (r = 0; r < 4; r++) {
            int n = (int)strlen(rows[r]);
            float off = (10 - n) / 2.0f;
            for (i = 0; i < n; i++) {
                char l[2] = {rows[r][i], 0};
                add(r, off + i, 1, rows[r][i], l);
            }
        }
        add(4, 0, 2, K_SHIFT, "Shift");
        add(4, 2, 4, K_SPACE, "Space");
        add(4, 6, 2, K_DEL, "Delete");
        add(4, 8, 2, K_DONE, "Done");
        nrows = 5;
    }
}

static int find_near(int row, float cx)
{
    int i, best = -1;
    float bd = 1e9f;
    for (i = 0; i < nkeys; i++) {
        if (keys[i].row != row) continue;
        {
            float d = fabsf(keys[i].x + keys[i].w / 2 - cx);
            if (d < bd) { bd = d; best = i; }
        }
    }
    return best;
}

int keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode)
{
    int sel = 0, shift = 1, i;
    float unit = mode == KB_PIN ? 104.0f : 70.0f, kh = mode == KB_PIN ? 72.0f : 58.0f, gap = 10;
    float hx = 0, hy = 0;

    layout(mode);
    if (mode == KB_TEXT && buf[0]) shift = 0;
    ui_fade_in();
    plat_text_input(1);

    for (;;) {
        int cols = mode == KB_PIN ? 3 : 10, dx, dy, len = (int)strlen(buf), nt;
        float total = cols * unit - gap, ox = (SCR_W - total) / 2.0f;
        int fy = HEADER_H + 40, ky = fy + 110;
        struct key *k = &keys[sel];
        char typed[32];

        ui_poll();

        /* A real keyboard, where there is one: type straight into the field. */
        nt = plat_text_take(typed, sizeof typed);
        for (i = 0; i < nt; i++) {
            char c = typed[i];
            len = (int)strlen(buf);
            if (c == '\n') { sfx(SFX_SELECT); plat_text_input(0); return 1; }
            if (c == '\b') { if (len) buf[len - 1] = 0; sfx(SFX_KEY); continue; }
            if (mode == KB_PIN ? !isdigit((unsigned char)c) : !(isalnum((unsigned char)c) || c == ' ')) continue;
            if (c == ' ' && (!len || buf[len - 1] == ' ')) continue;
            if (len < maxlen) { buf[len] = c; buf[len + 1] = 0; sfx(SFX_KEY); }
            else sfx(SFX_ERROR);
        }
        len = (int)strlen(buf);
        dx = ui_nav_x();
        dy = ui_nav_y();
        if (dx) {
            int n = sel + dx;
            if (n >= 0 && n < nkeys && keys[n].row == k->row) { sel = n; sfx(SFX_MOVE); }
        }
        if (dy) {
            int r = k->row + dy;
            if (r >= 0 && r < nrows) {
                int n = find_near(r, k->x + k->w / 2);
                if (n >= 0) { sel = n; sfx(SFX_MOVE); }
            }
        }
        k = &keys[sel];

        if (PRESSED(a)) {
            if (k->code == K_DONE) { sfx(SFX_SELECT); plat_text_input(0); return 1; }
            if (k->code == K_SHIFT) { shift = !shift; sfx(SFX_KEY); }
            else if (k->code == K_DEL) { if (len) buf[len - 1] = 0; sfx(SFX_KEY); }
            else if (k->code == K_SPACE) {
                if (len && len < maxlen && buf[len - 1] != ' ') { buf[len] = ' '; buf[len + 1] = 0; }
                sfx(SFX_KEY);
            } else if (len < maxlen) {
                char c = (char)k->code;
                buf[len] = shift ? (char)toupper((unsigned char)c) : c;
                buf[len + 1] = 0;
                if (shift && mode == KB_TEXT) shift = 0;
                sfx(SFX_KEY);
            } else {
                sfx(SFX_ERROR);
            }
        } else if (PRESSED(b) || PRESSED(x)) {
            if (!len && PRESSED(b)) { sfx(SFX_BACK); plat_text_input(0); return 0; }
            if (len) buf[len - 1] = 0;
            sfx(SFX_KEY);
        } else if (PRESSED(y) && mode == KB_TEXT) {
            if (len && len < maxlen && buf[len - 1] != ' ') { buf[len] = ' '; buf[len + 1] = 0; }
            sfx(SFX_KEY);
        } else if (PRESSED(start)) {
            sfx(SFX_SELECT);
            plat_text_input(0);
            return 1;
        } else if (PRESSED(lb) && mode == KB_TEXT) {
            shift = !shift;
            sfx(SFX_KEY);
        }

        /* Draw */
        ui_bg();
        ui_header(title, hint);
        {
            int fw = mode == KB_PIN ? 360 : 640, fx = (SCR_W - fw) / 2, tx = fx + 22;
            char shown[64];
            gfx_fill_rrect((float)fx, (float)fy, (float)fw, 68, 14, C_SURFACE, 255);
            gfx_stroke_rrect((float)fx, (float)fy, (float)fw, 68, 14, 2, C_ACCENT, 255);
            if (mode == KB_PIN) {
                int n = (int)strlen(buf);
                for (i = 0; i < n; i++) gfx_circle(tx + 14.0f + i * 34, fy + 34.0f, 8, C_TEXT, 255);
                tx += n * 34;
            } else {
                snprintf(shown, sizeof shown, "%s", buf);
                tx += gfx_str_vc(FONT_LABEL, tx, fy + 34, C_TEXT, shown);
                if (!buf[0]) gfx_str_vc(FONT_LABEL, fx + 22, fy + 34, C_DIM, "Type here");
            }
            if ((ui_ms() / 530) % 2 == 0) gfx_rect(tx + 3, fy + 18, 2, 32, C_ACCENT_HI);
            {
                char count[16];
                snprintf(count, sizeof count, "%d/%d", (int)strlen(buf), maxlen);
                gfx_str_r(FONT_SMALL, fx + fw - 16, fy + 72, C_DIM, count);
            }
        }
        hx = ui_ease(hx, keys[sel].x, 0.45f);
        hy = ui_ease(hy, (float)keys[sel].row, 0.45f);
        gfx_fill_rrect(ox + hx * unit, ky + hy * (kh + gap), keys[sel].w * unit - gap, kh, 8, C_TEXT, 255);
        for (i = 0; i < nkeys; i++) {
            struct key *q = &keys[i];
            float x = ox + q->x * unit, y = ky + q->row * (kh + gap), w = q->w * unit - gap;
            char label[8];
            int is_sel = i == sel;
            if (!is_sel) {
                gfx_fill_rrect(x, y, w, kh, 8, C_SURFACE, 255);
                gfx_stroke_rrect(x, y, w, kh, 8, 1, C_LINE, 255);
            }
            snprintf(label, sizeof label, "%s", q->label);
            if (q->code >= 'a' && q->code <= 'z' && shift) label[0] = (char)toupper((unsigned char)label[0]);
            if (q->code == K_SHIFT && shift) gfx_fill_rrect(x + 10, y + kh - 10, w - 20, 3, 2, C_ACCENT, 255);
            gfx_str_c(q->code < 32 ? FONT_BODY_SB : FONT_LABEL, (int)(x + w / 2),
                      (int)(y + kh / 2) + gfx_cap_h(q->code < 32 ? FONT_BODY_SB : FONT_LABEL) / 2
                          - font_table[q->code < 32 ? FONT_BODY_SB : FONT_LABEL].ascent,
                      is_sel ? C_INK : C_TEXT, label);
        }
        ui_hints(mode == KB_PIN ? "A=Press;B=Delete;START=Done" : "A=Type;B=Delete;Y=Space;LB=Shift;START=Done");
        ui_end_frame();
    }
}
