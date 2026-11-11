#include "ui.h"

#include <stdio.h>
#include <string.h>

static vita2d_pgf *font;

void ui_init(void)
{
    vita2d_init();
    vita2d_set_clear_color(COL_BG);
    font = vita2d_load_default_pgf();
}

void ui_shutdown(void)
{
    vita2d_free_pgf(font);
    vita2d_fini();
}

void ui_begin(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void ui_end(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void ui_text(int x, int y, unsigned color, float scale, const char *s)
{
    vita2d_pgf_draw_text(font, x, y, color, scale, s);
}

int ui_text_width(float scale, const char *s)
{
    return vita2d_pgf_text_width(font, scale, s);
}

void ui_text_box(int x, int y, int h, int max_w, unsigned color, float scale, const char *s)
{
    char full[200], out[208];
    snprintf(full, sizeof full, "%s", s);
    snprintf(out, sizeof out, "%s", full);
    for (int n = (int)strlen(full); n > 0 && ui_text_width(scale, out) > max_w; n--) {
        if ((full[n] & 0xC0) == 0x80) continue; /* cut only on a UTF-8 boundary */
        snprintf(out, sizeof out, "%.*s...", n, full);
    }
    int th = vita2d_pgf_text_height(font, scale, "Ag");
    ui_text(x, y + (h + th) / 2 - th / 8, color, scale, out);
}

void ui_rect(int x, int y, int w, int h, unsigned color)
{
    vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)h, color);
}

void ui_card(int x, int y, int w, int h, int focused)
{
    ui_rect(x, y, w, h, focused ? COL_ACCENT : COL_BORDER);
    ui_rect(x + 1, y + 1, w - 2, h - 2, focused ? COL_MUTED : COL_CARD);
}

int ui_chip(int x_right, int y, int h, const char *text, unsigned bg, unsigned fg)
{
    int w = ui_text_width(0.85f, text) + 24;
    ui_rect(x_right - w, y, w, h, bg);
    ui_text_box(x_right - w + 12, y, h, w, fg, 0.85f, text);
    return w;
}

void ui_header(const char *title, const char *subtitle)
{
    ui_text(32, 56, COL_FG, 1.6f, title);
    if (subtitle) ui_text_box(32, 62, 28, SCREEN_W - 64, COL_DIM, 0.9f, subtitle);
    ui_rect(32, 96, SCREEN_W - 64, 1, COL_BORDER);
}

void ui_footer(const char *hints)
{
    ui_rect(32, SCREEN_H - 52, SCREEN_W - 64, 1, COL_BORDER);
    ui_text_box(32, SCREEN_H - 50, 44, SCREEN_W - 64, COL_DIM, 0.9f, hints);
}

void ui_scrollbar(int top, int visible, int total, int y, int h)
{
    if (total <= visible) return;
    ui_rect(SCREEN_W - 24, y, 4, h, COL_MUTED);
    int bar = h * visible / total;
    if (bar < 24) bar = 24;
    ui_rect(SCREEN_W - 24, y + (h - bar) * top / (total - visible), 4, bar, COL_DIM);
}

void ui_dialog(const char *title, const char *const *lines, int nlines, const char *hints, unsigned title_color)
{
    ui_rect(0, 0, SCREEN_W, SCREEN_H, COL_SCRIM);
    int w = 680, h = 120 + nlines * 30 + 48, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    ui_card(x, y, w, h, 0);
    ui_rect(x, y, 6, h, title_color);
    ui_text(x + 32, y + 48, title_color, 1.25f, title);
    for (int i = 0; i < nlines; i++) ui_text_box(x + 32, y + 64 + i * 30, 30, w - 64, COL_FG, 0.95f, lines[i]);
    ui_text_box(x + 32, y + h - 56, 40, w - 64, COL_DIM, 0.9f, hints);
}
