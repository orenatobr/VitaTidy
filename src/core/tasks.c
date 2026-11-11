#include "tasks.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "badge.h"
#include "iso9660.h"
#include "order.h"
#include "platform.h"
#include "pngio.h"
#include "vt.h"

#define ICON_MAX_BYTES (2u * 1024 * 1024)

void paths_vita(Paths *p)
{
    memset(p, 0, sizeof *p);
    snprintf(p->db, sizeof p->db, "ur0:shell/db/app.db");
    snprintf(p->appmeta, sizeof p->appmeta, "ur0:appmeta");
    snprintf(p->app, sizeof p->app, "ux0:app");
    snprintf(p->data, sizeof p->data, "ux0:data/VitaTidy");
    snprintf(p->defaults, sizeof p->defaults, "app0:data");
    static const char *const roots[] = {"ux0:pspemu/ISO", "uma0:pspemu/ISO", "xmc0:pspemu/ISO", "imc0:pspemu/ISO"};
    snprintf(p->psp_iso, sizeof p->psp_iso, "%s", roots[0]);
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        DIR *d = opendir(roots[i]);
        if (d) { closedir(d); snprintf(p->psp_iso, sizeof p->psp_iso, "%s", roots[i]); break; }
    }
}

void gamelist_free(GameList *l)
{
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

/* Builds "<a>/<b>" style paths; false when the result would be truncated. */
static int path_join(char *dst, size_t n, const char *a, const char *b)
{
    return snprintf(dst, n, "%s/%s", a, b) < (int)n;
}

/* Loads <data>/<name>, falling back to the bundled <defaults>/<name>; empty order if neither exists. */
static int load_order(const Paths *p, const char *name, int inline_comments, OrderFile *out)
{
    char path[400];
    uint8_t *text = NULL;
    size_t len = 0;
    if (!(path_join(path, sizeof path, p->data, name) && fs_read_all(path, &text, &len, 1u << 20) == 0) &&
        !(path_join(path, sizeof path, p->defaults, name) && fs_read_all(path, &text, &len, 1u << 20) == 0)) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    char *z = realloc(text, len + 1);
    if (!z) { free(text); return -1; }
    z[len] = '\0';
    int rc = orderfile_parse(z, inline_comments, out);
    free(z);
    return rc;
}

static int files_differ(const char *a, const char *b)
{
    uint8_t *x, *y;
    size_t nx, ny;
    if (fs_read_all(a, &x, &nx, ICON_MAX_BYTES) != 0) return 1;
    if (fs_read_all(b, &y, &ny, ICON_MAX_BYTES) != 0) { free(x); return 1; }
    int diff = nx != ny || memcmp(x, y, nx) != 0;
    free(x);
    free(y);
    return diff;
}

/* Writes data and reads it back to be sure the copy is intact. */
static int write_verified(const char *path, const void *data, size_t len)
{
    if (fs_mkdirs_for(path) != 0 || fs_write_all(path, data, len) != 0) return -1;
    uint8_t *back;
    size_t n;
    if (fs_read_all(path, &back, &n, len + 1) != 0) return -1;
    int ok = n == len && memcmp(back, data, len) == 0;
    free(back);
    return ok ? 0 : -1;
}

/* ---- LiveArea ---- */

int task_livearea_plan(const Paths *p, LaPlan *plan, char *err, size_t errlen)
{
    sqlite3 *db;
    OrderFile of;
    if (la_db_load(p->db, &db, err, errlen) != 0) return -1;
    if (load_order(p, "order.txt", 1, &of) != 0) { sqlite3_close(db); VT_ERR(err, errlen, "out of memory"); return -1; }
    int rc = la_plan(db, &of, plan, err, errlen);
    orderfile_free(&of);
    sqlite3_close(db);
    return rc;
}

int task_livearea_apply(const Paths *p, const LaPlan *plan, char *err, size_t errlen)
{
    char orig[400], last[400], work[400];
    if (!path_join(orig, sizeof orig, p->data, "backup/app.db.orig") ||
        !path_join(last, sizeof last, p->data, "backup/app.db.last") ||
        !path_join(work, sizeof work, p->data, "work/app.db.new")) {
        VT_ERR(err, errlen, "path too long");
        return -1;
    }
    if (fs_mkdirs_for(orig) != 0 || fs_mkdirs_for(work) != 0) { VT_ERR(err, errlen, "cannot create backup folder"); return -1; }
    /* The first backup is never overwritten; the second one always holds the previous state. */
    if (!fs_exists(orig) && fs_copy(p->db, orig) != 0) { VT_ERR(err, errlen, "cannot back up app.db"); return -1; }
    if (fs_copy(p->db, last) != 0) { VT_ERR(err, errlen, "cannot back up app.db"); return -1; }

    sqlite3 *db;
    if (la_db_load(p->db, &db, err, errlen) != 0) return -1;
    if (la_apply(db, plan, LA_ITEMS_PER_PAGE, err, errlen) != 0) { sqlite3_close(db); return -1; }
    int rc = la_db_save(db, work, err, errlen);
    sqlite3_close(db);
    if (rc != 0) return -1;

    /* Check the new file before it replaces the real one. */
    if (la_db_load(work, &db, err, errlen) != 0) return -1;
    sqlite3_stmt *st;
    int ok = sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW &&
             strcmp((const char *)sqlite3_column_text(st, 0), "ok") == 0;
    sqlite3_finalize(st);
    sqlite3_close(db);
    if (!ok) { VT_ERR(err, errlen, "new app.db failed the integrity check; nothing was changed"); return -1; }

    uint8_t *data;
    size_t len;
    if (fs_read_all(work, &data, &len, 64u * 1024 * 1024) != 0) { VT_ERR(err, errlen, "cannot read new app.db"); return -1; }
    if (write_verified(p->db, data, len) != 0) {
        free(data);
        fs_copy(last, p->db); /* put the previous file back */
        VT_ERR(err, errlen, "writing app.db failed; the previous one was restored");
        return -1;
    }
    free(data);
    return 0;
}

/* ---- PSP order ---- */

static int has_iso_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".iso") == 0 || strcasecmp(dot, ".cso") == 0);
}

static int is_iso(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".iso") == 0;
}

/* Collects file names in the ISO folder that pass the filter. */
static int list_isos(const Paths *p, int (*keep)(const char *), char ***names, size_t *n, char *err, size_t errlen)
{
    DIR *d = opendir(p->psp_iso);
    if (!d) { VT_ERR(err, errlen, "cannot open %s", p->psp_iso); return -1; }
    char **v = NULL;
    size_t count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !keep(e->d_name)) continue;
        char **g = realloc(v, (count + 1) * sizeof *v);
        if (!g) { closedir(d); for (size_t i = 0; i < count; i++) free(v[i]); free(v); return -1; }
        v = g;
        v[count] = strdup(e->d_name);
        if (!v[count]) { closedir(d); for (size_t i = 0; i < count; i++) free(v[i]); free(v); return -1; }
        count++;
    }
    closedir(d);
    *names = v;
    *n = count;
    return 0;
}

void psp_plan_free(PspPlan *plan)
{
    for (size_t i = 0; i < plan->n; i++) free(plan->names[i]);
    free(plan->names);
    plan->names = NULL;
    plan->n = 0;
}

static void strip_ext(const char *name, char *dst, size_t n)
{
    snprintf(dst, n, "%s", name);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
}

int task_psp_plan(const Paths *p, PspPlan *plan, char *err, size_t errlen)
{
    memset(plan, 0, sizeof *plan);
    char **names;
    size_t n;
    if (list_isos(p, has_iso_ext, &names, &n, err, errlen) != 0) return -1;
    if (n == 0) { free(names); VT_ERR(err, errlen, "no ISO files in %s", p->psp_iso); return -1; }

    OrderFile of;
    if (load_order(p, "psp-order.txt", 0, &of) != 0) { VT_ERR(err, errlen, "out of memory"); goto oom; }
    OrderItem *items = malloc(n * sizeof *items);
    char (*stems)[256] = malloc(n * sizeof *stems);
    size_t *idx = malloc(n * sizeof *idx);
    if (!items || !stems || !idx) { free(items); free(stems); free(idx); orderfile_free(&of); VT_ERR(err, errlen, "out of memory"); goto oom; }
    for (size_t i = 0; i < n; i++) {
        strip_ext(names[i], stems[i], sizeof stems[i]);
        items[i].id = names[i];
        items[i].name = stems[i];
    }
    order_build(items, n, (const char *const *)of.games, of.ngames, idx);
    plan->names = malloc(n * sizeof *plan->names);
    if (!plan->names) { free(items); free(stems); free(idx); orderfile_free(&of); VT_ERR(err, errlen, "out of memory"); goto oom; }
    for (size_t i = 0; i < n; i++) plan->names[i] = names[idx[i]];
    plan->n = n;
    free(items);
    free(stems);
    free(idx);
    free(names);
    orderfile_free(&of);
    return 0;
oom:
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    return -1;
}

int task_psp_apply(const Paths *p, const PspPlan *plan, char *err, size_t errlen)
{
    vt_ticks base = plat_now();
    size_t failed = 0;
    for (size_t i = 0; i < plan->n; i++) {
        char path[400];
        vt_ticks want = base - (vt_ticks)i * VT_MINUTE;
        if (!path_join(path, sizeof path, p->psp_iso, plan->names[i]) || plat_set_time(path, want) != 0) { failed++; continue; }
        /* Adrenaline may sort by either date, so both must have taken. */
        vt_ticks got[2] = {plat_get_mtime(path), plat_get_ctime(path)};
        int ok = 1;
        for (int k = 0; k < 2; k++) {
            vt_ticks diff = got[k] > want ? got[k] - want : want - got[k];
            if (got[k] < 0 || diff > 2 * 1000000) ok = 0;
        }
        if (!ok) failed++;
    }
    if (failed) {
        VT_ERR(err, errlen, "%zu of %zu dates were not accepted by the file system", failed, plan->n);
        return -1;
    }
    return 0;
}

static int title_cmp(const void *a, const void *b)
{
    return order_cmp(((const Game *)a)->title, ((const Game *)b)->title);
}

/* ---- badge on Vita games ---- */

int task_vita_games(const Paths *p, GameList *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    sqlite3 *db;
    if (la_db_load(p->db, &db, err, errlen) != 0) return -1;
    OrderFile of;
    if (load_order(p, "order.txt", 1, &of) != 0) { sqlite3_close(db); VT_ERR(err, errlen, "out of memory"); return -1; }
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT DISTINCT titleId, COALESCE(title, titleId) FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%'",
                           -1, &st, NULL) != SQLITE_OK) {
        orderfile_free(&of);
        sqlite3_close(db);
        VT_ERR(err, errlen, "unexpected app.db schema");
        return -1;
    }
    size_t cap = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(st, 0);
        if (!id || orderfile_has(of.cfw, of.ncfw, id)) continue; /* homebrew tools are not games */
        char icon[400], backup[400], sub[160];
        snprintf(sub, sizeof sub, "%s/sce_sys/icon0.png", id);
        if (!path_join(icon, sizeof icon, p->app, sub) || !fs_exists(icon)) continue; /* homebrew or no icon to badge */
        snprintf(sub, sizeof sub, "originals/vita/%s.png", id);
        if (!path_join(backup, sizeof backup, p->data, sub)) continue;
        if (out->n == cap) {
            cap = cap ? cap * 2 : 64;
            Game *g = realloc(out->v, cap * sizeof *g);
            if (!g) { sqlite3_finalize(st); sqlite3_close(db); orderfile_free(&of); gamelist_free(out); VT_ERR(err, errlen, "out of memory"); return -1; }
            out->v = g;
        }
        Game *g = &out->v[out->n++];
        memset(g, 0, sizeof *g);
        snprintf(g->id, sizeof g->id, "%s", id);
        snprintf(g->title, sizeof g->title, "%s", (const char *)sqlite3_column_text(st, 1));
        g->marked = fs_exists(backup) && files_differ(backup, icon);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    orderfile_free(&of);
    qsort(out->v, out->n, sizeof *out->v, title_cmp);
    return 0;
}

static int valid_id(const char *id)
{
    /* The id becomes part of a path: allow only safe characters. */
    if (!*id) return 0;
    for (const char *c = id; *c; c++)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '_' || *c == '-'))
            return 0;
    return 1;
}

int task_vita_toggle(const Paths *p, const char *title_id, int *now_marked, char *err, size_t errlen)
{
    char icon[400], meta[400], backup[400], sub[160];
    if (!valid_id(title_id)) { VT_ERR(err, errlen, "invalid title id"); return -1; }
    snprintf(sub, sizeof sub, "%s/sce_sys/icon0.png", title_id);
    int ok = path_join(icon, sizeof icon, p->app, sub);
    snprintf(sub, sizeof sub, "%s/icon0.png", title_id);
    ok = ok && path_join(meta, sizeof meta, p->appmeta, sub);
    snprintf(sub, sizeof sub, "originals/vita/%s.png", title_id);
    ok = ok && path_join(backup, sizeof backup, p->data, sub);
    if (!ok) { VT_ERR(err, errlen, "path too long"); return -1; }

    uint8_t *cur;
    size_t cur_len;
    if (fs_read_all(icon, &cur, &cur_len, ICON_MAX_BYTES) != 0) { VT_ERR(err, errlen, "cannot read the game icon"); return -1; }

    int rc = -1;
    uint8_t *orig = NULL, *png = NULL;
    size_t orig_len = 0, png_len = 0;
    Image img = {0};

    if (fs_exists(backup) && files_differ(backup, icon)) {
        /* Marked: put the original icon back. */
        if (fs_read_all(backup, &orig, &orig_len, ICON_MAX_BYTES) != 0 || fs_write_all(icon, orig, orig_len) != 0) {
            VT_ERR(err, errlen, "cannot restore the original icon");
            goto out;
        }
        if (fs_exists(meta)) fs_write_all(meta, orig, orig_len); /* the firmware cache is only touched if it exists */
        fs_remove(backup);
        *now_marked = 0;
        rc = 0;
        goto out;
    }

    if (img_decode(cur, cur_len, &img) != 0) { VT_ERR(err, errlen, "icon is not a readable PNG"); goto out; }
    Badge b = badge_for(img.w, img.h);
    badge_draw(&img, &b);
    png = png_encode_truecolor(&img, &png_len);
    if (!png) { VT_ERR(err, errlen, "cannot encode the badged icon"); goto out; }
    if (!fs_exists(backup) && write_verified(backup, cur, cur_len) != 0) { VT_ERR(err, errlen, "cannot save the original icon"); goto out; }
    if (fs_write_all(icon, png, png_len) != 0) {
        fs_write_all(icon, cur, cur_len); /* do not leave a half-written icon behind */
        VT_ERR(err, errlen, "cannot write the badged icon");
        goto out;
    }
    /* The firmware cache is best effort and never created here: it is rebuilt from sce_sys on reboot anyway. */
    if (fs_exists(meta)) fs_write_all(meta, png, png_len);
    *now_marked = 1;
    rc = 0;
out:
    img_free(&img);
    free(png);
    free(orig);
    free(cur);
    return rc;
}

/* ---- badge on PSP games ---- */

/* An ISO is marked when a saved original exists and the icon inside it differs from that original. */
static int psp_is_marked(const char *iso, const char *backup)
{
    uint8_t *saved;
    size_t saved_len;
    if (fs_read_all(backup, &saved, &saved_len, ICON_MAX_BYTES) != 0) return 0;
    int marked = 0;
    FILE *f = fopen(iso, "rb");
    uint64_t off;
    uint32_t len;
    if (f && iso_find_icon0(f, &off, &len, NULL, 0) == 0 && len == saved_len) {
        uint8_t *cur = malloc(len);
        marked = cur && iso_read_at(f, off, cur, len) == 0 && memcmp(cur, saved, len) != 0;
        free(cur);
    }
    if (f) fclose(f);
    free(saved);
    return marked;
}

int task_psp_games(const Paths *p, GameList *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    char **names;
    size_t n;
    if (list_isos(p, is_iso, &names, &n, err, errlen) != 0) return -1;
    out->v = calloc(n ? n : 1, sizeof *out->v);
    if (!out->v) { for (size_t i = 0; i < n; i++) free(names[i]); free(names); VT_ERR(err, errlen, "out of memory"); return -1; }
    for (size_t i = 0; i < n; i++) {
        Game *g = &out->v[i];
        snprintf(g->id, sizeof g->id, "%s", names[i]);
        strip_ext(names[i], g->title, sizeof g->title);
        char backup[400], sub[400], iso[400];
        snprintf(sub, sizeof sub, "originals/psp/%s.png", names[i]);
        if (path_join(backup, sizeof backup, p->data, sub) && path_join(iso, sizeof iso, p->psp_iso, names[i]))
            g->marked = psp_is_marked(iso, backup);
        free(names[i]);
    }
    free(names);
    out->n = n;
    qsort(out->v, out->n, sizeof *out->v, title_cmp);
    return 0;
}

int task_psp_toggle(const Paths *p, const char *iso_name, int *now_marked, char *err, size_t errlen)
{
    char iso[400], backup[400], sub[400];
    if (strchr(iso_name, '/') || strchr(iso_name, ':') || strstr(iso_name, "..") || !is_iso(iso_name)) {
        VT_ERR(err, errlen, "invalid ISO name");
        return -1;
    }
    snprintf(sub, sizeof sub, "originals/psp/%s.png", iso_name);
    if (!path_join(iso, sizeof iso, p->psp_iso, iso_name) || !path_join(backup, sizeof backup, p->data, sub)) {
        VT_ERR(err, errlen, "path too long");
        return -1;
    }

    FILE *f = fopen(iso, "r+b");
    if (!f) { VT_ERR(err, errlen, "cannot open the ISO"); return -1; }
    int rc = -1;
    uint8_t *cur = NULL, *orig = NULL, *png = NULL, *padded = NULL;
    size_t orig_len = 0, png_len = 0;
    Image img = {0};
    uint64_t off;
    uint32_t len;

    if (iso_find_icon0(f, &off, &len, err, errlen) != 0) goto out;
    cur = malloc(len);
    if (!cur || iso_read_at(f, off, cur, len) != 0) { VT_ERR(err, errlen, "cannot read the ISO icon"); goto out; }

    int has_backup = fs_exists(backup);
    uint8_t *bk = NULL;
    size_t bk_len = 0;
    if (has_backup && fs_read_all(backup, &bk, &bk_len, ICON_MAX_BYTES) != 0) { VT_ERR(err, errlen, "cannot read the saved icon"); goto out; }
    orig = bk;
    orig_len = bk_len;

    if (has_backup && (bk_len != len || memcmp(bk, cur, len) != 0)) {
        /* Marked: write the original icon bytes back. */
        if (bk_len != len) { VT_ERR(err, errlen, "saved icon does not match this ISO"); goto out; }
        if (iso_write_at(f, off, bk, len) != 0) { VT_ERR(err, errlen, "cannot write the ISO"); goto out; }
        fs_remove(backup);
        *now_marked = 0;
        rc = 0;
        goto out;
    }

    if (img_decode(cur, len, &img) != 0) { VT_ERR(err, errlen, "ISO icon is not a readable PNG"); goto out; }
    Badge b = badge_for(img.w, img.h);
    badge_draw(&img, &b);
    /* The icon must stay inside its original byte range so the disc layout is untouched. */
    png = png_encode_fit(&img, len, &png_len);
    if (!png) { VT_ERR(err, errlen, "badged icon does not fit in the ISO (%u bytes)", (unsigned)len); goto out; }
    padded = calloc(len, 1);
    if (!padded) { VT_ERR(err, errlen, "out of memory"); goto out; }
    memcpy(padded, png, png_len);
    if (!has_backup && write_verified(backup, cur, len) != 0) { VT_ERR(err, errlen, "cannot save the original icon"); goto out; }
    if (iso_write_at(f, off, padded, len) != 0) { VT_ERR(err, errlen, "cannot write the ISO"); goto out; }
    *now_marked = 1;
    rc = 0;
out:
    (void)orig_len;
    img_free(&img);
    free(padded);
    free(png);
    free(orig);
    free(cur);
    fclose(f);
    return rc;
}
