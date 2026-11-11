/*
 * Developer tool: runs app tasks against directories you choose, so they can be tried on copies.
 *   task_cli psp-toggle <iso_dir> <data_dir> <name.iso>
 *   task_cli psp-apply  <iso_dir> <data_dir> <defaults_dir>
 *   task_cli vita-toggle <app_dir> <appmeta_dir> <data_dir> <title_id>
 */
#include <stdio.h>
#include <string.h>

#include "../src/core/tasks.h"

int main(int argc, char **argv)
{
    Paths p;
    memset(&p, 0, sizeof p);
    char err[300] = "";
    int marked = 0;
    if (argc == 5 && strcmp(argv[1], "psp-toggle") == 0) {
        snprintf(p.psp_iso, sizeof p.psp_iso, "%s", argv[2]);
        snprintf(p.data, sizeof p.data, "%s", argv[3]);
        if (task_psp_toggle(&p, argv[4], &marked, err, sizeof err) != 0) { fprintf(stderr, "error: %s\n", err); return 1; }
        printf("marked=%d\n", marked);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "psp-apply") == 0) {
        PspPlan plan;
        snprintf(p.psp_iso, sizeof p.psp_iso, "%s", argv[2]);
        snprintf(p.data, sizeof p.data, "%s", argv[3]);
        snprintf(p.defaults, sizeof p.defaults, "%s", argv[4]);
        if (task_psp_plan(&p, &plan, err, sizeof err) != 0) { fprintf(stderr, "error: %s\n", err); return 1; }
        for (size_t i = 0; i < plan.n; i++) printf("%2zu %s\n", i + 1, plan.names[i]);
        if (task_psp_apply(&p, &plan, err, sizeof err) != 0) { fprintf(stderr, "error: %s\n", err); return 1; }
        printf("dates applied\n");
        return 0;
    }
    if (argc == 6 && strcmp(argv[1], "vita-toggle") == 0) {
        snprintf(p.app, sizeof p.app, "%s", argv[2]);
        snprintf(p.appmeta, sizeof p.appmeta, "%s", argv[3]);
        snprintf(p.data, sizeof p.data, "%s", argv[4]);
        if (task_vita_toggle(&p, argv[5], &marked, err, sizeof err) != 0) { fprintf(stderr, "error: %s\n", err); return 1; }
        printf("marked=%d\n", marked);
        return 0;
    }
    fprintf(stderr, "usage: see the header of tools/task_cli.c\n");
    return 2;
}
