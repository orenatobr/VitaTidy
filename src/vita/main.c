/* VitaTidy: reorder the LiveArea and the Adrenaline list, and mark games as completed. */
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <string.h>

#include "../core/platform.h"
#include "../core/tasks.h"
#include "ui.h"

#define APP_VERSION "1.0.0"
#define ROW_H 44
#define LIST_Y 108
#define LIST_ROWS 9

/* Heap for app.db (a few MB) plus the SQLite copy and icon buffers. */
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;

typedef enum { SCR_MENU, SCR_PLAN, SCR_GAMES, SCR_MESSAGE } Screen;
typedef enum { KIND_LIVEAREA, KIND_PSP } Kind;

typedef struct {
    unsigned pressed, held;
    int repeat_up, repeat_down; /* auto-repeat while held */
    int tap, tap_x, tap_y;      /* a short touch released this frame */
} Input;

static struct {
    Paths paths;
    Screen scr;
    int menu_sel;
    Kind kind;          /* which plan or game list is open */
    LaPlan la;
    PspPlan psp;
    GameList games;
    int sel, top;
    int confirm;        /* apply dialog is showing */
    int vita_changed;   /* a Vita icon changed: LiveArea needs a refresh */
    char toast[200];
    int toast_ok, toast_frames;
    char msg_title[64], msg_body[320];
    int msg_ok, msg_offer_reboot;
} S;

static void set_toast(int ok, const char *text)
{
    snprintf(S.toast, sizeof S.toast, "%s", text);
    S.toast_ok = ok;
    S.toast_frames = 180;
}

static void set_message(int ok, int offer_reboot, const char *title, const char *body)
{
    snprintf(S.msg_title, sizeof S.msg_title, "%s", title);
    snprintf(S.msg_body, sizeof S.msg_body, "%s", body);
    S.msg_ok = ok;
    S.msg_offer_reboot = offer_reboot;
    S.scr = SCR_MESSAGE;
}

/* Draws one frame with a notice so slow steps do not look frozen. */
static void busy(const char *text)
{
    ui_begin();
    const char *lines[] = {text};
    ui_dialog("Working", lines, 1, "Please wait", COL_ACCENT);
    ui_end();
}

static void free_screen_data(void)
{
    la_plan_free(&S.la);
    psp_plan_free(&S.psp);
    gamelist_free(&S.games);
}

/* ---- input ---- */

static void read_input(Input *in)
{
    static unsigned prev;
    static int held_up, held_down, touching, start_x, start_y, last_x, last_y;
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);

    unsigned held = pad.buttons;
    if (pad.ly < 60) held |= SCE_CTRL_UP;
    if (pad.ly > 200) held |= SCE_CTRL_DOWN;
    in->held = held;
    in->pressed = held & ~prev;
    prev = held;

    /* Hold up or down: first repeat after ~25 frames, then every 4. */
    in->repeat_up = in->repeat_down = 0;
    held_up = (held & SCE_CTRL_UP) ? held_up + 1 : 0;
    held_down = (held & SCE_CTRL_DOWN) ? held_down + 1 : 0;
    if (held_up == 1 || (held_up > 25 && held_up % 4 == 0)) in->repeat_up = 1;
    if (held_down == 1 || (held_down > 25 && held_down % 4 == 0)) in->repeat_down = 1;

    SceTouchData t;
    in->tap = 0;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
    if (t.reportNum > 0) {
        last_x = t.report[0].x / 2; /* touch space is 1920x1088 */
        last_y = t.report[0].y / 2;
        if (!touching) { touching = 1; start_x = last_x; start_y = last_y; }
    } else if (touching) {
        touching = 0;
        int dx = last_x - start_x, dy = last_y - start_y;
        if (dx * dx + dy * dy < 24 * 24) { in->tap = 1; in->tap_x = start_x; in->tap_y = start_y; }
    }
}

/* Moves the selection for D-pad/stick/shoulders; returns 1 when it changed. */
static int list_nav(const Input *in, int n)
{
    int old = S.sel;
    if (in->repeat_up && S.sel > 0) S.sel--;
    if (in->repeat_down && S.sel < n - 1) S.sel++;
    if (in->pressed & SCE_CTRL_LTRIGGER) S.sel = S.sel > LIST_ROWS ? S.sel - LIST_ROWS : 0;
    if (in->pressed & SCE_CTRL_RTRIGGER) S.sel = S.sel + LIST_ROWS < n ? S.sel + LIST_ROWS : n - 1;
    if (S.sel < S.top) S.top = S.sel;
    if (S.sel >= S.top + LIST_ROWS) S.top = S.sel - LIST_ROWS + 1;
    return S.sel != old;
}

/* Row under a touch point, or -1. */
static int row_at(const Input *in, int n)
{
    if (!in->tap || in->tap_y < LIST_Y || in->tap_x > SCREEN_W - 32) return -1;
    int row = S.top + (in->tap_y - LIST_Y) / ROW_H;
    return row < n && (in->tap_y - LIST_Y) / ROW_H < LIST_ROWS ? row : -1;
}

/* ---- screens ---- */

static const struct {
    const char *title, *hint;
} MENU[] = {
    {"Reorder LiveArea", "Sort the home screen: CFW apps first, then games A to Z"},
    {"Reorder PSP games", "Sort the Adrenaline list A to Z by rewriting ISO dates"},
    {"Mark Vita game", "Put a green check on a finished game's bubble"},
    {"Mark PSP game", "Put a green check on a finished game's icon in Adrenaline"},
};

static void open_plan(Kind kind)
{
    char err[200] = "";
    free_screen_data();
    busy(kind == KIND_LIVEAREA ? "Reading the LiveArea database..." : "Reading the ISO list...");
    int rc = kind == KIND_LIVEAREA ? task_livearea_plan(&S.paths, &S.la, err, sizeof err)
                                   : task_psp_plan(&S.paths, &S.psp, err, sizeof err);
    if (rc != 0) { set_message(0, 0, "Cannot continue", err); return; }
    S.kind = kind;
    S.sel = S.top = 0;
    S.confirm = 0;
    S.scr = SCR_PLAN;
}

static void open_games(Kind kind)
{
    char err[200] = "";
    free_screen_data();
    busy("Reading your games...");
    int rc = kind == KIND_LIVEAREA ? task_vita_games(&S.paths, &S.games, err, sizeof err)
                                   : task_psp_games(&S.paths, &S.games, err, sizeof err);
    if (rc != 0) { set_message(0, 0, "Cannot continue", err); return; }
    if (S.games.n == 0) { set_message(0, 0, "Nothing to mark", "No games with an icon were found."); return; }
    S.kind = kind;
    S.sel = S.top = 0;
    S.toast_frames = 0;
    S.scr = SCR_GAMES;
}

static void update_menu(const Input *in)
{
    if (in->repeat_up && S.menu_sel > 0) S.menu_sel--;
    if (in->repeat_down && S.menu_sel < 3) S.menu_sel++;
    int go = (in->pressed & SCE_CTRL_CROSS) != 0;
    if (in->tap && in->tap_y >= 120 && in->tap_x < SCREEN_W - 32) {
        int row = (in->tap_y - 120) / 90;
        if (row >= 0 && row < 4) { S.menu_sel = row; go = 1; }
    }
    if (!go) return;
    switch (S.menu_sel) {
    case 0: open_plan(KIND_LIVEAREA); break;
    case 1: open_plan(KIND_PSP); break;
    case 2: open_games(KIND_LIVEAREA); break;
    default: open_games(KIND_PSP); break;
    }
}

static void draw_menu(void)
{
    ui_header("VitaTidy", "Keep the home screen tidy");
    ui_text(SCREEN_W - 32 - ui_text_width(0.8f, "v" APP_VERSION), 56, COL_DIM, 0.8f, "v" APP_VERSION);
    for (int i = 0; i < 4; i++) {
        int y = 120 + i * 90;
        ui_card(32, y, SCREEN_W - 64, 78, S.menu_sel == i);
        ui_text_box(60, y + 6, 36, SCREEN_W - 140, COL_FG, 1.2f, MENU[i].title);
        ui_text_box(60, y + 38, 30, SCREEN_W - 140, COL_DIM, 0.85f, MENU[i].hint);
    }
    ui_footer("D-pad: move    X: open    Start: exit");
}

static int la_page_of(size_t i)
{
    return S.la.sys_pages + 1 + (int)(i / LA_ITEMS_PER_PAGE);
}

static void apply_plan(void)
{
    char err[300] = "";
    if (S.kind == KIND_LIVEAREA) {
        busy("Saving a backup and rewriting the LiveArea...");
        if (task_livearea_apply(&S.paths, &S.la, err, sizeof err) != 0) { set_message(0, 0, "Reorder failed", err); return; }
        set_message(1, 1, "LiveArea reordered",
                    "Restart the console now so the new order loads. A backup of the old database is in ux0:data/VitaTidy/backup.");
    } else {
        busy("Rewriting file dates...");
        if (task_psp_apply(&S.paths, &S.psp, err, sizeof err) != 0) { set_message(0, 0, "Some dates were not applied", err); return; }
        set_message(1, 0, "Adrenaline list reordered", "Close and reopen Adrenaline to see the new order.");
    }
}

static void update_plan(const Input *in)
{
    int n = S.kind == KIND_LIVEAREA ? (int)S.la.n : (int)S.psp.n;
    if (S.confirm) {
        if (in->pressed & SCE_CTRL_CROSS) { S.confirm = 0; apply_plan(); }
        if (in->pressed & SCE_CTRL_CIRCLE) S.confirm = 0;
        return;
    }
    list_nav(in, n);
    if (in->pressed & SCE_CTRL_CIRCLE) { S.scr = SCR_MENU; return; }
    if (in->pressed & SCE_CTRL_CROSS) S.confirm = 1;
}

static void draw_plan(void)
{
    int la = S.kind == KIND_LIVEAREA;
    int n = la ? (int)S.la.n : (int)S.psp.n;
    char sub[120];
    snprintf(sub, sizeof sub, la ? "%d apps will be placed in this order" : "%d files, first one becomes the newest", n);
    ui_header(la ? "Reorder LiveArea" : "Reorder PSP games", sub);

    for (int r = 0; r < LIST_ROWS && S.top + r < n; r++) {
        int i = S.top + r, y = LIST_Y + r * ROW_H;
        int cur = i == S.sel;
        ui_card(32, y, SCREEN_W - 72, ROW_H - 6, cur);
        char idx[16];
        snprintf(idx, sizeof idx, "%d", i + 1);
        ui_text_box(48, y, ROW_H - 6, 60, COL_DIM, 0.85f, idx);
        if (la) {
            char page[32];
            snprintf(page, sizeof page, "Page %d", la_page_of((size_t)i));
            int cw = ui_chip(SCREEN_W - 56, y + 8, ROW_H - 22, page, COL_MUTED, COL_DIM);
            ui_text_box(110, y, ROW_H - 6, SCREEN_W - 200 - cw, COL_FG, 1.0f, S.la.items[i].title[0] ? S.la.items[i].title : S.la.items[i].id);
        } else {
            char stem[200];
            snprintf(stem, sizeof stem, "%s", S.psp.names[i]);
            ui_text_box(110, y, ROW_H - 6, SCREEN_W - 200, COL_FG, 1.0f, stem);
        }
    }
    ui_scrollbar(S.top, LIST_ROWS, n, LIST_Y, LIST_ROWS * ROW_H - 6);
    ui_footer("D-pad: move    L/R: page    X: apply    O: back");

    if (S.confirm) {
        if (la) {
            const char *lines[] = {"Your current database is backed up first.", "The console restarts after the change."};
            ui_dialog("Rewrite the LiveArea?", lines, 2, "X: rewrite    O: cancel", COL_ACCENT);
        } else {
            const char *lines[] = {"File names and contents stay the same.", "Only the dates change, one minute apart."};
            ui_dialog("Rewrite the ISO dates?", lines, 2, "X: rewrite    O: cancel", COL_ACCENT);
        }
    }
}

static void toggle_game(void)
{
    Game *g = &S.games.v[S.sel];
    char err[200] = "", text[200];
    int now = g->marked;
    busy(g->marked ? "Removing the green check..." : "Drawing the green check...");
    int rc = S.kind == KIND_LIVEAREA ? task_vita_toggle(&S.paths, g->id, &now, err, sizeof err)
                                     : task_psp_toggle(&S.paths, g->id, &now, err, sizeof err);
    if (rc != 0) { set_toast(0, err); return; }
    g->marked = now;
    if (S.kind == KIND_LIVEAREA) S.vita_changed = 1;
    snprintf(text, sizeof text, now ? "Marked as completed" : "Mark removed");
    set_toast(1, text);
}

static void update_games(const Input *in)
{
    list_nav(in, (int)S.games.n);
    if (in->pressed & SCE_CTRL_CIRCLE) { S.scr = SCR_MENU; return; }
    int row = row_at(in, (int)S.games.n);
    int act = (in->pressed & SCE_CTRL_CROSS) != 0;
    if (row >= 0) { if (row == S.sel) act = 1; else S.sel = row; }
    if (act) toggle_game();
    if (S.kind == KIND_LIVEAREA && S.vita_changed && (in->pressed & SCE_CTRL_TRIANGLE)) {
        set_message(1, 1, "Restart to refresh", "The LiveArea reloads icons on boot.");
    }
    if (S.toast_frames > 0) S.toast_frames--;
}

static void draw_games(void)
{
    int n = (int)S.games.n, vita = S.kind == KIND_LIVEAREA;
    char sub[120];
    int done = 0;
    for (int i = 0; i < n; i++) done += S.games.v[i].marked;
    snprintf(sub, sizeof sub, "%d of %d completed", done, n);
    ui_header(vita ? "Mark Vita game" : "Mark PSP game", sub);

    for (int r = 0; r < LIST_ROWS && S.top + r < n; r++) {
        int i = S.top + r, y = LIST_Y + r * ROW_H;
        const Game *g = &S.games.v[i];
        ui_card(32, y, SCREEN_W - 72, ROW_H - 6, i == S.sel);
        int cw = 0;
        if (g->marked) cw = ui_chip(SCREEN_W - 56, y + 8, ROW_H - 22, "Completed", COL_ACCENT, COL_ON_ACC);
        ui_text_box(48, y, ROW_H - 6, SCREEN_W - 140 - cw, COL_FG, 1.0f, g->title);
    }
    ui_scrollbar(S.top, LIST_ROWS, n, LIST_Y, LIST_ROWS * ROW_H - 6);

    if (S.toast_frames > 0) {
        int w = ui_text_width(0.95f, S.toast) + 48, x = (SCREEN_W - w) / 2;
        ui_card(x, SCREEN_H - 112, w, 44, 0);
        ui_rect(x, SCREEN_H - 112, 5, 44, S.toast_ok ? COL_ACCENT : COL_ERROR);
        ui_text_box(x + 24, SCREEN_H - 112, 44, w, COL_FG, 0.95f, S.toast);
    }
    ui_footer(vita && S.vita_changed ? "X: toggle check    L/R: page    Triangle: restart now    O: back"
                                     : "X: toggle check    L/R: page    O: back");
}

static void update_message(const Input *in)
{
    if (S.msg_offer_reboot) {
        if (in->pressed & SCE_CTRL_CROSS) { plat_reboot(); }
        if (in->pressed & SCE_CTRL_CIRCLE) S.scr = SCR_MENU;
    } else if (in->pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
        S.scr = SCR_MENU;
    }
}

static void draw_message(void)
{
    draw_menu();
    /* Split the body across up to three lines at roughly 70 characters. */
    char lines[3][96] = {"", "", ""};
    const char *src = S.msg_body;
    for (int l = 0; l < 3 && *src; l++) {
        size_t take = strlen(src) > 70 ? 70 : strlen(src);
        if (strlen(src) > 70) { while (take > 0 && src[take] != ' ') take--; if (take == 0) take = 70; }
        snprintf(lines[l], sizeof lines[l], "%.*s", (int)take, src);
        src += take;
        while (*src == ' ') src++;
    }
    const char *ptr[3] = {lines[0], lines[1], lines[2]};
    int n = lines[2][0] ? 3 : lines[1][0] ? 2 : 1;
    ui_dialog(S.msg_title, ptr, n, S.msg_offer_reboot ? "X: restart now    O: later" : "X: ok", S.msg_ok ? COL_ACCENT : COL_ERROR);
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    paths_vita(&S.paths);
    ui_init();

    Input in;
    memset(&in, 0, sizeof in);
    for (;;) {
        read_input(&in);
        if (in.pressed & SCE_CTRL_START) break;
        switch (S.scr) {
        case SCR_MENU: update_menu(&in); break;
        case SCR_PLAN: update_plan(&in); break;
        case SCR_GAMES: update_games(&in); break;
        case SCR_MESSAGE: update_message(&in); break;
        }
        ui_begin();
        switch (S.scr) {
        case SCR_MENU: draw_menu(); break;
        case SCR_PLAN: draw_plan(); break;
        case SCR_GAMES: draw_games(); break;
        case SCR_MESSAGE: draw_message(); break;
        }
        ui_end();
    }

    free_screen_data();
    ui_shutdown();
    sceKernelExitProcess(0);
    return 0;
}
