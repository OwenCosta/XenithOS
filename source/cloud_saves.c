/* Cloud saves: keeping this machine's copies of a profile's saves and settings (shared/cloudsave.h)
 * in step with the ones on XenithOS Cloud (/v1/saves). XenithOS does it when it comes online,
 * before an app with cloud saves starts and after it closes, and from Profile, Cloud saves.
 *
 * For each file the profile's folder has a record (cloud.txt) of the revision this machine last
 * had in step, and the file's SHA-256 then. Against that:
 *   changed only here                 it goes up
 *   changed only on XenithOS Cloud    it comes down
 *   changed in both, the same way     nothing to do
 *   changed in both, differently      the player chooses which to keep, and the other is
 *                                     replaced; "Decide later" leaves both as they are
 *   deleted from XenithOS Cloud       this machine keeps its copy, and sends it again only once
 *                                     it changes
 * The first time a profile's settings meet XenithOS Cloud's on a machine, XenithOS Cloud's are
 * taken without asking: settings follow the profile. Saves are always asked about. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cloudsave.h"
#include "config.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "sha256.h"
#include "ui.h"

#define FILE_MAX (512 * 1024)               /* what XenithOS Cloud takes for one file */
#define RECORDS 16

struct cloud_entry cloud_list[CLOUD_LIST_MAX];
int ncloud_list;
long cloud_used, cloud_limit;

static char problem[160];                   /* the last thing that went wrong, for a message */

int cloud_saves_list(const char *app)
{
    struct http_resp r;
    char form[160], *line;
    int code;
    snprintf(form, sizeof form, "token=%s%s%s", prof.token, app ? "&app=" : "", app ? app : "");
    code = cloud_post("/v1/saves", form, &r);
    if (code == 200) {
        ncloud_list = 0;
        cloud_used = cloud_limit = 0;
        for (line = r.body; line && *line; ) {
            char *next = strchr(line, '\n'), *f[8];
            if (next) *next++ = 0;
            if (!strncmp(line, "used=", 5)) cloud_used = atol(line + 5);
            else if (!strncmp(line, "limit=", 6)) cloud_limit = atol(line + 6);
            else if ((!strncmp(line, "file=", 5) || !strncmp(line, "gone=", 5)) && ncloud_list < CLOUD_LIST_MAX) {
                int gone = line[0] == 'g', n = split_fields(line + 5, f, 8);
                struct cloud_entry *e = &cloud_list[ncloud_list];
                if (n >= (gone ? 4 : 8)) {
                    memset(e, 0, sizeof *e);
                    snprintf(e->app, sizeof e->app, "%s", f[0]);
                    snprintf(e->name, sizeof e->name, "%s", f[1]);
                    e->rev = atoi(f[2]);
                    e->gone = gone;
                    if (gone) e->age = atoi(f[3]);
                    else {
                        e->size = atoi(f[3]);
                        snprintf(e->sha, sizeof e->sha, "%s", f[4]);
                        e->age = atoi(f[5]);
                        snprintf(e->from, sizeof e->from, "%s", f[6]);
                        snprintf(e->label, sizeof e->label, "%s", f[7]);
                    }
                    ncloud_list++;
                }
            }
            line = next;
        }
    } else {
        snprintf(problem, sizeof problem, "%s", cloud_error_text(&r, code));
    }
    http_free(&r);
    return code;
}

static struct cloud_entry *listed(const char *app, const char *name)
{
    int i;
    for (i = 0; i < ncloud_list; i++)
        if (!strcmp(cloud_list[i].app, app) && !strcmp(cloud_list[i].name, name)) return &cloud_list[i];
    return NULL;
}

/* ------------------------------------------------------------- records */

struct record { char name[52], sha[65]; int rev; };

static int read_records(const char *path, struct record *rec)
{
    char line[200], *f[3];
    int n = 0;
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    while (fgets(line, sizeof line, fp) && n < RECORDS) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || split_fields(line, f, 3) != 3) continue;
        snprintf(rec[n].name, sizeof rec[n].name, "%s", f[0]);
        rec[n].rev = atoi(f[1]);
        snprintf(rec[n].sha, sizeof rec[n].sha, "%s", f[2]);
        n++;
    }
    fclose(fp);
    return n;
}

static void write_records(const char *path, const struct record *rec, int n)
{
    int i;
    FILE *fp;
    make_folders(path);
    fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "# Written by XenithOS: the cloud saves this folder was last in step with (name|revision|sha256)\n");
    for (i = 0; i < n; i++) fprintf(fp, "%s|%d|%s\n", rec[i].name, rec[i].rev, rec[i].sha);
    fclose(fp);
}

static struct record *find_record(struct record *rec, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(rec[i].name, name)) return &rec[i];
    return NULL;
}

static void set_record(struct record *rec, int *n, const char *name, int rev, const char *sha)
{
    struct record *r = find_record(rec, *n, name);
    if (!r) {
        if (*n == RECORDS) return;
        r = &rec[(*n)++];
        snprintf(r->name, sizeof r->name, "%s", name);
    }
    r->rev = rev;
    snprintf(r->sha, sizeof r->sha, "%s", sha);
}

static void drop_record(struct record *rec, int *n, const char *name)
{
    struct record *r = find_record(rec, *n, name);
    if (r) *r = rec[--(*n)];
}

/* --------------------------------------------------------------- files */

/* The whole file, or NULL: *len is -1 when it's there but can't go to XenithOS Cloud (too big). */
static unsigned char *read_all(const char *path, int *len)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf;
    long n;
    *len = 0;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0 || n > FILE_MAX) { fclose(fp); *len = -1; return NULL; }
    if (!(buf = malloc(n ? n : 1))) { fclose(fp); *len = -1; return NULL; }
    if ((long)fread(buf, 1, n, fp) != n) { fclose(fp); free(buf); return NULL; }
    fclose(fp);
    *len = (int)n;
    return buf;
}

static int write_all(const char *path, const void *data, int len)
{
    char tmp[200];
    FILE *fp;
    make_folders(path);
    snprintf(tmp, sizeof tmp, "%s.part", path);
    fp = fopen(tmp, "wb");
    if (!fp) return 0;
    if ((int)fwrite(data, 1, len, fp) != len) { fclose(fp); remove(tmp); return 0; }
    fclose(fp);
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

/* A DOOM save's name: what the player typed into the slot, the first 24 bytes. */
static void save_label(const unsigned char *d, int len, char *out, int n)
{
    int i, o = 0;
    for (i = 0; i < len && i < 24 && d[i] && o < n - 1; i++)
        out[o++] = d[i] >= 32 && d[i] < 127 && d[i] != '|' ? (char)d[i] : ' ';
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* ------------------------------------------------------- up and down */

/* 200 with *rev set, or the status (409: it changed on XenithOS Cloud meanwhile). */
static int upload(const char *app, const struct cloud_file *f, const unsigned char *data, int len, int base, int force, int *rev)
{
    char head[400], en_from[64], en_label[96], label[28] = "", v[16];
    struct http_resp r;
    char *body;
    int hl, code;
    if (f->save) save_label(data, len, label, sizeof label);
    net_escape(plat_name(), en_from, sizeof en_from);
    net_escape(label, en_label, sizeof en_label);
    hl = snprintf(head, sizeof head, "token=%s&app=%s&name=%s&base=%d%s&from=%s&label=%s\n",
                  prof.token, app, f->name, base, force ? "&force=1" : "", en_from, en_label);
    body = malloc(hl + len);
    if (!body) return NE_MEMORY;
    memcpy(body, head, hl);
    memcpy(body + hl, data, len);
    code = http_request(cfg.server, "POST", "/v1/saves/put", body, hl + len, 8 * 1024, NULL, NULL, &r);
    free(body);
    if (code == 200) {
        kv_get(r.body, "rev", v, sizeof v);
        *rev = atoi(v);
    } else if (code != 409) {
        snprintf(problem, sizeof problem, "%s", cloud_error_text(&r, code));
    }
    http_free(&r);
    return code;
}

/* XenithOS Cloud's copy into path. 1 with *rev and sha (65 bytes) set. */
static int download(const char *app, const char *name, const char *path, int *rev, char *sha)
{
    char form[200], got[65];
    struct http_resp r;
    int code, ok = 0;
    snprintf(form, sizeof form, "token=%s&app=%s&name=%s", prof.token, app, name);
    code = http_request(cfg.server, "POST", "/v1/saves/get", form, (int)strlen(form), FILE_MAX + 4096, NULL, NULL, &r);
    if (code == 200) {
        char *nl = memchr(r.body, '\n', r.body_len < 300 ? r.body_len : 300);
        if (nl) {
            const unsigned char *data = (const unsigned char *)nl + 1;
            int len = r.body_len - (int)(nl + 1 - r.body);
            *nl = 0;
            /* the line is a form: rev=..&size=..&sha256=.. */
            {
                char *p, *save = NULL;
                *rev = 0;
                sha[0] = 0;
                for (p = strtok_r(r.body, "&", &save); p; p = strtok_r(NULL, "&", &save)) {
                    if (!strncmp(p, "rev=", 4)) *rev = atoi(p + 4);
                    else if (!strncmp(p, "sha256=", 7)) snprintf(sha, 65, "%s", p + 7);
                }
            }
            sha256_hex(data, (size_t)len, got);
            if (*rev > 0 && !strcmp(got, sha)) ok = write_all(path, data, len);
            if (!ok) snprintf(problem, sizeof problem, "A save from XenithOS Cloud didn't come through whole. Try again.");
        }
    } else {
        snprintf(problem, sizeof problem, "%s", cloud_error_text(&r, code));
    }
    http_free(&r);
    return ok;
}

/* ---------------------------------------------------- XenithOS's own */

/* XenithOS's settings that follow the profile: interface sounds and the start-up chime. Volume
 * and connecting at start-up depend on the machine, so they stay. */
static void personal_text(char *out, int n)
{
    snprintf(out, n, "ui_sounds=%d\nboot_sound=%d\n", st.ui_sounds, st.boot_sound);
}

static void personal_write(const char *dir)
{
    char path[160], want[96], *have;
    int len = 0;
    snprintf(path, sizeof path, "%spersonal.txt", dir);
    personal_text(want, sizeof want);
    have = (char *)read_all(path, &len);
    if (!have || len != (int)strlen(want) || memcmp(have, want, len)) write_all(path, want, (int)strlen(want));
    free(have);
}

static void personal_read(const char *dir)
{
    char path[160], *text, *line, *save = NULL;
    int len = 0;
    snprintf(path, sizeof path, "%spersonal.txt", dir);
    text = (char *)read_all(path, &len);
    if (!text) return;
    text = realloc(text, len + 1);
    text[len] = 0;
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "ui_sounds=", 10)) st.ui_sounds = atoi(line + 10) != 0;
        else if (!strncmp(line, "boot_sound=", 11)) st.boot_sound = atoi(line + 11) != 0;
    }
    free(text);
    audio_set_ui_sounds(st.ui_sounds);
    settings_save();
}

/* --------------------------------------------------------------- one app */

/* Which copy to keep, when a file changed here and on XenithOS Cloud. 0 here, 1 the Cloud's,
 * -1 decide later. */
static int ask_which(const struct cloud_app *a, const struct cloud_file *f, const unsigned char *data, int len,
                     const struct cloud_entry *e)
{
    char title[96], here[48], body[320], ago[40], label[28] = "", mine[48], theirs[48];
    const char *opts[3];
    const char *machine = plat_kind() == PLAT_XBOX ? "this Xbox" : "this PC";
    int c;
    snprintf(title, sizeof title, "%s %s changed in two places", a->title, f->title);
    if (f->save) save_label(data, len, label, sizeof label);
    cloud_ago(e->age, ago, sizeof ago);
    ago[0] = (char)(ago[0] >= 'A' && ago[0] <= 'Z' ? ago[0] - 'A' + 'a' : ago[0]);
    /* What each one is: a save's name, when there is one */
    if (label[0]) snprintf(mine, sizeof mine, "\"%s\"", label);
    else snprintf(mine, sizeof mine, "one copy");
    if (e->label[0]) snprintf(theirs, sizeof theirs, "\"%s\"", e->label);
    else snprintf(theirs, sizeof theirs, "another");
    snprintf(body, sizeof body, "%c%s has %s, and XenithOS Cloud has %s, from %s, %s. Keep one, and the other is "
             "replaced. Decide later leaves both as they are.", machine[0] - 'a' + 'A', machine + 1, mine, theirs,
             e->from[0] ? e->from : "another machine", ago);
    snprintf(here, sizeof here, "Keep %s's", machine);
    opts[0] = here;
    opts[1] = "Keep XenithOS Cloud's";
    opts[2] = "Decide later";
    sfx(SFX_NOTIFY);
    c = ui_dialog(title, body, opts, 3);
    return c == 0 ? 0 : c == 1 ? 1 : -1;
}

enum { KEEP_NOTHING = -2, KEEP_LATER = -1, KEEP_HERE = 0, KEEP_CLOUD = 1 };

/* What to do with one file, from this machine's copy (data, its SHA-256 lsha), its record r and
 * XenithOS Cloud's listing e. Records that only need bringing up to date are done here. */
static int decide(const struct cloud_app *a, const struct cloud_file *f, const unsigned char *data, int len,
                  const char *lsha, struct record *rec, int *nrec, const struct cloud_entry *e, int ask)
{
    struct record *r = find_record(rec, *nrec, f->name);
    int here_changed = data && (!r || strcmp(r->sha, lsha));
    int there_changed = e && (!r || e->rev != r->rev);
    if (!data && !e) { drop_record(rec, nrec, f->name); return KEEP_NOTHING; }
    if (e && e->gone) {                    /* deleted from XenithOS Cloud: this machine keeps its copy */
        if (!data) { drop_record(rec, nrec, f->name); return KEEP_NOTHING; }
        if (here_changed) return KEEP_HERE;        /* changed since: it goes up again */
        set_record(rec, nrec, f->name, e->rev, lsha);
        return KEEP_NOTHING;
    }
    if (!data) return KEEP_CLOUD;                  /* only on XenithOS Cloud */
    if (!e) return KEEP_HERE;                      /* only here */
    if (!here_changed && !there_changed) return KEEP_NOTHING;
    if (!there_changed) return KEEP_HERE;
    if (!here_changed) return KEEP_CLOUD;
    if (!strcmp(lsha, e->sha)) {                   /* both changed, to the same thing */
        set_record(rec, nrec, f->name, e->rev, lsha);
        return KEEP_NOTHING;
    }
    if (!r && !f->save) return KEEP_CLOUD;         /* settings follow the profile */
    if (!ask) return KEEP_LATER;
    return ask_which(a, f, data, len, e);
}

/* One app's files against the listing. Returns 0 if XenithOS Cloud couldn't be reached.
 * *changed counts files here that were replaced, *waiting clashes left for later. */
static int sync_app(const struct cloud_app *a, int ask, int *changed, int *waiting, int *full)
{
    char dir[96], recpath[128], path[200], sha[65], lsha[65];
    struct record rec[RECORDS];
    int nrec, i, rev, replaced = 0;

    profile_folder_start(a);
    profile_folder(a->dir, dir, sizeof dir);
    snprintf(recpath, sizeof recpath, "%scloud.txt", dir);
    nrec = read_records(recpath, rec);

    for (i = 0; i < a->nfiles; i++) {
        const struct cloud_file *f = &a->files[i];
        const struct cloud_entry *e = listed(a->id, f->name);
        unsigned char *data;
        int len, code, keep;

        snprintf(path, sizeof path, "%s%s", dir, f->path);
        data = read_all(path, &len);
        if (!data && len < 0) continue;            /* too big for XenithOS Cloud: left as it is */
        if (data) sha256_hex(data, (size_t)len, lsha);
        keep = decide(a, f, data, len, lsha, rec, &nrec, e, ask);

        if (keep == KEEP_HERE) {
            /* Based on the revision just listed: if it changes again meanwhile, XenithOS Cloud
             * refuses (409) and the next time round sees it. */
            code = upload(a->id, f, data, len, e ? e->rev : 0, 0, &rev);
            if (code == 200) set_record(rec, &nrec, f->name, rev, lsha);
            else if (code == 413) *full = 1;
            else if (code != 409) { free(data); write_records(recpath, rec, nrec); return 0; }
        } else if (keep == KEEP_CLOUD) {
            if (download(a->id, f->name, path, &rev, sha)) {
                set_record(rec, &nrec, f->name, rev, sha);
                replaced++;
            }
        } else if (keep == KEEP_LATER) {
            (*waiting)++;
        }
        free(data);
    }
    write_records(recpath, rec, nrec);
    if (replaced && !strcmp(a->id, "xenithos")) personal_read(dir);
    *changed += replaced;
    return 1;
}

int cloud_saves_on(void)
{
    return prof.signed_in && st.cloud_saves && cfg.server[0];
}

int cloud_saves_sync(const char *app_id, int ask)
{
    int i, ok = 1, changed = 0, waiting = 0, full = 0;
    char dir[96];
    if (app_id && !cloud_app_find(app_id)) return 1;
    if (!cloud_saves_on() || !net_is_up()) return 0;
    problem[0] = 0;
    if (!app_id || !strcmp(app_id, "xenithos")) {           /* XenithOS's own, as they are now */
        profile_folder(cloud_app_find("xenithos")->dir, dir, sizeof dir);
        profile_folder_start(cloud_app_find("xenithos"));
        personal_write(dir);
    }
    if (cloud_saves_list(app_id) != 200) ok = 0;            /* one listing for them all */
    for (i = 0; ok && i < ncloud_apps; i++)
        if (!app_id || !strcmp(app_id, cloud_apps[i].id))
            ok = sync_app(&cloud_apps[i], ask, &changed, &waiting, &full);
    if (!ok && ask) {
        sfx(SFX_ERROR);
        ui_message("Couldn't bring your saves up to date", problem[0] ? problem : "XenithOS Cloud didn't answer.");
    } else if (full) {
        ui_toast_icon(ICON_CLOUD);
        ui_toast("Your cloud saves are full", "Some didn't go up. Delete what you don't need in Profile, Cloud saves.");
    } else if (waiting && !ask) {
        ui_toast_icon(ICON_CLOUD);
        ui_toast("A save changed in two places", "You choose which to keep when you next open it, or in Profile, Cloud saves.");
    }
    return ok;
}

const char *cloud_saves_problem(void)
{
    return problem;
}
