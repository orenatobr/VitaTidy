/* Drawing primitives and the dark theme (vita2d, 960x544). */
#ifndef VT_UI_H
#define VT_UI_H

#include <vita2d.h>

#define SCREEN_W 960
#define SCREEN_H 544

/* Palette from the design system: dark slate surfaces, one green accent. */
#define COL_BG      RGBA8(15, 23, 42, 255)
#define COL_CARD    RGBA8(27, 35, 54, 255)
#define COL_MUTED   RGBA8(39, 47, 66, 255)
#define COL_BORDER  RGBA8(71, 85, 105, 255)
#define COL_FG      RGBA8(248, 250, 252, 255)
#define COL_DIM     RGBA8(148, 163, 184, 255)
#define COL_ACCENT  RGBA8(34, 197, 94, 255)
#define COL_ON_ACC  RGBA8(15, 23, 42, 255)
#define COL_ERROR   RGBA8(239, 68, 68, 255)
#define COL_SCRIM   RGBA8(2, 6, 23, 200)

void ui_init(void);
void ui_shutdown(void);
void ui_begin(void);
void ui_end(void);

void ui_text(int x, int y, unsigned color, float scale, const char *s);
int ui_text_width(float scale, const char *s);
/* Draws s vertically centered in a box of height h starting at y, cut with "..." at max_w. */
void ui_text_box(int x, int y, int h, int max_w, unsigned color, float scale, const char *s);

void ui_rect(int x, int y, int w, int h, unsigned color);
/* Card with a 1px border; focused cards get the accent border. */
void ui_card(int x, int y, int w, int h, int focused);
/* Right-aligned pill ending at x_right; returns its width. */
int ui_chip(int x_right, int y, int h, const char *text, unsigned bg, unsigned fg);

void ui_header(const char *title, const char *subtitle);
void ui_footer(const char *hints);
void ui_scrollbar(int top, int visible, int total, int y, int h);
/* Centered modal. lines are drawn one per row; hints go at the bottom. */
void ui_dialog(const char *title, const char *const *lines, int nlines, const char *hints, unsigned title_color);

#endif
