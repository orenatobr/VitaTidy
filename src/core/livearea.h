/* LiveArea reorder: rewrites the page layout in app.db (an in-memory copy). */
#ifndef VT_LIVEAREA_H
#define VT_LIVEAREA_H

#include <stddef.h>

#include "order.h"
#include "../../third_party/sqlite3/sqlite3.h"

#define LA_ITEMS_PER_PAGE 10

typedef struct {
    char id[24];
    char title[96];
} LaItem;

typedef struct {
    LaItem *items; /* CFW apps first, then games */
    size_t n;
    size_t n_cfw;
    int sys_pages; /* pages holding system apps; our pages start after them */
} LaPlan;

/* Loads app.db from path into an in-memory database. Returns 0 on success. */
int la_db_load(const char *path, sqlite3 **db, char *err, size_t errlen);
/* Serializes the database to path. Returns 0 on success. */
int la_db_save(sqlite3 *db, const char *path, char *err, size_t errlen);

/* Computes the new order (system NPXS* apps are never touched). Returns 0 on success. */
int la_plan(sqlite3 *db, const OrderFile *order, LaPlan *plan, char *err, size_t errlen);
/* Applies a plan to the database in one transaction. Returns 0 on success. */
int la_apply(sqlite3 *db, const LaPlan *plan, int per_page, char *err, size_t errlen);
void la_plan_free(LaPlan *plan);

#endif
