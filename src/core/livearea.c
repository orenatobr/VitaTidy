#include "livearea.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "vt.h"

#define DB_MAX_BYTES (64u * 1024 * 1024)

/* The shell registers add_rm_list; triggers on tbl_livearea reference it, so a stub is needed. */
static void add_rm_list_stub(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    (void)argc;
    (void)argv;
    sqlite3_result_null(ctx);
}

int la_db_load(const char *path, sqlite3 **out, char *err, size_t errlen)
{
    uint8_t *file;
    size_t len;
    if (fs_read_all(path, &file, &len, DB_MAX_BYTES) != 0 || len == 0) {
        VT_ERR(err, errlen, "cannot read %s", path);
        return -1;
    }
    unsigned char *buf = sqlite3_malloc64(len);
    if (!buf) { free(file); VT_ERR(err, errlen, "out of memory"); return -1; }
    memcpy(buf, file, len);
    free(file);

    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        sqlite3_free(buf);
        VT_ERR(err, errlen, "sqlite open failed");
        sqlite3_close(db);
        return -1;
    }
    int rc = sqlite3_deserialize(db, "main", buf, (sqlite3_int64)len, (sqlite3_int64)len,
                                 SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
    if (rc != SQLITE_OK) {
        VT_ERR(err, errlen, "not a valid database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }
    sqlite3_create_function(db, "add_rm_list", 1, SQLITE_UTF8, NULL, add_rm_list_stub, NULL, NULL);
    *out = db;
    return 0;
}

int la_db_save(sqlite3 *db, const char *path, char *err, size_t errlen)
{
    sqlite3_int64 size = 0;
    unsigned char *data = sqlite3_serialize(db, "main", &size, 0);
    if (!data || size <= 0) { VT_ERR(err, errlen, "serialize failed"); sqlite3_free(data); return -1; }
    int rc = fs_write_all(path, data, (size_t)size);
    sqlite3_free(data);
    if (rc != 0) { VT_ERR(err, errlen, "cannot write %s", path); return -1; }
    return 0;
}

/* ---- planning ---- */

static int is_game_id(const char *id)
{
    /* Store and cartridge titles are 4 letters (PCSE, NPNA...) plus 5 digits; homebrew ids differ. */
    if (strlen(id) != 9) return 0;
    int prefix = strncmp(id, "PCS", 3) == 0 || strncmp(id, "NP", 2) == 0;
    if (!prefix || !isupper((unsigned char)id[3])) return 0;
    for (int i = 4; i < 9; i++)
        if (!isdigit((unsigned char)id[i])) return 0;
    return 1;
}

void la_plan_free(LaPlan *plan)
{
    free(plan->items);
    memset(plan, 0, sizeof *plan);
}

static int group_order(const LaItem *src, size_t n, char *const *listed, size_t m, LaItem *dst)
{
    if (n == 0) return 0;
    OrderItem *oi = malloc(n * sizeof *oi);
    size_t *idx = malloc(n * sizeof *idx);
    if (!oi || !idx) { free(oi); free(idx); return -1; }
    for (size_t i = 0; i < n; i++) {
        oi[i].id = src[i].id;
        oi[i].name = src[i].title[0] ? src[i].title : src[i].id;
    }
    order_build(oi, n, (const char *const *)listed, m, idx);
    for (size_t i = 0; i < n; i++) dst[i] = src[idx[i]];
    free(oi);
    free(idx);
    return 0;
}

int la_plan(sqlite3 *db, const OrderFile *order, LaPlan *plan, char *err, size_t errlen)
{
    memset(plan, 0, sizeof *plan);
    sqlite3_stmt *st;

    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM tbl_appinfo_icon WHERE titleId IS NULL", -1, &st, NULL) != SQLITE_OK ||
        sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        VT_ERR(err, errlen, "unexpected app.db schema");
        return -1;
    }
    int nulls = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (nulls > 0) {
        VT_ERR(err, errlen, "LiveArea has folders; move apps out of them first");
        return -1;
    }

    if (sqlite3_prepare_v2(db, "SELECT count(DISTINCT pageId) FROM tbl_appinfo_icon WHERE titleId LIKE 'NPXS%'", -1, &st, NULL) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
        plan->sys_pages = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(db, "SELECT titleId, COALESCE(title, '') FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%'",
                           -1, &st, NULL) != SQLITE_OK) {
        VT_ERR(err, errlen, "cannot read icons: %s", sqlite3_errmsg(db));
        return -1;
    }
    LaItem *cfw = NULL, *games = NULL;
    size_t ncfw = 0, ngames = 0, cap = 0;
    int bad = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (ncfw + ngames == cap) {
            cap = cap ? cap * 2 : 64;
            LaItem *c2 = realloc(cfw, cap * sizeof *cfw), *g2 = realloc(games, cap * sizeof *games);
            if (c2) cfw = c2;
            if (g2) games = g2;
            if (!c2 || !g2) { bad = 1; break; }
        }
        LaItem it;
        memset(&it, 0, sizeof it);
        snprintf(it.id, sizeof it.id, "%s", (const char *)sqlite3_column_text(st, 0));
        snprintf(it.title, sizeof it.title, "%s", (const char *)sqlite3_column_text(st, 1));
        int is_cfw = orderfile_has(order->cfw, order->ncfw, it.id) ||
                     (!orderfile_has(order->games, order->ngames, it.id) && !is_game_id(it.id));
        if (is_cfw) cfw[ncfw++] = it; else games[ngames++] = it;
    }
    sqlite3_finalize(st);
    if (bad) { free(cfw); free(games); VT_ERR(err, errlen, "out of memory"); return -1; }

    plan->items = malloc((ncfw + ngames + 1) * sizeof *plan->items);
    if (!plan->items) { free(cfw); free(games); VT_ERR(err, errlen, "out of memory"); return -1; }
    int rc = group_order(cfw, ncfw, order->cfw, order->ncfw, plan->items) ||
             group_order(games, ngames, order->games, order->ngames, plan->items + ncfw);
    free(cfw);
    free(games);
    if (rc) { la_plan_free(plan); VT_ERR(err, errlen, "out of memory"); return -1; }
    plan->n = ncfw + ngames;
    plan->n_cfw = ncfw;
    return 0;
}

/* ---- apply ---- */

static int run(sqlite3 *db, char *err, size_t errlen, const char *sql)
{
    char *msg = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &msg) != SQLITE_OK) {
        VT_ERR(err, errlen, "%s", msg ? msg : "sql error");
        sqlite3_free(msg);
        return -1;
    }
    return 0;
}

/* Returns the table's column names except the excluded ones, as a quoted, comma separated list. */
static char *column_list(sqlite3 *db, const char *table, const char *const *skip, size_t nskip)
{
    char sql[96];
    snprintf(sql, sizeof sql, "PRAGMA table_info(%s)", table);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    char *out = calloc(1, 4096);
    size_t used = 0;
    while (out && sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 1);
        int skipped = 0;
        for (size_t i = 0; i < nskip; i++)
            if (strcmp(name, skip[i]) == 0) skipped = 1;
        if (skipped) continue;
        /* Quote as an SQL identifier; embedded quotes are doubled. */
        char q[128];
        size_t k = 0;
        q[k++] = '"';
        for (const char *c = name; *c && k < sizeof q - 3; c++) {
            if (*c == '"') q[k++] = '"';
            q[k++] = *c;
        }
        q[k++] = '"';
        q[k] = '\0';
        if (used + k + 2 >= 4096) { free(out); out = NULL; break; }
        used += (size_t)snprintf(out + used, 4096 - used, "%s%s", used ? "," : "", q);
    }
    sqlite3_finalize(st);
    return out;
}

int la_apply(sqlite3 *db, const LaPlan *plan, int per_page, char *err, size_t errlen)
{
    static const char *const icon_skip[] = {"pageId", "pos", "titleId"};
    static const char *const page_skip[] = {"pageId", "pageNo"};
    char *icon_cols = column_list(db, "tbl_appinfo_icon", icon_skip, 3);
    char *page_cols = column_list(db, "tbl_appinfo_page", page_skip, 2);
    char sql[8192];
    int rc = -1;
    sqlite3_stmt *page_st = NULL, *icon_st = NULL, *q = NULL;

    if (!icon_cols || !page_cols || per_page < 1) { VT_ERR(err, errlen, "unexpected app.db schema"); goto done; }
    if (run(db, err, errlen, "BEGIN")) goto done;

    /* System pages (any NPXS* icon) stay as they are. */
    if (sqlite3_prepare_v2(db, "SELECT DISTINCT pageId FROM tbl_appinfo_icon WHERE titleId LIKE 'NPXS%'", -1, &q, NULL) != SQLITE_OK)
        goto fail;
    char sys_list[1024] = "";
    int sys_max = -1, nsys = 0;
    while (sqlite3_step(q) == SQLITE_ROW && strlen(sys_list) < sizeof sys_list - 16) {
        int id = sqlite3_column_int(q, 0);
        size_t used = strlen(sys_list);
        snprintf(sys_list + used, sizeof sys_list - used, "%s%d", nsys ? "," : "", id);
        if (id > sys_max) sys_max = id;
        nsys++;
    }
    sqlite3_finalize(q);
    q = NULL;
    if (nsys == 0) { VT_ERR(err, errlen, "no system pages found; refusing to continue"); goto fail; }

    snprintf(sql, sizeof sql,
             "CREATE TEMP TABLE vt_meta AS SELECT titleId, %s FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%%';"
             "CREATE TEMP TABLE vt_tpl AS SELECT %s FROM tbl_appinfo_page WHERE pageId IN (%s) LIMIT 1;"
             "DELETE FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%%';"
             "DELETE FROM tbl_appinfo_page WHERE pageId NOT IN (%s);",
             icon_cols, page_cols, sys_list, sys_list);
    if (run(db, err, errlen, sql)) goto fail;

    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(pageNo), -1) + 1 FROM tbl_appinfo_page", -1, &q, NULL) != SQLITE_OK ||
        sqlite3_step(q) != SQLITE_ROW)
        goto fail;
    int page_no = sqlite3_column_int(q, 0), page_id = sys_max + 1;
    sqlite3_finalize(q);
    q = NULL;

    snprintf(sql, sizeof sql, "INSERT INTO tbl_appinfo_page(pageId,pageNo,%s) SELECT ?,?,%s FROM vt_tpl", page_cols, page_cols);
    if (sqlite3_prepare_v2(db, sql, -1, &page_st, NULL) != SQLITE_OK) goto fail;
    snprintf(sql, sizeof sql, "INSERT INTO tbl_appinfo_icon(pageId,pos,titleId,%s) SELECT ?,?,titleId,%s FROM vt_meta WHERE titleId=?",
             icon_cols, icon_cols);
    if (sqlite3_prepare_v2(db, sql, -1, &icon_st, NULL) != SQLITE_OK) goto fail;

    /* CFW apps fill their own pages, then games start on a fresh page. */
    size_t groups[2][2] = {{0, plan->n_cfw}, {plan->n_cfw, plan->n}};
    for (int g = 0; g < 2; g++) {
        for (size_t start = groups[g][0]; start < groups[g][1]; start += (size_t)per_page) {
            sqlite3_reset(page_st);
            sqlite3_bind_int(page_st, 1, page_id);
            sqlite3_bind_int(page_st, 2, page_no);
            if (sqlite3_step(page_st) != SQLITE_DONE) goto fail;
            for (size_t k = 0; k < (size_t)per_page && start + k < groups[g][1]; k++) {
                sqlite3_reset(icon_st);
                sqlite3_bind_int(icon_st, 1, page_id);
                sqlite3_bind_int(icon_st, 2, (int)k);
                sqlite3_bind_text(icon_st, 3, plan->items[start + k].id, -1, SQLITE_STATIC);
                if (sqlite3_step(icon_st) != SQLITE_DONE) goto fail;
            }
            page_id++;
            page_no++;
        }
    }

    /* Drop stale rows without artwork and make the labels white (as the original tool did). */
    if (run(db, err, errlen,
            "DELETE FROM tbl_livearea WHERE titleId NOT LIKE 'NPXS%' AND background_image IS NULL AND gate_startupImage IS NULL;"
            "UPDATE tbl_livearea SET titleColor = 16777215 WHERE titleId NOT LIKE 'NPXS%';"))
        goto rollback;

    /* Every planned app must have landed on a page. */
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%'", -1, &q, NULL) != SQLITE_OK ||
        sqlite3_step(q) != SQLITE_ROW || (size_t)sqlite3_column_int(q, 0) != plan->n) {
        VT_ERR(err, errlen, "icon count mismatch after reorder");
        goto rollback;
    }
    if (run(db, err, errlen, "COMMIT")) goto rollback;
    rc = 0;
    goto done;

fail:
    VT_ERR(err, errlen, "reorder failed: %s", sqlite3_errmsg(db));
rollback:
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
done:
    sqlite3_finalize(q);
    sqlite3_finalize(page_st);
    sqlite3_finalize(icon_st);
    sqlite3_exec(db, "DROP TABLE IF EXISTS vt_meta; DROP TABLE IF EXISTS vt_tpl", NULL, NULL, NULL);
    free(icon_cols);
    free(page_cols);
    return rc;
}
