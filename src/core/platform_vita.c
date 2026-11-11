/* PS Vita implementation of the platform layer. */
#include "platform.h"

#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <string.h>

static vt_ticks to_ticks(const SceDateTime *dt)
{
    SceRtcTick t;
    if (sceRtcGetTick(dt, &t) != 0) return -1;
    return (vt_ticks)t.tick;
}

vt_ticks plat_now(void)
{
    SceDateTime dt;
    if (sceRtcGetCurrentClockLocalTime(&dt) != 0) return -1;
    return to_ticks(&dt);
}

int plat_set_time(const char *path, vt_ticks t)
{
    SceRtcTick tick = {.tick = (unsigned long long)t};
    SceIoStat st;
    memset(&st, 0, sizeof st);
    if (sceRtcSetTick(&st.st_mtime, &tick) != 0) return -1;
    st.st_ctime = st.st_mtime;
    st.st_atime = st.st_mtime;
    return sceIoChstat(path, &st, SCE_CST_CT | SCE_CST_AT | SCE_CST_MT) < 0 ? -1 : 0;
}

vt_ticks plat_get_mtime(const char *path)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    return to_ticks(&st.st_mtime);
}

vt_ticks plat_get_ctime(const char *path)
{
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) return -1;
    return to_ticks(&st.st_ctime);
}

void plat_reboot(void)
{
    scePowerRequestColdReset();
}
