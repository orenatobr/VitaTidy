/* The four things the app does, independent of the UI and of the console. */
#ifndef VT_TASKS_H
#define VT_TASKS_H

#include <stddef.h>

#include "livearea.h"

typedef struct {
    char db[256];       /* LiveArea database */
    char appmeta[256];  /* firmware icon cache */
    char app[256];      /* installed games */
    char data[256];     /* our own data: backups, originals, order files */
    char defaults[256]; /* bundled defaults shipped in the VPK */
    char psp_iso[256];  /* Adrenaline ISO folder */
} Paths;

void paths_vita(Paths *p);

typedef struct {
    char id[128];    /* title id (Vita) or file name (PSP) */
    char title[128]; /* what the user sees */
    int marked;      /* completed badge is on */
} Game;

typedef struct {
    Game *v;
    size_t n;
} GameList;

void gamelist_free(GameList *l);

/* ---- Vita LiveArea ---- */
int task_livearea_plan(const Paths *p, LaPlan *plan, char *err, size_t errlen);
/* Backs up app.db, applies the plan, validates the result and replaces the file. */
int task_livearea_apply(const Paths *p, const LaPlan *plan, char *err, size_t errlen);

/* ---- PSP list order ---- */
typedef struct {
    char **names; /* first entry becomes the newest file */
    size_t n;
} PspPlan;

int task_psp_plan(const Paths *p, PspPlan *plan, char *err, size_t errlen);
/* Rewrites file dates one minute apart. Returns 0 when every date was verified. */
int task_psp_apply(const Paths *p, const PspPlan *plan, char *err, size_t errlen);
void psp_plan_free(PspPlan *plan);

/* ---- completed badge ---- */
int task_vita_games(const Paths *p, GameList *out, char *err, size_t errlen);
int task_vita_toggle(const Paths *p, const char *title_id, int *now_marked, char *err, size_t errlen);
int task_psp_games(const Paths *p, GameList *out, char *err, size_t errlen);
int task_psp_toggle(const Paths *p, const char *iso_name, int *now_marked, char *err, size_t errlen);

#endif
