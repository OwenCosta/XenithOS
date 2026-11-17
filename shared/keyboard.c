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
        static const char *const text_rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
        /* KB_LINE: the symbols Minecraft's names, commands and chat use most. */
        static const char *const line_rows[] = {"1234567890", "qwertyuiop", "asdfghjkl_", "zxcvbnm-.,", ":@~!?'#=[]"};
        const char *const *rows = mode == KB_LINE ? line_rows : text_rows;
        int r, i, nr = mode == KB_LINE ? 5 : 4;
        for (r = 0; r < nr; r++) {
            int n = (int)strlen(rows[r]);
            float off = (10 - n) / 2.0f;
            for (i = 0; i < n; i++) {
                char l[2] = {rows[r][i], 0};
                add(r, off + i, 1, rows[r][i], l);
            }
        }
        add(nr, 0, 2, K_SHIFT, "Shift");
        add(nr, 2, 4, K_SPACE, "Space");
        add(nr, 6, 2, K_DEL, "Delete");
        add(nr, 8, 2, K_DONE, "Done");
        nrows = nr + 1;
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

/* ------------------------------------------------------------ desktop apps */

/* What a field takes: the same rules as the on-screen keyboard's. */
static int allowed(const struct kb_field *f, char c)
{
    if (f->mode == KB_PIN) return isdigit((unsigned char)c);
    if (f->mode == KB_LINE) return c >= ' ' && c <= '~';
    return isalnum((unsigned char)c) || c == ' ';
}

/* c at the caret: no space first, or next to another. */
static int field_insert(struct kb_field *f, char c)
{
    int len = (int)strlen(f->buf);
    if (!allowed(f, c)) return 0;
    if (c == ' ' && (f->caret == 0 || f->buf[f->caret - 1] == ' ' || f->buf[f->caret] == ' ')) return 0;
    if (len >= f->max) return -1;
    memmove(f->buf + f->caret + 1, f->buf + f->caret, (size_t)(len - f->caret + 1));
    f->buf[f->caret++] = c;
    return 1;
}

/* How wide n characters of s are. */
static int sub_w(int font, const char *s, int n)
{
    char tmp[300];
    if (n <= 0) return 0;
    if (n > (int)sizeof tmp - 1) n = (int)sizeof tmp - 1;
    memcpy(tmp, s, (size_t)n);
    tmp[n] = 0;
    return gfx_str_w(font, tmp);
}

void kb_field_init(struct kb_field *f, char *buf, int max, int mode)
{
    f->buf = buf;
    f->max = max;
    f->mode = mode;
    f->caret = (int)strlen(buf);
    f->first = 0;
    f->all = 0;
}

/* With all of it selected, what's typed, deleted or pasted replaces it. */
static void replace_all(struct kb_field *f)
{
    if (!f->all) return;
    f->buf[0] = 0;
    f->caret = f->first = 0;
    f->all = 0;
}

int kb_field_input(struct kb_field *f)
{
    char typed[64];
    int n, i, len = (int)strlen(f->buf), got = 0, full = 0, dx;
    if (f->caret > len) f->caret = len;
    if (f->caret < 0) f->caret = 0;
    n = plat_text_take(typed, sizeof typed);
    for (i = 0; i < n; i++) {
        if (typed[i] == '\n') { sfx(SFX_SELECT); f->all = 0; return 1; }
        if (typed[i] == '\b') {
            if (f->all) { replace_all(f); got = 1; }
            else if (f->caret > 0) {
                memmove(f->buf + f->caret - 1, f->buf + f->caret, strlen(f->buf + f->caret) + 1);
                f->caret--;
                got = 1;
            }
            continue;
        }
        if (allowed(f, typed[i])) replace_all(f);
        switch (field_insert(f, typed[i])) {
        case 1: got = 1; break;
        case -1: full = 1; break;
        }
    }
    len = (int)strlen(f->buf);
    if (ui_key(PLAT_KEY_DELETE)) {
        if (f->all) { replace_all(f); got = 1; }
        else if (f->caret < len) {
            memmove(f->buf + f->caret, f->buf + f->caret + 1, (size_t)(len - f->caret));
            got = 1;
        }
    }
    if (ui_key('a') == 2 && f->buf[0]) f->all = 1;
    if (ui_key('v') == 2) {                          /* paste: the first line of what was copied */
        char clip[256];
        if (plat_clipboard_get(clip, sizeof clip)) {
            replace_all(f);
            for (i = 0; clip[i] && clip[i] != '\n' && clip[i] != '\r'; i++) {
                int r = field_insert(f, clip[i] == '\t' ? ' ' : clip[i]);
                if (r < 0) { full = 1; break; }
                if (r > 0) got = 1;
            }
        }
    }
    len = (int)strlen(f->buf);
    dx = ui_nav_x();
    if (f->all && (dx || ui_key(PLAT_KEY_HOME) || ui_key(PLAT_KEY_END))) {
        f->caret = dx < 0 || ui_key(PLAT_KEY_HOME) ? 0 : len;    /* to that end of what was selected */
        f->all = 0;
    } else {
        if (ui_key(PLAT_KEY_HOME)) f->caret = 0;
        if (ui_key(PLAT_KEY_END)) f->caret = len;
        if (dx && f->caret + dx >= 0 && f->caret + dx <= len) f->caret += dx;
    }
    if (full) sfx(SFX_ERROR);
    else if (got) sfx(SFX_KEY);
    return 0;
}

int kb_field_click(struct kb_field *f, int x, int y, int w, int h, int font)
{
    int i, len = (int)strlen(f->buf), best = len, best_d = 1 << 30;
    if (!ui_clicked(x, y, w, h)) return 0;
    f->all = 0;
    if (f->mode != KB_PIN) {
        for (i = f->first; i <= len; i++) {
            int px = x + 16 + sub_w(font, f->buf + f->first, i - f->first), d = abs(px - mouse.x);
            if (d < best_d) { best_d = d; best = i; }
            if (px > x + w) break;
        }
    }
    f->caret = best;
    return 1;
}

void kb_field_draw(struct kb_field *f, int x, int y, int w, int h, int font, const char *placeholder, int focused)
{
    int pad = 16, inner = w - 2 * pad - 4, len = (int)strlen(f->buf), cy = y + h / 2, cx, ch = gfx_cap_h(font) + 12, clip[4], i;
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 10, C_SURFACE2, 255);
    if (focused) gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 10, 2, C_ACCENT, 255);
    else gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 10, 1, C_LINE, 255);
    gfx_get_clip(clip);
    gfx_clip_intersect(x + 4, y + 2, w - 8, h - 4);
    if (f->mode == KB_PIN) {
        for (i = 0; i < len; i++) gfx_circle(x + pad + 8.0f + i * 28, (float)cy, 7, C_TEXT, 255);
        cx = x + pad + len * 28;
    } else {
        /* The part with the caret in it: as much as fits, ending where the text ends if it can. */
        if (f->caret > len) f->caret = len;
        if (f->first > f->caret) f->first = f->caret;
        while (f->first < f->caret && sub_w(font, f->buf + f->first, f->caret - f->first) > inner) f->first++;
        while (f->first > 0 && sub_w(font, f->buf + f->first - 1, len - f->first + 1) <= inner) f->first--;
        if (f->all && len) {                         /* all of it selected */
            int sw = sub_w(font, f->buf + f->first, len - f->first);
            gfx_fill_rrect(x + pad - 4.0f, (float)(cy - ch / 2), sw + 8.0f, (float)ch, 4, RGB(52, 84, 150), 255);
        }
        if (!len && placeholder) gfx_str_vc(font, x + pad, cy, C_DIM, placeholder);
        else gfx_str_vc(font, x + pad, cy, C_TEXT, f->buf + f->first);
        cx = x + pad + sub_w(font, f->buf + f->first, f->caret - f->first);
    }
    if (focused && !f->all && (ui_ms() / 530) % 2 == 0) gfx_rect(cx + 1, cy - ch / 2, 2, ch, C_ACCENT_HI);
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
}

/* keyboard() in a desktop app: a box over the screen with the field, OK and Cancel. */
static int desktop_keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode)
{
    size_t bytes = (size_t)SCR_W * SCR_H * 4;
    uint32_t *under = malloc(bytes), *dimmed = NULL;
    char before[512];
    struct kb_field f;
    int w = SCR_W * 46 / 100, x, y, h, title_lines = 1, hint_lines = 0, result = -1, keys_were = ui_keys_on, i;
    int fh = 60, bw = 150, bh = 46;
    static const char *const labels[2] = {"OK", "Cancel"};

    snprintf(before, sizeof before, "%s", buf);
    if (under) {
        memcpy(under, gfx_bb, bytes);
        dimmed = malloc(bytes);                /* the screen behind, darkened once */
        if (dimmed) {
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
            memcpy(dimmed, gfx_bb, bytes);
        }
    }
    if (w < 600) w = 600;
    {
        int clip[4];
        gfx_get_clip(clip);
        gfx_set_clip(0, 0, 0, 0);              /* measure only */
        title_lines = gfx_wrap(FONT_TITLE, 0, 0, w - 64, 2, 0, title, 2);
        if (hint && *hint) hint_lines = gfx_wrap(FONT_BODY, 0, 0, w - 64, 4, 0, hint, 3);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    }
    h = 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8 + hint_lines * (gfx_line_h(FONT_BODY) + 4) + 18 + fh + 12 + 26 + 18 + bh + 28;
    x = (SCR_W - w) / 2;
    y = (SCR_H - h) / 2;
    kb_field_init(&f, buf, maxlen, mode);
    f.all = buf[0] != 0;                       /* what's there is selected: typing replaces it */
    plat_text_input(1);
    if (!keys_were) {                          /* keys from before the box opened aren't for it */
        int k, d;
        while (plat_key_event(&k, &d)) { }
        ui_keys_on = 1;
        ui_ctrl = 0;
    }

    while (result < 0) {
        int fy = y + 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8 + hint_lines * (gfx_line_h(FONT_BODY) + 4) + 18;
        int by = y + h - 28 - bh, hx = x + 32, cyh = fy + fh + 12 + 13;
        ui_poll();
        if (kb_field_input(&f)) result = 1;
        else if (PRESSED(b)) { sfx(SFX_BACK); result = 0; }
        for (i = 0; i < 2 && result < 0; i++)
            if (ui_clicked(x + w - 32 - (2 - i) * bw - (1 - i) * 12, by, bw, bh)) {
                sfx(i == 0 ? SFX_SELECT : SFX_BACK);
                result = i == 0;
            }
        kb_field_click(&f, x + 32, fy, w - 64, fh, FONT_LABEL);

        if (dimmed) memcpy(gfx_bb, dimmed, bytes);
        else {
            if (under) memcpy(gfx_bb, under, bytes);
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
        }
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, C_SURFACE, 255);
        gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 14, 1, C_LINE, 255);
        gfx_wrap(FONT_TITLE, x + 32, y + 26, w - 64, 2, C_TEXT, title, 2);
        if (hint_lines) gfx_wrap(FONT_BODY, x + 32, y + 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8, w - 64, 4, C_SUB, hint, 3);
        kb_field_draw(&f, x + 32, fy, w - 64, fh, FONT_LABEL, mode == KB_PIN ? NULL : "Type here", 1);
        {
            char count[16];
            snprintf(count, sizeof count, "%d/%d", (int)strlen(buf), maxlen);
            gfx_str_r(FONT_SMALL, x + w - 34, cyh - gfx_line_h(FONT_SMALL) / 2, C_DIM, count);
        }
        hx += ui_button_glyph(hx, cyh, "Enter") + 8;
        hx += gfx_str(FONT_SMALL, hx, cyh - gfx_line_h(FONT_SMALL) / 2, C_SUB, "OK") + 22;
        hx += ui_button_glyph(hx, cyh, "Esc") + 8;
        hx += gfx_str(FONT_SMALL, hx, cyh - gfx_line_h(FONT_SMALL) / 2, C_SUB, "Cancel") + 22;
        if (mode != KB_PIN) {
            hx += ui_button_glyph(hx, cyh, "Ctrl+V") + 8;
            gfx_str(FONT_SMALL, hx, cyh - gfx_line_h(FONT_SMALL) / 2, C_SUB, "Paste");
        }
        for (i = 0; i < 2; i++) {
            int bx = x + w - 32 - (2 - i) * bw - (1 - i) * 12, over = ui_mouse_in(bx, by, bw, bh);
            uint32_t fill = i == 0 ? (over ? 0xFFFFFF : C_TEXT) : (over ? C_SEL : C_SURFACE2);
            gfx_fill_rrect((float)bx, (float)by, (float)bw, (float)bh, 10, fill, 255);
            if (i == 1) gfx_stroke_rrect((float)bx, (float)by, (float)bw, (float)bh, 10, 1, C_LINE, 255);
            gfx_str_c(FONT_BODY_SB, bx + bw / 2, by + (bh - gfx_line_h(FONT_BODY_SB)) / 2, i == 0 ? C_INK : C_TEXT, labels[i]);
        }
        ui_end_frame();
    }
    plat_text_input(0);
    ui_keys_on = keys_were;
    if (!result) snprintf(buf, (size_t)maxlen + 1, "%s", before);
    if (under) {
        memcpy(gfx_bb, under, bytes);
        free(under);
    }
    free(dimmed);
    do { ui_poll(); } while (cur.a || cur.b);
    return result;
}

int keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode)
{
    int sel = 0, shift = 1, i;
    float unit = mode == KB_PIN ? 104.0f : 70.0f, kh = mode == KB_PIN ? 72.0f : mode == KB_LINE ? 50.0f : 58.0f, gap = 10;
    float hx = 0, hy = 0;

    if (ui_desktop) return desktop_keyboard(title, hint, buf, maxlen, mode);
    layout(mode);
    if ((mode == KB_TEXT && buf[0]) || mode == KB_LINE) shift = 0;
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
            if (mode == KB_PIN ? !isdigit((unsigned char)c)
                : mode == KB_LINE ? (c < ' ' || c > '~') : !(isalnum((unsigned char)c) || c == ' ')) continue;
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
                if (shift && mode != KB_PIN) shift = 0;
                sfx(SFX_KEY);
            } else {
                sfx(SFX_ERROR);
            }
        } else if (PRESSED(b) || PRESSED(x)) {
            if (!len && PRESSED(b)) { sfx(SFX_BACK); plat_text_input(0); return 0; }
            if (len) buf[len - 1] = 0;
            sfx(SFX_KEY);
        } else if (PRESSED(y) && mode != KB_PIN) {
            if (len && len < maxlen && buf[len - 1] != ' ') { buf[len] = ' '; buf[len + 1] = 0; }
            sfx(SFX_KEY);
        } else if (PRESSED(start)) {
            sfx(SFX_SELECT);
            plat_text_input(0);
            return 1;
        } else if (PRESSED(lb) && mode != KB_PIN) {
            shift = !shift;
            sfx(SFX_KEY);
        }

        /* Draw */
        ui_bg();
        ui_header(title, hint);
        {
            int fw = mode == KB_PIN ? 360 : 640, fx = (SCR_W - fw) / 2, tx = fx + 22;
            char shown[256];
            gfx_fill_rrect((float)fx, (float)fy, (float)fw, 68, 14, C_SURFACE, 255);
            gfx_stroke_rrect((float)fx, (float)fy, (float)fw, 68, 14, 2, C_ACCENT, 255);
            if (mode == KB_PIN) {
                int n = (int)strlen(buf);
                for (i = 0; i < n; i++) gfx_circle(tx + 14.0f + i * 34, fy + 34.0f, 8, C_TEXT, 255);
                tx += n * 34;
            } else {
                /* Longer than the field (a command): its end, where the typing is. */
                const char *from = buf;
                while (*from && gfx_str_w(FONT_LABEL, from) > fw - 60) from++;
                snprintf(shown, sizeof shown, "%s", from);
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
