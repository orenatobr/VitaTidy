/* Developer tool: runs the LiveArea reorder on a database file. Usage: la_cli app.db order.txt out.db */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/livearea.h"
#include "../src/core/vt.h"

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s app.db order.txt out.db\n", argv[0]); return 2; }
    char err[200] = "";
    uint8_t *text;
    size_t len;
    if (fs_read_all(argv[2], &text, &len, 1 << 20) != 0) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    char *z = realloc(text, len + 1);
    z[len] = '\0';
    OrderFile of;
    orderfile_parse(z, 1, &of);
    sqlite3 *db;
    LaPlan plan;
    if (la_db_load(argv[1], &db, err, sizeof err) || la_plan(db, &of, &plan, err, sizeof err) ||
        la_apply(db, &plan, LA_ITEMS_PER_PAGE, err, sizeof err) || la_db_save(db, argv[3], err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    for (size_t i = 0; i < plan.n; i++)
        printf("%s %zu %s  %s\n", i < plan.n_cfw ? "cfw " : "game", i, plan.items[i].id, plan.items[i].title);
    return 0;
}
