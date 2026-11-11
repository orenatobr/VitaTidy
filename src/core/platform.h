/* Thin platform layer: clock, file times and reboot. Two implementations (host, Vita). */
#ifndef VT_PLATFORM_H
#define VT_PLATFORM_H

typedef long long vt_ticks; /* microseconds; the epoch is platform specific */

#define VT_MINUTE ((vt_ticks)60 * 1000000)

vt_ticks plat_now(void);
/* Sets creation, access and modification time. Returns 0 on success. */
int plat_set_time(const char *path, vt_ticks t);
/* Returns the modification time, or -1 on error. */
vt_ticks plat_get_mtime(const char *path);
/* Returns the creation time, or -1 on error (the host build has none and returns the modification time). */
vt_ticks plat_get_ctime(const char *path);
/* Cold-resets the console (no-op on the host). */
void plat_reboot(void);

#endif
