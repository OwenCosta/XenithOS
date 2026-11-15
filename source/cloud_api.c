/* Talking to XenithOS Cloud. Replies are plain text: key=value lines, or rows split by '|'. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "ui.h"

void kv_get(const char *body, const char *key, char *out, int size)
{
    int klen = (int)strlen(key);
    const char *p = body;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1, *e = strchr(v, '\n');
            int n = e ? (int)(e - v) : (int)strlen(v);
            if (n >= size) n = size - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (; *line && n < max; line++)
        if (*line == '|') { *line = 0; f[n++] = line + 1; }
    return n;
}

const char *cloud_error_text(struct http_resp *r, int status)
{
    static char msg[120];
    if (status < 0) return net_error(status);
    if (r && r->body) {
        kv_get(r->body, "error", msg, sizeof msg);
        if (msg[0]) return msg;
    }
    snprintf(msg, sizeof msg, "XenithOS Cloud answered with error %d.", status);
    return msg;
}

int cloud_ready(const char *title)
{
    (void)title;
    if (!cfg.server[0]) {
        sfx(SFX_ERROR);
        ui_message("XenithOS Cloud isn't set up", "This copy of XenithOS doesn't have the XenithOS Cloud address. Download XenithOS again from its website, which includes it.");
        return 0;
    }
    if (net_is_up()) return 1;
    ui_loading_frame("Connecting", "Getting an address from your router");
    audio_prepare_stall();
    if (!net_up()) {
        sfx(SFX_ERROR);
        if (plat_kind() == PLAT_XBOX)
            ui_message("No network", "Plug an Ethernet cable into the back of the Xbox, then try again. XenithOS can't use Wi-Fi on the Xbox.");
        else
            ui_message("No network", "This machine isn't connected to the internet. Connect it, then try again.");
        return 0;
    }
    return 1;
}

int cloud_post(const char *path, const char *form, struct http_resp *r)
{
    return http_request(cfg.server, "POST", path, form, (int)strlen(form), 64 * 1024, NULL, NULL, r);
}

int cloud_get(const char *path, int max, struct http_resp *r)
{
    return http_request(cfg.server, "GET", path, NULL, 0, max, NULL, NULL, r);
}

/* Show newer XenithOS Cloud news as a toast. */
static void check_inbox(void)
{
    struct http_resp r;
    char *line, *f[3];
    int st_code;
    st_code = cloud_get("/v1/news", 16 * 1024, &r);
    if (st_code == 200 && r.body_len) {
        line = r.body;
        {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
        }
        if (split_fields(line, f, 3) == 3) {
            int id = atoi(f[0]);
            if (id > st.last_inbox) {
                st.last_inbox = id;
                settings_save();
                ui_toast_icon(ICON_MAIL);
                ui_toast("XenithOS news", f[2]);
                sfx(SFX_NOTIFY);
            }
        }
    }
    http_free(&r);
}

void cloud_sync(int quiet)
{
    struct http_resp r;
    char form[160], v[64];
    int code;
    (void)quiet;
    if (!cfg.server[0] || !net_is_up()) return;

    if (prof.signed_in) {
        snprintf(form, sizeof form, "token=%s", prof.token);
        code = cloud_post("/v1/accounts/me", form, &r);
        if (code == 200) {
            kv_get(r.body, "name", v, sizeof v);
            if (v[0]) snprintf(prof.name, sizeof prof.name, "%s", v);
            kv_get(r.body, "avatar", v, sizeof v);
            if (avatar_valid(v)) snprintf(prof.avatar, sizeof prof.avatar, "%s", v);
            kv_get(r.body, "joined", v, sizeof v);
            snprintf(prof.joined, sizeof prof.joined, "%s", v);
            profile_save();
        } else if (code == 401) {
            profile_clear();
            ui_toast_icon(ICON_SIGN_OUT);
            ui_toast("Signed out", "Your sign-in expired. Sign in again from Profile.");
        }
        http_free(&r);
    }
    check_inbox();
}

void cloud_poll_inbox(void)
{
    static uint64_t last;
    uint64_t now = ui_ms();
    if (!net_is_up() || !cfg.server[0]) return;
    if (!last) { last = now; return; }        /* go_online() already checked at boot */
    if (now - last < 60000) return;
    last = now;
    check_inbox();
}
