/*
 * Minimal SQLite VFS. The app only ever opens ":memory:" databases (app.db is
 * loaded and saved as a byte buffer), so no file access is needed. Building
 * SQLite with SQLITE_OS_OTHER avoids the unix VFS, which needs syscalls the
 * Vita libc does not have. Host tests use the same build.
 */
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../../third_party/sqlite3/sqlite3.h"

static int vt_open(sqlite3_vfs *v, const char *name, sqlite3_file *f, int flags, int *out)
{
    (void)v; (void)name; (void)f; (void)flags; (void)out;
    return SQLITE_CANTOPEN;
}

static int vt_delete(sqlite3_vfs *v, const char *name, int sync)
{
    (void)v; (void)name; (void)sync;
    return SQLITE_IOERR_DELETE;
}

static int vt_access(sqlite3_vfs *v, const char *name, int flags, int *res)
{
    (void)v; (void)name; (void)flags;
    *res = 0;
    return SQLITE_OK;
}

static int vt_fullpath(sqlite3_vfs *v, const char *name, int n, char *out)
{
    (void)v;
    sqlite3_snprintf(n, out, "%s", name);
    return SQLITE_OK;
}

static int vt_random(sqlite3_vfs *v, int n, char *out)
{
    (void)v;
    /* Only used for temp file names and rowid hints; a clock-seeded xorshift is enough. */
    static uint32_t s;
    if (!s) s = (uint32_t)time(NULL) | 1u;
    for (int i = 0; i < n; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        out[i] = (char)(s & 0xff);
    }
    return n;
}

static int vt_sleep(sqlite3_vfs *v, int us)
{
    (void)v;
    return us;
}

static int vt_time(sqlite3_vfs *v, double *t)
{
    (void)v;
    *t = 2440587.5 + (double)time(NULL) / 86400.0; /* Julian day from the Unix epoch */
    return SQLITE_OK;
}

static int vt_last_error(sqlite3_vfs *v, int n, char *b)
{
    (void)v; (void)n; (void)b;
    return 0;
}

int sqlite3_os_init(void)
{
    static sqlite3_vfs vfs = {
        1, sizeof(sqlite3_file), 512, NULL, "vitatidy-memory", NULL,
        vt_open, vt_delete, vt_access, vt_fullpath, NULL, NULL, NULL, NULL,
        vt_random, vt_sleep, vt_time, vt_last_error, NULL, NULL, NULL, NULL,
    };
    return sqlite3_vfs_register(&vfs, 1);
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
