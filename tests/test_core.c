/* Host tests for the portable core. Run with: cmake --build build && ctest --test-dir build */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/core/badge.h"
#include "../src/core/iso9660.h"
#include "../src/core/livearea.h"
#include "../src/core/order.h"
#include "../src/core/platform.h"
#include "../src/core/pngio.h"
#include "../src/core/tasks.h"
#include "../src/core/vt.h"

static int failures, checks;

#define CHECK(cond) \
    do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) \
    do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define CHECK_INT(a, b) \
    do { checks++; long long x_ = (a), y_ = (b); if (x_ != y_) { failures++; printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, x_, y_); } } while (0)

/* ---------- helpers ---------- */

static char tmp_root[256];

static void make_tmp(void)
{
    snprintf(tmp_root, sizeof tmp_root, "/tmp/vitatidy-test-XXXXXX");
    if (!mkdtemp(tmp_root)) { perror("mkdtemp"); exit(2); }
}

static void rm_tmp(void)
{
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp_root);
    if (system(cmd) != 0) printf("warning: cleanup failed\n");
}

static void path(char *dst, size_t n, const char *rel)
{
    snprintf(dst, n, "%s/%s", tmp_root, rel);
}

/* Deterministic picture with gradients and a little noise so it does not compress to nothing. */
static Image make_image(int w, int h, int alpha)
{
    Image img = {w, h, malloc((size_t)w * h * 4)};
    unsigned s = 12345;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t *p = img.rgba + ((size_t)y * w + x) * 4;
            s = s * 1103515245u + 12345u;
            p[0] = (uint8_t)(x * 255 / w);
            p[1] = (uint8_t)(y * 255 / h);
            p[2] = (uint8_t)(128 + ((s >> 16) & 15));
            p[3] = alpha ? (uint8_t)(x < 4 ? 0 : 255) : 255;
        }
    }
    return img;
}

static uint8_t *png_of(const Image *img, size_t *len)
{
    uint8_t *p = png_encode_palette(img, 64, len);
    assert(p);
    return p;
}

/* ---------- ordering ---------- */

static void test_order(void)
{
    CHECK_STR(order_skip_article("The Last of Us"), "Last of Us");
    CHECK_STR(order_skip_article("an Apple"), "Apple");
    CHECK_STR(order_skip_article("A Plague Tale"), "Plague Tale");
    CHECK_STR(order_skip_article("Another World"), "Another World");
    CHECK_STR(order_skip_article("The "), "The ");
    CHECK(order_cmp("the alpha", "Beta") < 0);
    CHECK(order_cmp("ZED", "zed") == 0);

    OrderItem items[] = {{"d", "Persona 4: Dancing"}, {"g", "Persona 4 Golden"}, {"a", "The Alpha"}, {"z", "Zed"}, {"b", "Beta"}};
    const char *listed[] = {"g", "d", "missing"};
    size_t out[5];
    CHECK_INT(order_build(items, 5, listed, 3, out), 5);
    /* Alpha and Beta sort before Persona; Zed after; listed order is kept. */
    CHECK_STR(items[out[0]].id, "a");
    CHECK_STR(items[out[1]].id, "b");
    CHECK_STR(items[out[2]].id, "g");
    CHECK_STR(items[out[3]].id, "d");
    CHECK_STR(items[out[4]].id, "z");

    CHECK_INT(order_build(items, 5, NULL, 0, out), 5);
    CHECK_STR(items[out[0]].id, "a");
    CHECK_STR(items[out[4]].id, "z");

    OrderFile of;
    CHECK_INT(orderfile_parse("# head\nPCSE1  # one\n[cfw]\nVITASHELL\n\n[games]\n  PCSE2\r\n", 1, &of), 0);
    CHECK_INT(of.ngames, 2);
    CHECK_INT(of.ncfw, 1);
    CHECK_STR(of.games[0], "PCSE1");
    CHECK_STR(of.games[1], "PCSE2");
    CHECK_STR(of.cfw[0], "VITASHELL");
    orderfile_free(&of);

    CHECK_INT(orderfile_parse("# comment\nGame #2 (USA).iso\n", 0, &of), 0);
    CHECK_INT(of.ngames, 1);
    CHECK_STR(of.games[0], "Game #2 (USA).iso");
    orderfile_free(&of);
}

static void test_clean_title(void)
{
    char t[64] = "  FREEDOM\nWARS\r\n  2 \t";
    la_clean_title(t);
    CHECK_STR(t, "FREEDOM WARS 2");
    char u[8] = "";
    la_clean_title(u);
    CHECK_STR(u, "");
}

/* ---------- PNG and badge ---------- */

static void test_png(void)
{
    Image src = make_image(64, 32, 0), back;
    size_t len;
    uint8_t *png = png_encode_truecolor(&src, &len);
    CHECK(png != NULL);
    CHECK(img_decode(png, len, &back) == 0);
    CHECK(back.w == 64 && back.h == 32 && memcmp(back.rgba, src.rgba, 64 * 32 * 4) == 0);
    img_free(&back);
    free(png);

    Image a = make_image(40, 40, 1);
    png = png_encode_truecolor(&a, &len);
    CHECK(img_decode(png, len, &back) == 0 && memcmp(back.rgba, a.rgba, 40 * 40 * 4) == 0);
    img_free(&back);
    free(png);

    /* Few colors round-trip exactly through the palette encoder, transparency included. */
    Image flat = {16, 16, malloc(16 * 16 * 4)};
    for (int i = 0; i < 256; i++) {
        uint8_t *p = flat.rgba + i * 4;
        p[0] = (uint8_t)((i % 3) * 100); p[1] = 20; p[2] = (uint8_t)((i % 5) * 50); p[3] = i % 7 == 0 ? 0 : 255;
        if (p[3] == 0) { p[0] = p[1] = p[2] = 0; }
    }
    png = png_encode_palette(&flat, 256, &len);
    CHECK(png != NULL && img_decode(png, len, &back) == 0 && memcmp(back.rgba, flat.rgba, 16 * 16 * 4) == 0);
    img_free(&back);
    free(png);

    /* Size-limited encoding: a tiny budget forces a palette; an impossible one returns NULL. */
    Image big = make_image(144, 80, 0);
    size_t full;
    png = png_encode_truecolor(&big, &full);
    free(png);
    size_t fit_len;
    png = png_encode_fit(&big, full / 2, &fit_len);
    CHECK(png != NULL && fit_len <= full / 2 && img_decode(png, fit_len, &back) == 0 && back.w == 144);
    img_free(&back);
    free(png);
    CHECK(png_encode_fit(&big, 50, &fit_len) == NULL);

    CHECK(img_decode((const uint8_t *)"not a png at all", 16, &back) != 0);
    free(src.rgba); free(a.rgba); free(flat.rgba); free(big.rgba);
}

static void test_badge(void)
{
    Image img = make_image(128, 128, 0);
    Image before = {128, 128, malloc(128 * 128 * 4)};
    memcpy(before.rgba, img.rgba, 128 * 128 * 4);
    Badge b = badge_for(128, 128);
    badge_draw(&img, &b);
    const uint8_t *in = img.rgba + (64 * 128 + 73) * 4;
    CHECK(abs(in[0] - 34) <= 2 && abs(in[1] - 197) <= 2 && abs(in[2] - 94) <= 2);
    CHECK(memcmp(img.rgba, before.rgba, 4) == 0);                                   /* far corner untouched */
    CHECK(memcmp(img.rgba + (64 * 128 + 90) * 4, before.rgba + (64 * 128 + 90) * 4, 4) == 0); /* outside the circle */
    int white = 0;
    for (int y = 50; y < 80; y++)
        for (int x = 50; x < 80; x++) {
            const uint8_t *p = img.rgba + ((size_t)y * 128 + x) * 4;
            if (p[0] > 240 && p[1] > 240 && p[2] > 240) white++;
        }
    CHECK(white > 10); /* the checkmark is there */

    Badge psp = badge_for(144, 80);
    CHECK(psp.cx == 126.0f && psp.cy == 18.0f);

    /* Transparent pixels under the badge turn opaque. */
    Image clear = {128, 128, calloc(128 * 128, 4)};
    badge_draw(&clear, &b);
    CHECK(clear.rgba[(64 * 128 + 73) * 4 + 3] == 255);
    CHECK(clear.rgba[3] == 0);
    free(img.rgba); free(before.rgba); free(clear.rgba);
}

/* ---------- ISO ---------- */

enum { ICON_LBA = 24, ISO_SECTORS = 96 };

static void put_record(uint8_t *dir, size_t *pos, const char *name, int name_len, uint32_t lba, uint32_t len, int is_dir)
{
    uint8_t *r = dir + *pos;
    size_t rec = 33 + (size_t)name_len + ((name_len % 2) == 0 ? 1 : 0);
    r[0] = (uint8_t)rec;
    for (int i = 0; i < 4; i++) { r[2 + i] = (uint8_t)(lba >> (8 * i)); r[10 + i] = (uint8_t)(len >> (8 * i)); }
    r[25] = is_dir ? 2 : 0;
    r[32] = (uint8_t)name_len;
    memcpy(r + 33, name, (size_t)name_len);
    *pos += rec;
}

/* Builds a small PSP-like ISO: root -> PSP_GAME -> ICON0.PNG, followed by another file at the next sector. */
static void build_iso(const char *file, const uint8_t *icon, size_t icon_len)
{
    uint8_t *iso = calloc(ISO_SECTORS, ISO_SECTOR);
    uint8_t *pvd = iso + 16 * ISO_SECTOR;
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    size_t pos = 156;
    put_record(pvd, &pos, "\0", 1, 20, ISO_SECTOR, 1);
    iso[17 * ISO_SECTOR] = 255; memcpy(iso + 17 * ISO_SECTOR + 1, "CD001", 5);

    size_t d = 0;
    uint8_t *root = iso + 20 * ISO_SECTOR;
    put_record(root, &d, "\0", 1, 20, ISO_SECTOR, 1);
    put_record(root, &d, "\1", 1, 20, ISO_SECTOR, 1);
    put_record(root, &d, "PSP_GAME", 8, 21, ISO_SECTOR, 1);
    d = 0;
    uint8_t *game = iso + 21 * ISO_SECTOR;
    put_record(game, &d, "\0", 1, 21, ISO_SECTOR, 1);
    put_record(game, &d, "\1", 1, 20, ISO_SECTOR, 1);
    put_record(game, &d, "PARAM.SFO;1", 11, 23, 16, 0);
    put_record(game, &d, "ICON0.PNG;1", 11, ICON_LBA, (uint32_t)icon_len, 0);
    memset(iso + 23 * ISO_SECTOR, 0x5a, 16);
    memcpy(iso + ICON_LBA * ISO_SECTOR, icon, icon_len);
    size_t next = ICON_LBA + (icon_len + ISO_SECTOR - 1) / ISO_SECTOR;
    memset(iso + next * ISO_SECTOR, 0xa5, ISO_SECTOR); /* data of the next file must survive patching */
    assert(fs_write_all(file, iso, (size_t)ISO_SECTORS * ISO_SECTOR) == 0);
    free(iso);
}

static void test_iso(void)
{
    Image img = make_image(144, 80, 0);
    size_t len;
    uint8_t *png = png_of(&img, &len);
    char file[300], err[128] = "";
    path(file, sizeof file, "t.iso");
    build_iso(file, png, len);

    FILE *f = fopen(file, "r+b");
    uint64_t off;
    uint32_t ilen;
    CHECK(iso_find_icon0(f, &off, &ilen, err, sizeof err) == 0);
    CHECK_INT(off, ICON_LBA * ISO_SECTOR);
    CHECK_INT(ilen, len);
    uint8_t *back = malloc(len);
    CHECK(iso_read_at(f, off, back, len) == 0 && memcmp(back, png, len) == 0);
    free(back);
    fclose(f);

    char junk[300];
    path(junk, sizeof junk, "junk.iso");
    uint8_t zeros[ISO_SECTOR * 20] = {0};
    fs_write_all(junk, zeros, sizeof zeros);
    f = fopen(junk, "rb");
    CHECK(iso_find_icon0(f, &off, &ilen, err, sizeof err) != 0);
    CHECK(strstr(err, "ISO9660") != NULL);
    fclose(f);
    free(png);
    free(img.rgba);
}

/* ---------- fuzz: untrusted bytes must never crash ---------- */

static void test_fuzz(void)
{
    Image src = make_image(32, 24, 1);
    size_t len;
    uint8_t *png = png_encode_truecolor(&src, &len);
    unsigned s = 7;
    int decoded = 0;
    for (int iter = 0; iter < 3000; iter++) {
        uint8_t *m = malloc(len);
        memcpy(m, png, len);
        int flips = 1 + iter % 6;
        for (int k = 0; k < flips; k++) {
            s = s * 1103515245u + 12345u;
            m[(s >> 8) % len] ^= (uint8_t)(1u << ((s >> 4) & 7));
        }
        size_t cut = iter % 7 == 0 ? len / 2 : len;
        Image out;
        if (img_decode(m, cut, &out) == 0) { decoded++; img_free(&out); }
        free(m);
    }
    CHECK(decoded > 0); /* many flips hit pixel data, so some inputs still decode */

    /* Mutated ISO headers: every outcome is fine except a crash or a hang. */
    Image big = make_image(144, 80, 0);
    size_t ilen;
    uint8_t *icon = png_of(&big, &ilen);
    char file[300], mut[300];
    path(file, sizeof file, "fz.iso");
    path(mut, sizeof mut, "fz-mut.iso");
    build_iso(file, icon, ilen);
    uint8_t *iso;
    size_t isolen;
    assert(fs_read_all(file, &iso, &isolen, 1 << 24) == 0);
    for (int iter = 0; iter < 2000; iter++) {
        uint8_t *m = malloc(isolen);
        memcpy(m, iso, isolen);
        for (int k = 0; k < 1 + iter % 8; k++) {
            s = s * 1103515245u + 12345u;
            /* Aim at the descriptor and directory sectors where the parser reads. */
            size_t region = (s >> 12) % 4;
            size_t base = region == 0 ? 16 * ISO_SECTOR : region == 1 ? 17 * ISO_SECTOR : region == 2 ? 20 * ISO_SECTOR : 21 * ISO_SECTOR;
            m[base + (s >> 4) % 200] = (uint8_t)(s >> 20);
        }
        assert(fs_write_all(mut, m, isolen) == 0);
        FILE *f = fopen(mut, "rb");
        uint64_t off;
        uint32_t l;
        char err[64];
        iso_find_icon0(f, &off, &l, err, sizeof err);
        fclose(f);
        free(m);
    }
    free(iso);
    free(icon);
    free(big.rgba);
    free(png);
    free(src.rgba);
}

/* ---------- LiveArea ---------- */

static void sql(sqlite3 *db, const char *q)
{
    char *msg = NULL;
    if (sqlite3_exec(db, q, NULL, NULL, &msg) != SQLITE_OK) { printf("SQL error: %s\n  %s\n", msg, q); exit(2); }
}

static void write_schema_db(const char *file)
{
    uint8_t *schema;
    size_t n;
    assert(fs_read_all("tests/fixtures/appdb_schema.sql", &schema, &n, 1 << 20) == 0);
    char *text = malloc(n + 1);
    memcpy(text, schema, n);
    text[n] = '\0';
    free(schema);
    sqlite3 *db;
    assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
    sqlite3_create_function(db, "add_rm_list", 1, SQLITE_UTF8, NULL, NULL, NULL, NULL);
    sql(db, text);
    free(text);
    sql(db, "INSERT INTO tbl_appinfo_page(pageId,pageNo,bgColor) VALUES (2,0,33554431),(3,1,33554431),(4,2,33554431)");
    sql(db, "INSERT INTO tbl_appinfo_icon(pageId,pos,title,type,titleId,icon0Type) VALUES"
            "(2,0,'PS Store',0,'NPXS10002',5),(2,4,'Browser',0,'NPXS10099',5),(3,0,'Videos',0,'NPXS10010',5)");
    /* One page with homebrew and games in a messy order. */
    sql(db, "INSERT INTO tbl_appinfo_icon(pageId,pos,title,type,titleId,icon0Type) VALUES"
            "(4,0,'Zeta Quest',0,'PCSE00003',5),(4,1,'VitaShell',0,'VITASHELL',5),(4,2,'The Alpha',0,'PCSA00001',5),"
            "(4,3,'Persona 4 Golden',0,'PCSE00120',5),(4,4,'Persona 4: Dancing',0,'PCSE00764',5),(4,5,'Adrenaline',0,'PSPEMUCFW',5),"
            "(4,6,'Be'||char(10)||'ta ',0,'PCSB00002',5)");
    sql(db, "INSERT INTO tbl_livearea(titleId,org_path,style,formatVer,background_image,gate_startupImage,titleColor) VALUES"
            "('PCSE00003','p','a','1',NULL,NULL,0),('PCSA00001','p','a','1','bg.png','gate.png',0),('NPXS10002','p','a','1','x','y',7)");
    assert(la_db_save(db, file, NULL, 0) == 0);
    sqlite3_close(db);
}

static int count(sqlite3 *db, const char *q)
{
    sqlite3_stmt *st;
    assert(sqlite3_prepare_v2(db, q, -1, &st, NULL) == SQLITE_OK);
    assert(sqlite3_step(st) == SQLITE_ROW);
    int v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

static void titles_on_page(sqlite3 *db, int page_no, char *out, size_t n)
{
    sqlite3_stmt *st;
    assert(sqlite3_prepare_v2(db, "SELECT i.titleId FROM tbl_appinfo_icon i JOIN tbl_appinfo_page p USING(pageId) "
                                  "WHERE p.pageNo = ? ORDER BY i.pos", -1, &st, NULL) == SQLITE_OK);
    sqlite3_bind_int(st, 1, page_no);
    out[0] = '\0';
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (out[0]) strncat(out, ",", n - strlen(out) - 1);
        strncat(out, (const char *)sqlite3_column_text(st, 0), n - strlen(out) - 1);
    }
    sqlite3_finalize(st);
}

static void test_livearea(void)
{
    char file[300], err[200] = "", pages[512];
    path(file, sizeof file, "app.db");
    write_schema_db(file);

    sqlite3 *db;
    CHECK(la_db_load(file, &db, err, sizeof err) == 0);
    OrderFile of;
    CHECK(orderfile_parse("[cfw]\nVITASHELL\n[games]\nPCSE00120  # Persona 4 Golden\nPCSE00764\n", 1, &of) == 0);
    LaPlan plan;
    CHECK(la_plan(db, &of, &plan, err, sizeof err) == 0);
    CHECK_INT(plan.n, 7);
    CHECK_INT(plan.n_cfw, 2); /* VITASHELL listed; PSPEMUCFW is not a store id so it is treated as CFW */
    CHECK_STR(plan.items[0].id, "PSPEMUCFW"); /* Adrenaline sorts before VitaShell only if unlisted; see below */
    orderfile_free(&of);
    la_plan_free(&plan);

    CHECK(orderfile_parse("[cfw]\nVITASHELL\nPSPEMUCFW\n[games]\nPCSE00120\nPCSE00764\n", 1, &of) == 0);
    CHECK(la_plan(db, &of, &plan, err, sizeof err) == 0);
    CHECK_STR(plan.items[0].id, "VITASHELL");
    CHECK_STR(plan.items[1].id, "PSPEMUCFW");
    /* Alpha, Beta come before the listed Persona pair; Zeta goes last. */
    CHECK_STR(plan.items[2].id, "PCSA00001");
    CHECK_STR(plan.items[3].id, "PCSB00002");
    CHECK_STR(plan.items[4].id, "PCSE00120");
    CHECK_STR(plan.items[5].id, "PCSE00764");
    CHECK_STR(plan.items[6].id, "PCSE00003");

    CHECK_STR(plan.items[3].title, "Be ta"); /* line breaks in titles become one space */
    CHECK(la_apply(db, &plan, 3, err, sizeof err) == 0);
    CHECK_INT(count(db, "SELECT count(*) FROM tbl_appinfo_page"), 2 + 3); /* system pages, then 7 apps in pages of 3 */
    titles_on_page(db, 0, pages, sizeof pages);
    CHECK_STR(pages, "NPXS10002,NPXS10099");
    /* The hole between the two system icons is closed: positions are 0 and 1. */
    CHECK_INT(count(db, "SELECT max(pos) FROM tbl_appinfo_icon i JOIN tbl_appinfo_page p USING(pageId) WHERE p.pageNo = 0"), 1);
    titles_on_page(db, 1, pages, sizeof pages);
    CHECK_STR(pages, "NPXS10010");
    /* CFW apps and games share one run: no empty slots between them. */
    titles_on_page(db, 2, pages, sizeof pages);
    CHECK_STR(pages, "VITASHELL,PSPEMUCFW,PCSA00001");
    titles_on_page(db, 3, pages, sizeof pages);
    CHECK_STR(pages, "PCSB00002,PCSE00120,PCSE00764");
    titles_on_page(db, 4, pages, sizeof pages);
    CHECK_STR(pages, "PCSE00003");
    CHECK_INT(count(db, "SELECT count(*) FROM (SELECT pageId FROM tbl_appinfo_icon GROUP BY pageId HAVING max(pos) + 1 <> count(*))"), 0);
    /* Rows without artwork are dropped and labels turn white; system rows are untouched. */
    CHECK_INT(count(db, "SELECT count(*) FROM tbl_livearea WHERE titleId='PCSE00003'"), 0);
    CHECK_INT(count(db, "SELECT titleColor FROM tbl_livearea WHERE titleId='PCSA00001'"), 0xFFFFFF);
    CHECK_INT(count(db, "SELECT titleColor FROM tbl_livearea WHERE titleId='NPXS10002'"), 7);
    CHECK_INT(count(db, "SELECT count(*) FROM tbl_appinfo_page WHERE bgColor = 33554431"), 5);
    la_plan_free(&plan);
    orderfile_free(&of);
    sqlite3_close(db);

    /* A database with folders (NULL title ids) is refused instead of corrupted. */
    CHECK(la_db_load(file, &db, err, sizeof err) == 0);
    sql(db, "INSERT INTO tbl_appinfo_icon(pageId,pos,title,type,titleId,icon0Type) VALUES (2,5,'Folder',1,NULL,5)");
    memset(&of, 0, sizeof of);
    CHECK(la_plan(db, &of, &plan, err, sizeof err) != 0 && strstr(err, "folders") != NULL);
    sqlite3_close(db);

    CHECK(la_db_load("/nonexistent/app.db", &db, err, sizeof err) != 0);
}

/* ---------- tasks ---------- */

static void setup_paths(Paths *p)
{
    memset(p, 0, sizeof *p);
    path(p->db, sizeof p->db, "ur0/app.db");
    path(p->appmeta, sizeof p->appmeta, "ur0/appmeta");
    path(p->app, sizeof p->app, "ux0/app");
    path(p->data, sizeof p->data, "ux0/data/VitaTidy");
    path(p->defaults, sizeof p->defaults, "app0/data");
    path(p->psp_iso, sizeof p->psp_iso, "ux0/pspemu/ISO");
    char d[300];
    path(d, sizeof d, "ur0/app.db");
    fs_mkdirs_for(d);
    fs_mkdirs(p->psp_iso);
    fs_mkdirs(p->defaults);
}

static uint8_t *slurp(const char *file, size_t *len)
{
    uint8_t *b;
    assert(fs_read_all(file, &b, len, 1 << 26) == 0);
    return b;
}

static void test_psp(void)
{
    Paths p;
    setup_paths(&p);
    Image img = make_image(144, 80, 0);
    unsigned seed = 99;
    for (size_t i = 0; i < 144 * 80; i++) {
        for (int c = 0; c < 3; c++) {
            seed = seed * 1103515245u + 12345u;
            img.rgba[i * 4 + c] = (uint8_t)(img.rgba[i * 4 + c] / 2 + ((seed >> 16) & 127));
        }
    }
    size_t ilen;
    uint8_t *icon = png_encode_truecolor(&img, &ilen);
    const char *names[] = {"Zelda.iso", "The Beta (USA).iso", "alpha.ISO", "Gamma.cso", "notes.txt"};
    for (int i = 0; i < 4; i++) {
        char f[400];
        snprintf(f, sizeof f, "%s/%s", p.psp_iso, names[i]);
        build_iso(f, icon, ilen);
    }
    char f[400];
    snprintf(f, sizeof f, "%s/%s", p.psp_iso, names[4]);
    fs_write_all(f, "x", 1);

    /* Order file: Zelda then Gamma. Unlisted files slot in alphabetically around them (article ignored). */
    char of[400];
    snprintf(of, sizeof of, "%s/psp-order.txt", p.defaults);
    CHECK(fs_write_all(of, "Zelda.iso\nGamma.cso\n", 19) == 0);
    char err[200] = "";
    PspPlan plan;
    CHECK(task_psp_plan(&p, &plan, err, sizeof err) == 0);
    CHECK_INT(plan.n, 4);
    CHECK_STR(plan.names[0], "alpha.ISO");
    CHECK_STR(plan.names[1], "The Beta (USA).iso");
    CHECK_STR(plan.names[2], "Zelda.iso");
    CHECK_STR(plan.names[3], "Gamma.cso");
    CHECK(task_psp_apply(&p, &plan, err, sizeof err) == 0);
    vt_ticks prev = 0;
    for (size_t i = 0; i < plan.n; i++) {
        snprintf(f, sizeof f, "%s/%s", p.psp_iso, plan.names[i]);
        vt_ticks t = plat_get_mtime(f);
        if (i) CHECK_INT((prev - t) / 1000000, 60); /* one minute older each */
        prev = t;
    }
    psp_plan_free(&plan);

    /* Badge on and off: only the icon bytes change, the next file's data survives, and off restores exactly. */
    GameList gl;
    CHECK(task_psp_games(&p, &gl, err, sizeof err) == 0);
    CHECK_INT(gl.n, 3); /* .cso cannot be badged */
    CHECK_STR(gl.v[0].title, "alpha");
    CHECK_STR(gl.v[1].title, "The Beta (USA)");
    CHECK(!gl.v[0].marked);
    gamelist_free(&gl);

    snprintf(f, sizeof f, "%s/Zelda.iso", p.psp_iso);
    size_t before_len, after_len;
    uint8_t *before = slurp(f, &before_len);
    int marked = -1;
    CHECK(task_psp_toggle(&p, "Zelda.iso", &marked, err, sizeof err) == 0 && marked == 1);
    uint8_t *after = slurp(f, &after_len);
    CHECK_INT(after_len, before_len);
    CHECK(memcmp(after, before, (size_t)ICON_LBA * ISO_SECTOR) == 0);                 /* everything before the icon */
    CHECK(memcmp(after + (ICON_LBA + 1 + ilen / ISO_SECTOR) * ISO_SECTOR, before + (ICON_LBA + 1 + ilen / ISO_SECTOR) * ISO_SECTOR,
                 before_len - (ICON_LBA + 1 + ilen / ISO_SECTOR) * ISO_SECTOR) == 0);  /* everything after it */
    CHECK(memcmp(after + ICON_LBA * ISO_SECTOR, before + ICON_LBA * ISO_SECTOR, ilen) != 0);
    Image badged;
    CHECK(img_decode(after + ICON_LBA * ISO_SECTOR, ilen, &badged) == 0);
    const uint8_t *g = badged.rgba + (18 * 144 + 126 + 8) * 4;
    CHECK(abs(g[0] - 34) < 40 && abs(g[1] - 197) < 40 && abs(g[2] - 94) < 40);
    img_free(&badged);
    free(after);

    CHECK(task_psp_games(&p, &gl, err, sizeof err) == 0);
    int seen = 0;
    for (size_t i = 0; i < gl.n; i++) if (strcmp(gl.v[i].id, "Zelda.iso") == 0) seen = gl.v[i].marked;
    CHECK(seen == 1);
    gamelist_free(&gl);

    CHECK(task_psp_toggle(&p, "Zelda.iso", &marked, err, sizeof err) == 0 && marked == 0);
    after = slurp(f, &after_len);
    CHECK(after_len == before_len && memcmp(after, before, before_len) == 0);
    free(after);
    free(before);

    /* A stale backup (ISO replaced by a clean one) must not count as marked. */
    snprintf(f, sizeof f, "%s/Zelda.iso", p.psp_iso);
    build_iso(f, icon, ilen);
    char bk[400];
    snprintf(bk, sizeof bk, "%s/originals/psp/Zelda.iso.png", p.data);
    fs_mkdirs_for(bk);
    fs_write_all(bk, icon, ilen);
    CHECK(task_psp_games(&p, &gl, err, sizeof err) == 0);
    for (size_t i = 0; i < gl.n; i++) CHECK(!gl.v[i].marked);
    gamelist_free(&gl);

    /* A broken icon inside the ISO is reported, not written over. */
    uint8_t junk_icon[64];
    memset(junk_icon, 0xEE, sizeof junk_icon);
    snprintf(f, sizeof f, "%s/alpha.ISO", p.psp_iso);
    build_iso(f, junk_icon, sizeof junk_icon);
    size_t j_before_len, j_after_len;
    uint8_t *j_before = slurp(f, &j_before_len);
    CHECK(task_psp_toggle(&p, "alpha.ISO", &marked, err, sizeof err) != 0 && strstr(err, "PNG") != NULL);
    uint8_t *j_after = slurp(f, &j_after_len);
    CHECK(j_before_len == j_after_len && memcmp(j_before, j_after, j_before_len) == 0);
    free(j_before);
    free(j_after);

    CHECK(task_psp_toggle(&p, "../evil.iso", &marked, err, sizeof err) != 0);
    CHECK(task_psp_toggle(&p, "missing.iso", &marked, err, sizeof err) != 0);
    free(icon);
    free(img.rgba);
}

static void test_vita(void)
{
    Paths p;
    setup_paths(&p);
    char err[200] = "", f[400];
    write_schema_db(p.db);

    Image img = make_image(128, 128, 1);
    size_t len;
    uint8_t *icon = png_encode_truecolor(&img, &len);
    const char *ids[] = {"PCSE00003", "PCSA00001"};
    for (int i = 0; i < 2; i++) {
        snprintf(f, sizeof f, "%s/%s/sce_sys/icon0.png", p.app, ids[i]);
        fs_mkdirs_for(f);
        fs_write_all(f, icon, len);
    }

    /* A homebrew tool listed under [cfw] has an icon too but is not offered as a game. */
    snprintf(f, sizeof f, "%s/VITASHELL/sce_sys/icon0.png", p.app);
    fs_mkdirs_for(f);
    fs_write_all(f, icon, len);
    snprintf(f, sizeof f, "%s/order.txt", p.defaults);
    fs_write_all(f, "[cfw]\nVITASHELL\n", 16);
    snprintf(f, sizeof f, "%s/PCSA00001/icon0.png", p.appmeta); /* the firmware cache entry exists for installed games */
    fs_mkdirs_for(f);
    fs_write_all(f, icon, len);
    snprintf(f, sizeof f, "%s/PCSE00003/icon0.png", p.appmeta);
    CHECK(!fs_exists(f));

    GameList gl;
    CHECK(task_vita_games(&p, &gl, err, sizeof err) == 0);
    for (size_t i = 0; i < gl.n; i++) CHECK(strchr(gl.v[i].title, '\n') == NULL);
    CHECK_INT(gl.n, 2); /* only games that have an icon on disk; VitaShell and Adrenaline are skipped */
    for (size_t i = 0; i < gl.n; i++) CHECK(!gl.v[i].marked);
    gamelist_free(&gl);

    snprintf(f, sizeof f, "%s/PCSA00001/sce_sys/icon0.png", p.app);
    int marked = -1;
    CHECK(task_vita_toggle(&p, "PCSA00001", &marked, err, sizeof err) == 0 && marked == 1);
    size_t now_len;
    uint8_t *now = slurp(f, &now_len);
    CHECK(now_len != len || memcmp(now, icon, len) != 0);
    Image badged;
    CHECK(img_decode(now, now_len, &badged) == 0 && badged.w == 128);
    const uint8_t *g = badged.rgba + (64 * 128 + 73) * 4;
    CHECK(abs(g[0] - 34) <= 2 && abs(g[1] - 197) <= 2);
    CHECK_INT(badged.rgba[3], 0);                 /* the transparent corner stays transparent */
    img_free(&badged);
    char meta[400];
    snprintf(meta, sizeof meta, "%s/PCSA00001/icon0.png", p.appmeta);
    size_t meta_len;
    uint8_t *m = slurp(meta, &meta_len);
    CHECK(meta_len == now_len && memcmp(m, now, now_len) == 0);
    free(m);
    free(now);

    CHECK(task_vita_games(&p, &gl, err, sizeof err) == 0);
    for (size_t i = 0; i < gl.n; i++) CHECK(gl.v[i].marked == (strcmp(gl.v[i].id, "PCSA00001") == 0));
    gamelist_free(&gl);

    CHECK(task_vita_toggle(&p, "PCSA00001", &marked, err, sizeof err) == 0 && marked == 0);
    now = slurp(f, &now_len);
    CHECK(now_len == len && memcmp(now, icon, len) == 0);
    free(now);
    char bk[400];
    snprintf(bk, sizeof bk, "%s/originals/vita/PCSA00001.png", p.data);
    CHECK(!fs_exists(bk));
    CHECK(task_vita_toggle(&p, "../x", &marked, err, sizeof err) != 0);
    CHECK(task_vita_toggle(&p, "PCSZ99999", &marked, err, sizeof err) != 0);

    /* Full LiveArea flow through the task layer: backups first, then the real file is replaced. */
    LaPlan plan;
    CHECK(task_livearea_plan(&p, &plan, err, sizeof err) == 0);
    size_t old_len;
    uint8_t *old = slurp(p.db, &old_len);
    CHECK(task_livearea_apply(&p, &plan, err, sizeof err) == 0);
    size_t orig_len;
    snprintf(f, sizeof f, "%s/backup/app.db.orig", p.data);
    uint8_t *orig = slurp(f, &orig_len);
    CHECK(orig_len == old_len && memcmp(orig, old, old_len) == 0);
    free(orig);
    free(old);
    sqlite3 *db;
    CHECK(la_db_load(p.db, &db, err, sizeof err) == 0);
    CHECK_INT(count(db, "SELECT count(*) FROM tbl_appinfo_icon WHERE titleId NOT LIKE 'NPXS%'"), 7);
    CHECK_INT(count(db, "SELECT count(*) FROM tbl_appinfo_page WHERE pageNo >= 2"), 1); /* 7 apps, 10 per page */
    sqlite3_close(db);
    la_plan_free(&plan);
    free(icon);
    free(img.rgba);
}

int main(void)
{
    make_tmp();
    test_order();
    test_clean_title();
    test_png();
    test_badge();
    test_iso();
    test_fuzz();
    test_livearea();
    test_psp();
    test_vita();
    rm_tmp();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
