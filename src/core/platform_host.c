/* Host implementation used by the tests. */
#include "platform.h"

#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

vt_ticks plat_now(void)
{
    return (vt_ticks)time(NULL) * 1000000;
}

int plat_set_time(const char *path, vt_ticks t)
{
    struct timeval tv[2];
    tv[0].tv_sec = tv[1].tv_sec = (time_t)(t / 1000000);
    tv[0].tv_usec = tv[1].tv_usec = 0;
    return utimes(path, tv);
}

vt_ticks plat_get_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (vt_ticks)st.st_mtime * 1000000;
}

vt_ticks plat_get_ctime(const char *path)
{
    return plat_get_mtime(path);
}

void plat_reboot(void) {}
